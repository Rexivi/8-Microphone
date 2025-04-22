#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "AudioCapture.h"

#include "SD_MMC.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_random.h" 

#include "ADAU7118.h" 
#include "hardwareInit.h" 
#include "uart_console.h"

static const char *TAG = "main";

void app_main(void) {
    esp_err_t ret;
    
    // 初始化 NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_LOGI(TAG, "系统初始化完成");
    // 初始化硬件
    SD_Init();
    ret = Init_ADAU7118();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADAU7118初始化失败: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = tdm_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TDM初始化失败: %s", esp_err_to_name(ret));
        return;
    }
    
    // 延迟一秒，等待系统稳定
    vTaskDelay(pdMS_TO_TICKS(1000));
    // 开启音频采集
    ret = audio_capture_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "音频采集模块初始化失败: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "开始音频捕获测试");
    ESP_LOGI(TAG, "将采集一分钟的音频数据...");
    
    // 使用延时函数代替定时器
    // 主循环 - 每10秒输出一次状态信息，总计60秒
    int remaining_seconds = 60;
    
    while (remaining_seconds > 0 && audio_capture_is_running()) {
        // 选择较短的延时间隔，提高响应性
        int delay_interval = (remaining_seconds >= 10) ? 10 : remaining_seconds;
        
        // 等待指定的时间
        vTaskDelay(pdMS_TO_TICKS(delay_interval * 1000));
        
        // 更新剩余时间
        remaining_seconds -= delay_interval;
        
        // 输出状态信息
        ESP_LOGI(TAG, "音频捕获已运行 %d 秒，还剩 %d 秒...", 
                 60 - remaining_seconds, remaining_seconds);
    }
    
    // 时间到，停止音频捕获
    ESP_LOGI(TAG, "一分钟时间到，停止音频捕获");
    
    // 停止音频捕获
    ret = audio_capture_stop();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "音频捕获已成功停止");
    } else {
        ESP_LOGE(TAG, "停止音频捕获失败: %s", esp_err_to_name(ret));
    }
    
    ESP_LOGI(TAG, "音频捕获测试完成");
    
    // 继续保持主任务运行
    ESP_LOGI(TAG, "程序继续运行中...");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

// void app_main(void) {
//     esp_err_t ret;
    
//     // 初始化 NVS
//     ret = nvs_flash_init();
//     if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
//         ESP_ERROR_CHECK(nvs_flash_erase());
//         ret = nvs_flash_init();
//     }
//     ESP_ERROR_CHECK(ret);
    
//     ESP_LOGI(TAG, "系统初始化完成");
//     // 初始化硬件
//     SD_Init();
//     ret = Init_ADAU7118();
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "ADAU7118初始化失败: %s", esp_err_to_name(ret));
//         return;
//     }
    
//     ret = tdm_init();
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "TDM初始化失败: %s", esp_err_to_name(ret));
//         return;
//     }
    
//     start_repl();  // 启动REPL
//     while (1) {
//         vTaskDelay(pdMS_TO_TICKS(10000));
//     }
// }
