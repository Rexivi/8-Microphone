#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include <dirent.h>    // For directory operations
#include <sys/stat.h>  // For file status checks
#include <errno.h>

// Configuration constants
#define AUDIO_BUFFER_SIZE      (32*1024)  // 32KB per buffer - can be adjusted
#define AUDIO_TASK_STACK_SIZE  (8*1024)   // Stack size for audio task
#define FILE_TASK_STACK_SIZE   (8*1024)   // Stack size for file task
#define AUDIO_TASK_PRIORITY    10         // Audio task priority
#define FILE_TASK_PRIORITY     5          // File task priority
#define AUDIO_FILE_DIR          "/sdcard"         // Directory for audio files
#define AUDIO_FILE_PREFIX      "AUDIO"           // Prefix for audio files
#define AUDIO_FILE_EXT         ".bin"            // File extension

// I2S RX channel - should be defined elsewhere
extern i2s_chan_handle_t rx_chan;

// Initialize and control audio capture
esp_err_t audio_capture_start(void);
esp_err_t audio_capture_stop(void);
bool audio_capture_is_running(void);

#endif /* AUDIO_CAPTURE_H */