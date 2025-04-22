#include "AudioCapture.h"

static const char *TAG = "AudioCapture";

// 任务句柄
static TaskHandle_t audioTaskHandle = NULL;
static TaskHandle_t fileTaskHandle = NULL;

// 音频数据的N缓冲区系统（替代双缓冲区）
#define NUM_BUFFERS 6  // 缓冲区数量(N) - 可调整

typedef struct {
    SemaphoreHandle_t mutex[NUM_BUFFERS];
    uint8_t *buffer[NUM_BUFFERS];
    size_t size;        // 每个缓冲区的大小
    int activeBuffer;   // 当前用于写入的活动缓冲区
    bool bufferReady[NUM_BUFFERS]; // 跟踪哪些缓冲区准备好保存
} AudioMultiBuffer;

static AudioMultiBuffer audioBuffer = {
    .mutex = {NULL},
    .buffer = {NULL},
    .size = AUDIO_BUFFER_SIZE,
    .activeBuffer = 0,
    .bufferReady = {false}
};

// 用于任务间通信的队列
static QueueHandle_t dataQueue = NULL;

// 文件句柄
static FILE *audioFile = NULL;
static char currentFilePath[128] = {0}; // 存储当前文件路径的缓冲区

// 任务状态
static bool tasksRunning = false;

// 为每个录制会话生成唯一文件名的函数
static esp_err_t generate_audio_filename(char *file_path, size_t max_len) {
    DIR *dir;
    struct dirent *entry;
    int max_index = 0;
    char pattern[32];
    int file_index;
    
    // 确保输出缓冲区有效
    if (file_path == NULL || max_len < 16) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 打开目录
    dir = opendir(AUDIO_FILE_DIR);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory: %s", AUDIO_FILE_DIR);
        // 如果目录无法打开，默认使用Audio1.bin
        snprintf(file_path, max_len, "%s/%s1%s", AUDIO_FILE_DIR, AUDIO_FILE_PREFIX, AUDIO_FILE_EXT);
        return ESP_ERR_NOT_FOUND;
    }
    
    // 创建匹配模式（例如，"Audio"）
    snprintf(pattern, sizeof(pattern), "%s", AUDIO_FILE_PREFIX);
    size_t pattern_len = strlen(pattern);
    
    // 扫描目录中的所有文件
    while ((entry = readdir(dir)) != NULL) {
        // 检查文件名是否匹配我们的模式
        if (strncmp(entry->d_name, pattern, pattern_len) == 0) {
            // 尝试提取前缀后的索引号
            if (sscanf(entry->d_name + pattern_len, "%d", &file_index) == 1) {
                // 如果这个索引比我们当前的最大值高，则更新最大值
                if (file_index > max_index) {
                    max_index = file_index;
                }
            }
        }
    }
    
    // 关闭目录
    closedir(dir);
    
    // 创建比找到的最大索引高一的新文件名
    snprintf(file_path, max_len, "%s/%s%d%s", 
             AUDIO_FILE_DIR, AUDIO_FILE_PREFIX, max_index + 1, AUDIO_FILE_EXT);
    
    ESP_LOGI(TAG, "Generated filename: %s", file_path);
    return ESP_OK;
}

// 音频数据捕获任务
static void audio_capture_task(void *pvParameters) {
    size_t bytes_read;
    esp_err_t result;
    size_t writePos = 0;  // 当前缓冲区的本地写入位置
    
    ESP_LOGI(TAG, "Audio capture task started");
    
    while (1) {
        // 检查任务是否应该暂停
        if (ulTaskNotifyTake(pdTRUE, 0)) {
            // 收到通知，暂停任务
            ESP_LOGI(TAG, "Audio capture task going to suspend");
            vTaskSuspend(NULL);
            ESP_LOGI(TAG, "Audio capture task resumed");
            continue;
        }
        
        // 尝试找到一个空闲缓冲区
        bool bufferFound = false;
        int startBuffer = audioBuffer.activeBuffer;  // 记住我们开始搜索的位置
        
        // 从当前activeBuffer开始，以循环方式尝试所有缓冲区
        for (int i = 0; i < NUM_BUFFERS; i++) {
            int bufferIndex = (startBuffer + i) % NUM_BUFFERS;
            
            // 尝试锁定缓冲区以进行写入（非阻塞）
            if (xSemaphoreTake(audioBuffer.mutex[bufferIndex], 0) == pdTRUE) {
                // 检查缓冲区是否未标记为就绪（不等待保存）
                if (!audioBuffer.bufferReady[bufferIndex]) {
                    // 我们找到了一个可用的缓冲区
                    audioBuffer.activeBuffer = bufferIndex;
                    bufferFound = true;
                    
                    // 计算此缓冲区中的剩余空间
                    uint8_t* bufPtr = audioBuffer.buffer[bufferIndex] + writePos;
                    size_t bytesToRead = audioBuffer.size - writePos;
                    
                    // 从I2S直接读取数据到缓冲区
                    result = i2s_channel_read(rx_chan, bufPtr, bytesToRead, &bytes_read, portMAX_DELAY);
                    
                    // 检查读取是否成功且接收到数据
                    if (result == ESP_OK && bytes_read > 0) {
                        // 更新写入位置
                        writePos += bytes_read;
                        
                        // 检查缓冲区是否已满
                        if (writePos >= audioBuffer.size) {
                            // 为下一个缓冲区重置写入位置
                            writePos = 0;
                            
                            // 标记此缓冲区为就绪
                            audioBuffer.bufferReady[bufferIndex] = true;
                            
                            // 发送缓冲区索引到文件任务
                            xQueueSend(dataQueue, &bufferIndex, portMAX_DELAY);
                        }
                    } else if (result != ESP_OK) {
                        ESP_LOGW(TAG, "I2S read error: %s", esp_err_to_name(result));
                    }
                    
                    // 释放缓冲区互斥锁
                    xSemaphoreGive(audioBuffer.mutex[bufferIndex]);
                    break;  // 既然已处理数据，退出for循环
                } else {
                    // 缓冲区标记为就绪但我们获得了互斥锁 - 释放它并尝试另一个
                    xSemaphoreGive(audioBuffer.mutex[bufferIndex]);
                }
            }
            // 如果我们无法获取互斥锁或缓冲区已就绪，尝试下一个
        }
        
        // 如果未找到缓冲区，短暂让出CPU再试
        if (!bufferFound) {
            taskYIELD();  // 让其他任务有机会运行
        }
    }
}

// 文件保存任务
static void file_save_task(void *pvParameters) {
    ESP_LOGI(TAG, "File save task started");
    
    // 生成唯一文件名
    memset(currentFilePath, 0, sizeof(currentFilePath));
    if (generate_audio_filename(currentFilePath, sizeof(currentFilePath)) != ESP_OK) {
        ESP_LOGW(TAG, "Error generating filename, using fallback");
    }
    
    // 创建文件
    audioFile = fopen(currentFilePath, "wb");
    setvbuf(audioFile, NULL, _IOFBF, 8192);  // 使用完全缓冲模式
    if (audioFile == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", currentFilePath);
        vTaskDelete(NULL);
        fileTaskHandle = NULL;
        return;
    }
    
    ESP_LOGI(TAG, "File opened: %s", currentFilePath);
    
    while (1) {
        // 检查任务是否应该暂停
        if (ulTaskNotifyTake(pdTRUE, 0)) {
            // 刷新任何待处理的缓冲区
            for (int i = 0; i < NUM_BUFFERS; i++) {
                if (audioBuffer.bufferReady[i]) {
                    if (xSemaphoreTake(audioBuffer.mutex[i], portMAX_DELAY) == pdTRUE) {
                        // 将缓冲区写入文件
                        size_t written = fwrite(audioBuffer.buffer[i], 1, audioBuffer.size, audioFile);
                        if (written != audioBuffer.size) {
                            ESP_LOGW(TAG, "Failed to write all data to file: %d/%d", written, audioBuffer.size);
                        }
                        
                        // 标记缓冲区为可用
                        audioBuffer.bufferReady[i] = false;
                        xSemaphoreGive(audioBuffer.mutex[i]);
                    }
                }
            }
            
            // 刷新并关闭文件
            if (audioFile != NULL) {
                fflush(audioFile);
                fclose(audioFile);
                audioFile = NULL;
                ESP_LOGI(TAG, "File closed");
            }
            
            // 暂停任务
            ESP_LOGI(TAG, "File save task going to suspend");
            vTaskSuspend(NULL);
            
            // 恢复时创建新文件
            ESP_LOGI(TAG, "File save task resumed");
            
            // 生成新的唯一文件名
            memset(currentFilePath, 0, sizeof(currentFilePath));
            if (generate_audio_filename(currentFilePath, sizeof(currentFilePath)) != ESP_OK) {
                ESP_LOGW(TAG, "Error generating filename, using fallback");
            }
            
            audioFile = fopen(currentFilePath, "wb");
            if (audioFile == NULL) {
                ESP_LOGE(TAG, "Failed to open file for writing after resume: %s", currentFilePath);
                vTaskDelete(NULL);
                fileTaskHandle = NULL;
                return;
            }
            ESP_LOGI(TAG, "New file opened: %s", currentFilePath);
            continue;
        }
        
        // 从队列接收缓冲区索引
        uint8_t bufferIndex;
        if (xQueueReceive(dataQueue, &bufferIndex, portMAX_DELAY) == pdTRUE) {
            if (bufferIndex < NUM_BUFFERS && audioBuffer.bufferReady[bufferIndex]) {
                if (xSemaphoreTake(audioBuffer.mutex[bufferIndex], portMAX_DELAY) == pdTRUE) {
                    // 将缓冲区写入文件
                    size_t written = fwrite(audioBuffer.buffer[bufferIndex], 1, audioBuffer.size, audioFile);
                    if (written != audioBuffer.size) {
                        ESP_LOGW(TAG, "Failed to write all data to file: %d/%d", written, audioBuffer.size);
                    }
                    
                    // 将缓冲区重新标记为可用
                    audioBuffer.bufferReady[bufferIndex] = false;
                    xSemaphoreGive(audioBuffer.mutex[bufferIndex]);
                }
            } else {
                ESP_LOGW(TAG, "Received invalid buffer index: %d", bufferIndex);
            }
        }
    }
}

// 初始化音频捕获系统
static esp_err_t audio_capture_init(void) {
    // 创建数据队列 - 增加大小以容纳更多缓冲区
    dataQueue = xQueueCreate(NUM_BUFFERS * 2, sizeof(uint8_t));
    if (dataQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create data queue");
        return ESP_FAIL;
    }
    
    // 为缓冲区分配DMA可用内存
    for (int i = 0; i < NUM_BUFFERS; i++) {
        audioBuffer.buffer[i] = heap_caps_malloc(AUDIO_BUFFER_SIZE, MALLOC_CAP_DMA);
        if (audioBuffer.buffer[i] == NULL) {
            ESP_LOGE(TAG, "Failed to allocate DMA buffer %d", i);
            // 释放先前分配的缓冲区
            for (int j = 0; j < i; j++) {
                heap_caps_free(audioBuffer.buffer[j]);
                audioBuffer.buffer[j] = NULL;
            }
            vQueueDelete(dataQueue);
            dataQueue = NULL;
            return ESP_ERR_NO_MEM;
        }
        
        // 清空缓冲区
        memset(audioBuffer.buffer[i], 0, AUDIO_BUFFER_SIZE);
        
        // 为此缓冲区创建互斥锁
        audioBuffer.mutex[i] = xSemaphoreCreateMutex();
        if (audioBuffer.mutex[i] == NULL) {
            ESP_LOGE(TAG, "Failed to create mutex for buffer %d", i);
            // 释放资源
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
        
        // 初始化缓冲区状态
        audioBuffer.bufferReady[i] = false;
    }
    
    // 重置缓冲区状态
    audioBuffer.activeBuffer = 0;
    
    return ESP_OK;
}

// 释放所有资源
static void audio_capture_deinit(void) {
    // 释放缓冲区资源
    for (int i = 0; i < NUM_BUFFERS; i++) {
        if (audioBuffer.buffer[i] != NULL) {
            heap_caps_free(audioBuffer.buffer[i]);
            audioBuffer.buffer[i] = NULL;
        }
        
        if (audioBuffer.mutex[i] != NULL) {
            vSemaphoreDelete(audioBuffer.mutex[i]);
            audioBuffer.mutex[i] = NULL;
        }
    }
    
    // 删除队列
    if (dataQueue != NULL) {
        vQueueDelete(dataQueue);
        dataQueue = NULL;
    }
    
    // 关闭文件（如果打开）
    if (audioFile != NULL) {
        fclose(audioFile);
        audioFile = NULL;
    }
}

// 开始音频捕获
esp_err_t audio_capture_start(void) {
    esp_err_t ret = ESP_OK;
    static bool resources_initialized = false;
    
    // 检查任务是否已经运行
    if (tasksRunning) {
        // 检查任务是否已暂停
        if (audioTaskHandle != NULL && eTaskGetState(audioTaskHandle) == eSuspended) {
            // 恢复任务
            vTaskResume(audioTaskHandle);
            vTaskResume(fileTaskHandle);
            ESP_LOGI(TAG, "Audio capture tasks resumed");
            return ESP_OK;
        } else {
            // 任务已经在运行
            ESP_LOGW(TAG, "Audio capture already running");
            return ESP_OK;
        }
    }
    
    // 检查任务句柄存在但未标记为运行的异常状态
    if (audioTaskHandle != NULL || fileTaskHandle != NULL) {
        ESP_LOGW(TAG, "Task handles exist but not marked as running - cleaning up");
        // 删除任何现有任务
        if (audioTaskHandle != NULL) {
            vTaskDelete(audioTaskHandle);
            audioTaskHandle = NULL;
        }
        if (fileTaskHandle != NULL) {
            vTaskDelete(fileTaskHandle);
            fileTaskHandle = NULL;
        }
    }
    
    // 仅在尚未完成的情况下初始化资源
    if (!resources_initialized) {
        // 初始化资源
        ret = audio_capture_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize audio capture: %s", esp_err_to_name(ret));
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
            0  // 核心0
        );
        
        if (xReturned != pdPASS) {
            ESP_LOGE(TAG, "Failed to create audio capture task");
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
            1  // 核心1
        );
        
        if (xReturned != pdPASS) {
            ESP_LOGE(TAG, "Failed to create file save task");
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
        ESP_LOGI(TAG, "Audio capture started");
        return ESP_OK;
    }
    return ESP_OK;
}

// 停止音频捕获
esp_err_t audio_capture_stop(void) {
    // 检查任务是否正在运行
    if (!tasksRunning) {
        ESP_LOGW(TAG, "Audio capture not running");
        return ESP_OK;
    }
    
    // 检查任务是否存在
    if (audioTaskHandle == NULL || fileTaskHandle == NULL) {
        ESP_LOGW(TAG, "Audio capture tasks not found");
        tasksRunning = false;
        return ESP_OK;
    }
    
    // 通知任务暂停
    xTaskNotifyGive(audioTaskHandle);
    xTaskNotifyGive(fileTaskHandle);
    
    // 等待任务暂停
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 确认任务已暂停
    if (eTaskGetState(audioTaskHandle) != eSuspended || 
        eTaskGetState(fileTaskHandle) != eSuspended) {
        ESP_LOGW(TAG, "Failed to suspend tasks");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Audio capture stopped");
    return ESP_OK;
}

// 检查音频捕获是否正在运行
bool audio_capture_is_running(void) {
    return tasksRunning && 
           audioTaskHandle != NULL && 
           fileTaskHandle != NULL && 
           eTaskGetState(audioTaskHandle) != eSuspended;
}