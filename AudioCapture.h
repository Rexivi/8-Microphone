#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2s_tdm.h"
#include "driver/gpio.h"
#include "SD_MMC.h"
#include "ADAU7118.h"
#include "hardwareInit.h"

// Buffer and task settings
#define AUDIO_BUFFER_SIZE (96*1024)     // Size of each audio buffer
#define AUDIO_TASK_STACK_SIZE (8192) // Stack size for audio capture task
#define FILE_TASK_STACK_SIZE (8192)  // Stack size for file saving task
#define AUDIO_TASK_PRIORITY (5)      // Priority of audio capture task
#define FILE_TASK_PRIORITY (4)       // Priority of file saving task

// File settings
#define AUDIO_FILE_PATH "/sdcard/audio.bin"

// Double buffer structure for audio data
typedef struct {
    SemaphoreHandle_t mutex[2];      // Mutexes for buffer access
    uint8_t *buffer[2];              // Two buffers for double buffering
    size_t size;                     // Size of each buffer
    int activeBuffer;                // Currently active buffer (0 or 1)
    int readyBuffer;                 // Buffer ready for processing (-1 if none)
    size_t writePos;                 // Current write position in active buffer
} AudioDoubleBuffer;

// Control functions
esp_err_t audio_capture_start(void);
esp_err_t audio_capture_stop(void);
bool audio_capture_is_running(void);

#endif // AUDIO_CAPTURE_H