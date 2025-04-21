#include "hardwareInit.h"
#include <string.h>
static const char *TAG = "HardwareInit";

i2s_chan_handle_t rx_chan;  // 没有static关键字
esp_err_t tdm_init(void)
{
    esp_err_t ret = ESP_OK;
    
    ESP_LOGI(TAG, "Initializing TDM interface for ADAU7118...");
    
    // 步骤1: 配置I2S通道
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    // 由于我们只需要接收数据，所以只分配RX通道
    ret = i2s_new_channel(&chan_cfg, NULL, &rx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to allocate I2S channel: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 步骤2: 配置TDM模式
    i2s_tdm_config_t tdm_cfg = {
        // 时钟配置
        .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(TDM_SAMPLE_RATE),
        
        // 槽位配置: 8个通道, 16位宽度, MSB对齐
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,    // 每个通道16位
            I2S_SLOT_MODE_STEREO,        // 立体声基础模式
            // 启用所有8个槽位
            I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3 |
            I2S_TDM_SLOT4 | I2S_TDM_SLOT5 | I2S_TDM_SLOT6 | I2S_TDM_SLOT7
        ),
        
        // GPIO配置
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,         // 主时钟 (如需使用) 
            .bclk = TDM_BCLK_IO,         // 位时钟
            .ws = TDM_WS_IO,             // 帧同步/字选择
            .dout = I2S_GPIO_UNUSED,     // 我们不需要输出
            .din = TDM_DIN_IO,           // 数据输入
            .invert_flags = {
                .mclk_inv = false,       // 不反转主时钟
                .bclk_inv = false,       // 不反转位时钟
                .ws_inv = false,         // 不反转帧同步信号
            },
        },
    };
    
    // 多通道TDM模式需要较高的MCLK倍频来确保BCLK分频器足够大
    tdm_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_512;
    
    // 初始化TDM模式
    ret = i2s_channel_init_tdm_mode(rx_chan, &tdm_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2S TDM mode: %s", esp_err_to_name(ret));
        i2s_del_channel(rx_chan);
        return ret;
    }
    
    // 步骤3: 启用通道
    ret = i2s_channel_enable(rx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S channel: %s", esp_err_to_name(ret));
        i2s_del_channel(rx_chan);
        return ret;
    }
    
    ESP_LOGI(TAG, "TDM interface initialized successfully");
    ESP_LOGI(TAG, "Sample rate: %d Hz, 8 channels, 16-bit", TDM_SAMPLE_RATE);
    
    return ret;
}



// 释放TDM资源函数
void tdm_deinit(void)
{
    if (rx_chan) {
        i2s_channel_disable(rx_chan);
        i2s_del_channel(rx_chan);
    }
}








