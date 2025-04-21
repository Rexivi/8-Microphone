#include "AudioCapture.h"

static const char *TAG = "AudioCapture";

// Task handles
static TaskHandle_t audioTaskHandle = NULL;
static TaskHandle_t fileTaskHandle = NULL;

// Double buffer for audio data
static AudioDoubleBuffer audioBuffer = {
    .mutex = {NULL, NULL},
    .buffer = {NULL, NULL},
    .size = AUDIO_BUFFER_SIZE,
    .activeBuffer = 0,
    .readyBuffer = -1,
    .writePos = 0
};

// Queue for communication between tasks
static QueueHandle_t dataQueue = NULL;

// File handle
static FILE *audioFile = NULL;

// Task states
static bool tasksRunning = false;

// Audio data capture task
static void audio_capture_task(void *pvParameters) {
    size_t bytes_read;
    esp_err_t result;
    
    ESP_LOGI(TAG, "Audio capture task started");
    
    while (1) {
        // Check if task should be suspended
        if (ulTaskNotifyTake(pdTRUE, 0)) {
            // Notify received, suspend the task
            ESP_LOGI(TAG, "Audio capture task going to suspend");
            vTaskSuspend(NULL);
            ESP_LOGI(TAG, "Audio capture task resumed");
            continue;
        }
        
        // Get current active buffer
        int bufferIndex = audioBuffer.activeBuffer;
        
        // Try to lock the current buffer for writing
        if (xSemaphoreTake(audioBuffer.mutex[bufferIndex], portMAX_DELAY) == pdTRUE) {
            // Calculate buffer position and remaining space
            uint8_t* bufPtr = audioBuffer.buffer[bufferIndex] + audioBuffer.writePos;
            size_t bytesToRead = audioBuffer.size - audioBuffer.writePos;
            
            // Read data from I2S directly into buffer
            result = i2s_channel_read(rx_chan, bufPtr, bytesToRead, &bytes_read, portMAX_DELAY);
            
            // Check if read was successful and data was received
            if (result == ESP_OK && bytes_read > 0) {
                // Update write position
                audioBuffer.writePos += bytes_read;
                
                // Check if buffer is full
                if (audioBuffer.writePos >= audioBuffer.size) {
                    // Reset write position for next buffer
                    audioBuffer.writePos = 0;
                    // Mark current buffer as ready
                    audioBuffer.readyBuffer = bufferIndex;
                    // Switch to other buffer
                    audioBuffer.activeBuffer ^= 1;
                    // Send notification to file task
                    uint8_t bufferIndex = audioBuffer.readyBuffer;  // 或直接使用当前bufferIndex
                    xQueueSend(dataQueue, &bufferIndex, portMAX_DELAY);
                }
            } else if (result != ESP_OK) {
                ESP_LOGW(TAG, "I2S read error: %s", esp_err_to_name(result));
            }
            
            // Release buffer mutex
            xSemaphoreGive(audioBuffer.mutex[bufferIndex]);
        }
    }
}

// File save task
static void file_save_task(void *pvParameters) {
    
    ESP_LOGI(TAG, "File save task started");
    
    // Create file
    audioFile = fopen(AUDIO_FILE_PATH, "wb");
    setvbuf(audioFile, NULL, _IOFBF, 8192);  // 使用全缓冲模式
    if (audioFile == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        vTaskDelete(NULL);
        fileTaskHandle = NULL;
        return;
    }
    
    ESP_LOGI(TAG, "File opened: %s", AUDIO_FILE_PATH);
    
    while (1) {
        // Check if task should be suspended
        if (ulTaskNotifyTake(pdTRUE, 0)) {
            // Save current buffer if any
            if (audioBuffer.readyBuffer != -1) {
                int bufferIndex = audioBuffer.readyBuffer;
                
                if (xSemaphoreTake(audioBuffer.mutex[bufferIndex], portMAX_DELAY) == pdTRUE) {
                    // Write buffer to file
                    size_t written = fwrite(audioBuffer.buffer[bufferIndex], 1, audioBuffer.size, audioFile);
                    if (written != audioBuffer.size) {
                        ESP_LOGW(TAG, "(1)Failed to write all data to file: %d/%d", written, audioBuffer.size);
                    }
                    
                    // Reset ready buffer
                    audioBuffer.readyBuffer = -1;
                    xSemaphoreGive(audioBuffer.mutex[bufferIndex]);
                }
            }
            
            // Flush and close the file
            if (audioFile != NULL) {
                fflush(audioFile);
                fclose(audioFile);
                audioFile = NULL;
                ESP_LOGI(TAG, "File closed");
            }
            
            // Suspend the task
            ESP_LOGI(TAG, "File save task going to suspend");
            vTaskSuspend(NULL);
            
            // Create new file when resumed
            ESP_LOGI(TAG, "File save task resumed");
            audioFile = fopen(AUDIO_FILE_PATH, "wb");
            if (audioFile == NULL) {
                ESP_LOGE(TAG, "Failed to open file for writing after resume");
                vTaskDelete(NULL);
                fileTaskHandle = NULL;
                return;
            }
            ESP_LOGI(TAG, "New file opened: %s", AUDIO_FILE_PATH);
            continue;
        }
        
        uint8_t bufferIndex;
        if (xQueueReceive(dataQueue, &bufferIndex, portMAX_DELAY) == pdTRUE) {
            // 直接使用接收到的bufferIndex处理相应缓冲区
            if (xSemaphoreTake(audioBuffer.mutex[bufferIndex], portMAX_DELAY) == pdTRUE) {
                
                // Write buffer to file
                size_t written = fwrite(audioBuffer.buffer[bufferIndex], 1, audioBuffer.size, audioFile);
                if (written != audioBuffer.size) {
                    ESP_LOGW(TAG, "(2)Failed to write all data to file: %d/%d", written, audioBuffer.size);
                }
                
                // Reset ready buffer
                audioBuffer.readyBuffer = -1;
                xSemaphoreGive(audioBuffer.mutex[bufferIndex]);
            }
        }
    }
}

// Initialize audio capture system
static esp_err_t audio_capture_init(void) {
    // Create data queue
    dataQueue = xQueueCreate(10, sizeof(uint8_t));
    if (dataQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create data queue");
        return ESP_FAIL;
    }
    
    // Allocate DMA-capable memory for buffers
    for (int i = 0; i < 2; i++) {
        // ESP_LOGI(TAG, "可用DMA内存: %d KB", heap_caps_get_free_size(MALLOC_CAP_DMA) / 1024);
        // ESP_LOGI(TAG, "可用8BIT内存: %d KB", heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024);
        // ESP_LOGI(TAG, "DMA内存堆信息:");
        // heap_caps_print_heap_info(MALLOC_CAP_DMA);
        audioBuffer.buffer[i] = heap_caps_malloc(AUDIO_BUFFER_SIZE, MALLOC_CAP_DMA);
        if (audioBuffer.buffer[i] == NULL) {
            ESP_LOGE(TAG, "Failed to allocate DMA buffer %d", i);
            // Free previously allocated buffers
            for (int j = 0; j < i; j++) {
                heap_caps_free(audioBuffer.buffer[j]);
                audioBuffer.buffer[j] = NULL;
            }
            vQueueDelete(dataQueue);
            dataQueue = NULL;
            return ESP_ERR_NO_MEM;
        }
        
        // Clear buffer
        memset(audioBuffer.buffer[i], 0, AUDIO_BUFFER_SIZE);
        
        // Create mutex for this buffer
        audioBuffer.mutex[i] = xSemaphoreCreateMutex();
        if (audioBuffer.mutex[i] == NULL) {
            ESP_LOGE(TAG, "Failed to create mutex for buffer %d", i);
            // Free resources
            for (int j = 0; j <= i; j++) {
                heap_caps_free(audioBuffer.buffer[j]);
                audioBuffer.buffer[j] = NULL;
                if (j < i) {
                    vSemaphoreDelete(audioBuffer.mutex[j]);
                    audioBuffer.mutex[j] = NULL;
                }
            }
            vQueueDelete(dataQueue);
            dataQueue = NULL;
            return ESP_ERR_NO_MEM;
        }
    }
    
    // Reset buffer state
    audioBuffer.activeBuffer = 0;
    audioBuffer.readyBuffer = -1;
    audioBuffer.writePos = 0;
    
    return ESP_OK;
}

// Release all resources
static void audio_capture_deinit(void) {
    // Free buffer resources
    for (int i = 0; i < 2; i++) {
        if (audioBuffer.buffer[i] != NULL) {
            heap_caps_free(audioBuffer.buffer[i]);
            audioBuffer.buffer[i] = NULL;
        }
        
        if (audioBuffer.mutex[i] != NULL) {
            vSemaphoreDelete(audioBuffer.mutex[i]);
            audioBuffer.mutex[i] = NULL;
        }
    }
    
    // Delete queue
    if (dataQueue != NULL) {
        vQueueDelete(dataQueue);
        dataQueue = NULL;
    }
    
    // Close file if open
    if (audioFile != NULL) {
        fclose(audioFile);
        audioFile = NULL;
    }
}

// Start audio capture
esp_err_t audio_capture_start(void) {
    esp_err_t ret = ESP_OK;
    static bool resources_initialized = false;
    
    // 检查任务是否已在运行
    if (tasksRunning) {
        // 检查任务是否被挂起
        if (audioTaskHandle != NULL && eTaskGetState(audioTaskHandle) == eSuspended) {
            // 恢复任务
            vTaskResume(audioTaskHandle);
            vTaskResume(fileTaskHandle);
            ESP_LOGI(TAG, "音频捕获任务已恢复");
            return ESP_OK;
        } else {
            // 任务已在运行
            ESP_LOGW(TAG, "音频捕获已在运行中");
            return ESP_OK;
        }
    }
    
    // 检查任务是否存在但未标记为运行（异常状态）
    if (audioTaskHandle != NULL || fileTaskHandle != NULL) {
        ESP_LOGW(TAG, "任务句柄存在但未标记为运行 - 正在清理");
        // 删除所有现有任务
        if (audioTaskHandle != NULL) {
            vTaskDelete(audioTaskHandle);
            audioTaskHandle = NULL;
        }
        if (fileTaskHandle != NULL) {
            vTaskDelete(fileTaskHandle);
            fileTaskHandle = NULL;
        }
    }
    
    // 只有在资源尚未初始化时才初始化
    if (!resources_initialized) {
        // 初始化资源
        ret = audio_capture_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "初始化音频捕获失败: %s", esp_err_to_name(ret));
            audio_capture_deinit();
            return ret;
        }
           
    
        // 创建音频捕获任务
        BaseType_t xReturned = xTaskCreatePinnedToCore(
            audio_capture_task,
            "audio_capture_task",
            AUDIO_TASK_STACK_SIZE,
            NULL,
            AUDIO_TASK_PRIORITY,
            &audioTaskHandle,
            0  // 绑定到核心0
        );
        
        if (xReturned != pdPASS) {
            ESP_LOGE(TAG, "创建音频捕获任务失败");
            audio_capture_deinit();
            resources_initialized = false;
            return ESP_ERR_NO_MEM;
        }
        
        // 创建文件保存任务
        xReturned = xTaskCreatePinnedToCore(
            file_save_task,
            "file_save_task",
            FILE_TASK_STACK_SIZE,
            NULL,
            FILE_TASK_PRIORITY,
            &fileTaskHandle,
            1  // 绑定到核心1
        );
        
        if (xReturned != pdPASS) {
            ESP_LOGE(TAG, "创建文件保存任务失败");
            if (audioTaskHandle != NULL) {
                vTaskDelete(audioTaskHandle);
                audioTaskHandle = NULL;
            }
            audio_capture_deinit();
            resources_initialized = false;
            return ESP_ERR_NO_MEM;
        }
        resources_initialized = true;
        // 标记任务为运行状态
        tasksRunning = true;
        ESP_LOGI(TAG, "音频捕获已启动");
        return ESP_OK;
    }
    return ESP_OK;
}

// Stop audio capture
esp_err_t audio_capture_stop(void) {
    // Check if tasks are running
    if (!tasksRunning) {
        ESP_LOGW(TAG, "Audio capture not running");
        return ESP_OK;
    }
    
    // Check if tasks exist
    if (audioTaskHandle == NULL || fileTaskHandle == NULL) {
        ESP_LOGW(TAG, "Audio capture tasks not found");
        tasksRunning = false;
        return ESP_OK;
    }
    
    // Notify tasks to suspend
    xTaskNotifyGive(audioTaskHandle);
    xTaskNotifyGive(fileTaskHandle);
    
    // Wait for tasks to suspend
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Confirm tasks are suspended
    if (eTaskGetState(audioTaskHandle) != eSuspended || 
        eTaskGetState(fileTaskHandle) != eSuspended) {
        ESP_LOGW(TAG, "Failed to suspend tasks");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Audio capture stopped");
    return ESP_OK;
}

// 检查音频捕获是否在运行
bool audio_capture_is_running(void) {
    return tasksRunning && 
           audioTaskHandle != NULL && 
           fileTaskHandle != NULL && 
           eTaskGetState(audioTaskHandle) != eSuspended;
}