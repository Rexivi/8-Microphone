#include "AudioCapture.h"

static const char *TAG = "AudioCapture";

// Task handles
static TaskHandle_t audioTaskHandle = NULL;
static TaskHandle_t fileTaskHandle = NULL;

// N-buffer for audio data (replacing double buffer)
#define NUM_BUFFERS 6  // Number of buffers (N) - can be adjusted

typedef struct {
    SemaphoreHandle_t mutex[NUM_BUFFERS];
    uint8_t *buffer[NUM_BUFFERS];
    size_t size;        // Size of each buffer
    int activeBuffer;   // Currently active buffer for writing
    bool bufferReady[NUM_BUFFERS]; // Track which buffers are ready for saving
} AudioMultiBuffer;

static AudioMultiBuffer audioBuffer = {
    .mutex = {NULL},
    .buffer = {NULL},
    .size = AUDIO_BUFFER_SIZE,
    .activeBuffer = 0,
    .bufferReady = {false}
};

// Queue for communication between tasks
static QueueHandle_t dataQueue = NULL;

// File handle
static FILE *audioFile = NULL;
static char currentFilePath[128] = {0}; // Buffer to store current file path

// Task states
static bool tasksRunning = false;

// Function to generate a unique filename for each recording session
static esp_err_t generate_audio_filename(char *file_path, size_t max_len) {
    DIR *dir;
    struct dirent *entry;
    int max_index = 0;
    char pattern[32];
    int file_index;
    
    // Ensure output buffer is valid
    if (file_path == NULL || max_len < 16) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Open the directory
    dir = opendir(AUDIO_FILE_DIR);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory: %s", AUDIO_FILE_DIR);
        // Default to Audio1.bin if directory can't be opened
        snprintf(file_path, max_len, "%s/%s1%s", AUDIO_FILE_DIR, AUDIO_FILE_PREFIX, AUDIO_FILE_EXT);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Create pattern to match (e.g., "Audio")
    snprintf(pattern, sizeof(pattern), "%s", AUDIO_FILE_PREFIX);
    size_t pattern_len = strlen(pattern);
    
    // Scan all files in the directory
    while ((entry = readdir(dir)) != NULL) {
        // Check if file name matches our pattern
        if (strncmp(entry->d_name, pattern, pattern_len) == 0) {
            // Try to extract the index number after the prefix
            if (sscanf(entry->d_name + pattern_len, "%d", &file_index) == 1) {
                // If this index is higher than our current max, update max
                if (file_index > max_index) {
                    max_index = file_index;
                }
            }
        }
    }
    
    // Close the directory
    closedir(dir);
    
    // Create new filename with index one higher than max found
    snprintf(file_path, max_len, "%s/%s%d%s", 
             AUDIO_FILE_DIR, AUDIO_FILE_PREFIX, max_index + 1, AUDIO_FILE_EXT);
    
    ESP_LOGI(TAG, "Generated filename: %s", file_path);
    return ESP_OK;
}

// Audio data capture task
static void audio_capture_task(void *pvParameters) {
    size_t bytes_read;
    esp_err_t result;
    size_t writePos = 0;  // Local write position for current buffer
    
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
        
        // Try to find a free buffer
        bool bufferFound = false;
        int startBuffer = audioBuffer.activeBuffer;  // Remember where we started searching
        
        // Try all buffers in a round-robin fashion starting from current activeBuffer
        for (int i = 0; i < NUM_BUFFERS; i++) {
            int bufferIndex = (startBuffer + i) % NUM_BUFFERS;
            
            // Try to lock the buffer for writing (non-blocking)
            if (xSemaphoreTake(audioBuffer.mutex[bufferIndex], 0) == pdTRUE) {
                // Check if buffer is not marked as ready (not waiting to be saved)
                if (!audioBuffer.bufferReady[bufferIndex]) {
                    // We found a usable buffer
                    audioBuffer.activeBuffer = bufferIndex;
                    bufferFound = true;
                    
                    // Calculate remaining space in this buffer
                    uint8_t* bufPtr = audioBuffer.buffer[bufferIndex] + writePos;
                    size_t bytesToRead = audioBuffer.size - writePos;
                    
                    // Read data from I2S directly into buffer
                    result = i2s_channel_read(rx_chan, bufPtr, bytesToRead, &bytes_read, portMAX_DELAY);
                    
                    // Check if read was successful and data was received
                    if (result == ESP_OK && bytes_read > 0) {
                        // Update write position
                        writePos += bytes_read;
                        
                        // Check if buffer is full
                        if (writePos >= audioBuffer.size) {
                            // Reset write position for next buffer
                            writePos = 0;
                            
                            // Mark this buffer as ready
                            audioBuffer.bufferReady[bufferIndex] = true;
                            
                            // Send buffer index to file task
                            xQueueSend(dataQueue, &bufferIndex, portMAX_DELAY);
                        }
                    } else if (result != ESP_OK) {
                        ESP_LOGW(TAG, "I2S read error: %s", esp_err_to_name(result));
                    }
                    
                    // Release buffer mutex
                    xSemaphoreGive(audioBuffer.mutex[bufferIndex]);
                    break;  // Exit the for loop since we processed data
                } else {
                    // Buffer is marked as ready but we got the mutex - release it and try another
                    xSemaphoreGive(audioBuffer.mutex[bufferIndex]);
                }
            }
            // If we couldn't get the mutex or the buffer was ready, try the next one
        }
        
        // If no buffer was found, yield briefly before trying again
        if (!bufferFound) {
            taskYIELD();  // Give other tasks a chance to run
        }
    }
}

// File save task
static void file_save_task(void *pvParameters) {
    ESP_LOGI(TAG, "File save task started");
    
    // Generate unique filename
    memset(currentFilePath, 0, sizeof(currentFilePath));
    if (generate_audio_filename(currentFilePath, sizeof(currentFilePath)) != ESP_OK) {
        ESP_LOGW(TAG, "Error generating filename, using fallback");
    }
    
    // Create file
    audioFile = fopen(currentFilePath, "wb");
    setvbuf(audioFile, NULL, _IOFBF, 8192);  // Use fully buffered mode
    if (audioFile == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", currentFilePath);
        vTaskDelete(NULL);
        fileTaskHandle = NULL;
        return;
    }
    
    ESP_LOGI(TAG, "File opened: %s", currentFilePath);
    
    while (1) {
        // Check if task should be suspended
        if (ulTaskNotifyTake(pdTRUE, 0)) {
            // Flush any pending buffers
            for (int i = 0; i < NUM_BUFFERS; i++) {
                if (audioBuffer.bufferReady[i]) {
                    if (xSemaphoreTake(audioBuffer.mutex[i], portMAX_DELAY) == pdTRUE) {
                        // Write buffer to file
                        size_t written = fwrite(audioBuffer.buffer[i], 1, audioBuffer.size, audioFile);
                        if (written != audioBuffer.size) {
                            ESP_LOGW(TAG, "Failed to write all data to file: %d/%d", written, audioBuffer.size);
                        }
                        
                        // Mark buffer as available
                        audioBuffer.bufferReady[i] = false;
                        xSemaphoreGive(audioBuffer.mutex[i]);
                    }
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
            
            // Generate new unique filename
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
        
        // Receive buffer index from queue
        uint8_t bufferIndex;
        if (xQueueReceive(dataQueue, &bufferIndex, portMAX_DELAY) == pdTRUE) {
            if (bufferIndex < NUM_BUFFERS && audioBuffer.bufferReady[bufferIndex]) {
                if (xSemaphoreTake(audioBuffer.mutex[bufferIndex], portMAX_DELAY) == pdTRUE) {
                    // Write buffer to file
                    size_t written = fwrite(audioBuffer.buffer[bufferIndex], 1, audioBuffer.size, audioFile);
                    if (written != audioBuffer.size) {
                        ESP_LOGW(TAG, "Failed to write all data to file: %d/%d", written, audioBuffer.size);
                    }
                    
                    // Mark buffer as available again
                    audioBuffer.bufferReady[bufferIndex] = false;
                    xSemaphoreGive(audioBuffer.mutex[bufferIndex]);
                }
            } else {
                ESP_LOGW(TAG, "Received invalid buffer index: %d", bufferIndex);
            }
        }
    }
}

// Initialize audio capture system
static esp_err_t audio_capture_init(void) {
    // Create data queue - increased size to accommodate more buffers
    dataQueue = xQueueCreate(NUM_BUFFERS * 2, sizeof(uint8_t));
    if (dataQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create data queue");
        return ESP_FAIL;
    }
    
    // Allocate DMA-capable memory for buffers
    for (int i = 0; i < NUM_BUFFERS; i++) {
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
        
        // Initialize buffer state
        audioBuffer.bufferReady[i] = false;
    }
    
    // Reset buffer state
    audioBuffer.activeBuffer = 0;
    
    return ESP_OK;
}

// Release all resources
static void audio_capture_deinit(void) {
    // Free buffer resources
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
    
    // Check if tasks are already running
    if (tasksRunning) {
        // Check if tasks are suspended
        if (audioTaskHandle != NULL && eTaskGetState(audioTaskHandle) == eSuspended) {
            // Resume tasks
            vTaskResume(audioTaskHandle);
            vTaskResume(fileTaskHandle);
            ESP_LOGI(TAG, "Audio capture tasks resumed");
            return ESP_OK;
        } else {
            // Tasks are already running
            ESP_LOGW(TAG, "Audio capture already running");
            return ESP_OK;
        }
    }
    
    // Check for anomalous state where task handles exist but not marked as running
    if (audioTaskHandle != NULL || fileTaskHandle != NULL) {
        ESP_LOGW(TAG, "Task handles exist but not marked as running - cleaning up");
        // Delete any existing tasks
        if (audioTaskHandle != NULL) {
            vTaskDelete(audioTaskHandle);
            audioTaskHandle = NULL;
        }
        if (fileTaskHandle != NULL) {
            vTaskDelete(fileTaskHandle);
            fileTaskHandle = NULL;
        }
    }
    
    // Initialize resources only if not already done
    if (!resources_initialized) {
        // Initialize resources
        ret = audio_capture_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize audio capture: %s", esp_err_to_name(ret));
            audio_capture_deinit();
            return ret;
        }
        
        // Create audio capture task
        BaseType_t xReturned = xTaskCreatePinnedToCore(
            audio_capture_task,
            "audio_capture_task",
            AUDIO_TASK_STACK_SIZE,
            NULL,
            AUDIO_TASK_PRIORITY,
            &audioTaskHandle,
            0  // Core 0
        );
        
        if (xReturned != pdPASS) {
            ESP_LOGE(TAG, "Failed to create audio capture task");
            audio_capture_deinit();
            resources_initialized = false;
            return ESP_ERR_NO_MEM;
        }
        
        // Create file save task
        xReturned = xTaskCreatePinnedToCore(
            file_save_task,
            "file_save_task",
            FILE_TASK_STACK_SIZE,
            NULL,
            FILE_TASK_PRIORITY,
            &fileTaskHandle,
            1  // Core 1
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
        // Mark tasks as running
        tasksRunning = true;
        ESP_LOGI(TAG, "Audio capture started");
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

// Check if audio capture is running
bool audio_capture_is_running(void) {
    return tasksRunning && 
           audioTaskHandle != NULL && 
           fileTaskHandle != NULL && 
           eTaskGetState(audioTaskHandle) != eSuspended;
}