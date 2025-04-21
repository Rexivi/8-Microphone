#include "uart_console.h"

#define TAG "console"

// // 音频采样命令处理函数声明
static int start_audio_cmd_handler(int argc, char **argv);
static int stop_audio_cmd_handler(int argc, char **argv);

void start_repl() {
    // REPL配置
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "esp32>";

    // UART配置
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

    // 注册命令
    esp_console_register_help_command();
    register_console_commands();
    // 开始REPL
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}

void register_console_commands() {
    // 只保留音频采样相关的两个命令
    
    // 开启音频采样命令
    const esp_console_cmd_t start_audio_cmd = {
        .command = "startaudio",
        .help = "Start audio sampling and recording to a unique file",
        .hint = NULL,
        .func = &start_audio_cmd_handler,
        .argtable = NULL
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&start_audio_cmd));

    // 停止音频采样命令
    const esp_console_cmd_t stop_audio_cmd = {
        .command = "stopaudio",
        .help = "Stop audio sampling and close the current recording file",
        .hint = NULL,
        .func = &stop_audio_cmd_handler,
        .argtable = NULL
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&stop_audio_cmd));
}

// 开启音频采样命令处理函数
static int start_audio_cmd_handler(int argc, char **argv) {
    esp_err_t ret = audio_capture_start();
    if (ret == ESP_OK) {
        if (audio_capture_is_running()) {
            printf("Audio sampling started successfully. Recording to a new file...\n");
        } else {
            printf("Audio sampling tasks created but not running properly.\n");
            return 1;
        }
    } else {
        printf("Failed to start audio sampling: %s\n", esp_err_to_name(ret));
        return 1;
    }
    return 0;
}

// 停止音频采样命令处理函数
static int stop_audio_cmd_handler(int argc, char **argv) {
    if (!audio_capture_is_running()) {
        printf("Audio sampling is not currently running.\n");
        return 0;
    }
    
    esp_err_t ret = audio_capture_stop();
    if (ret == ESP_OK) {
        printf("Audio sampling stopped successfully. File has been saved.\n");
    } else {
        printf("Failed to stop audio sampling: %s\n", esp_err_to_name(ret));
        return 1;
    }
    return 0;
}