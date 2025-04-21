#ifndef HARDWAREINIT_H
#define HARDWAREINIT_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2s_tdm.h"
#include "esp_console.h"

// 串口控制台相关宏
#define CONSOLE_UART_NUM UART_NUM_0



// TDM/I2S引脚定义 
#define TDM_WS_IO           13                          // WS引脚
#define TDM_BCLK_IO         12                          // BCK引脚
#define TDM_DIN_IO          11                          // DATA引脚
#define TDM_MCLK_IO         4  // 主时钟引脚（如需要）
#define TDM_MASTER_NUM      I2S_NUM_0                   // I2S编号

// TDM配置常量
#define TDM_SAMPLE_RATE  96000         // 采样率96kHz
#define TDM_CHANNELS     8             // 8通道
#define TDM_BIT_WIDTH    16            // 16位位宽
#define TDM_BUFFER_SIZE   2048            // 接收缓冲区大小



extern i2s_chan_handle_t rx_chan;
void i2c_master_init(void);
esp_err_t tdm_init(void);


#endif 