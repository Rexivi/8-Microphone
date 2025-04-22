#include "ADAU7118.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "ADAU7118";
static i2c_master_bus_handle_t i2c_bus_handle = NULL;
static i2c_master_dev_handle_t adau7118_dev_handle = NULL;

// 初始化I2C总线和设备句柄
esp_err_t adau7118_init_i2c() {
    esp_err_t ret;
    
    // 配置I2C总线
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_MASTER_NUM,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    
    // 创建I2C总线
    ret = i2c_new_master_bus(&bus_config, &i2c_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C总线创建失败: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 配置ADAU7118设备
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ADAU7118_I2C_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ, // 10kHz
    };
    
    // 添加设备到总线
    ret = i2c_master_bus_add_device(i2c_bus_handle, &dev_config, &adau7118_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "添加ADAU7118设备失败: %s", esp_err_to_name(ret));
        i2c_del_master_bus(i2c_bus_handle);
        i2c_bus_handle = NULL;
        return ret;
    }
    
    return ESP_OK;
}

// 写寄存器函数
esp_err_t adau7118_write_reg(uint8_t reg_addr, uint8_t reg_data) {
    if (adau7118_dev_handle == NULL) {
        ESP_LOGE(TAG, "ADAU7118设备未初始化");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t write_buf[2] = {reg_addr, reg_data};
    return i2c_master_transmit(adau7118_dev_handle, write_buf, sizeof(write_buf), -1);
}

// 读寄存器函数
esp_err_t adau7118_read_reg(uint8_t reg_addr, uint8_t *reg_data) {
    if (adau7118_dev_handle == NULL || reg_data == NULL) {
        ESP_LOGE(TAG, "无效参数或ADAU7118设备未初始化");
        return ESP_ERR_INVALID_ARG;
    }

    // 先写寄存器地址
    esp_err_t ret = i2c_master_transmit(adau7118_dev_handle, &reg_addr, 1, -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "写入寄存器地址失败: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 然后读取数据
    return i2c_master_receive(adau7118_dev_handle, reg_data, 1, -1);
}

// 初始化ADAU7118
esp_err_t Init_ADAU7118() {
    esp_err_t ret;
    
    // 初始化I2C通信
    ret = adau7118_init_i2c();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C初始化失败");
        return ret;
    }
    
    vTaskDelay(10 / portTICK_PERIOD_MS);
    uint8_t device_ID = 0;

    // 读取并检查设备ID
    ESP_LOGI(TAG, "校验设备ID...");
    int retry_count = 0;
    const int max_retries = 5;
    
    while (device_ID != Defult_VENDOR_ID && retry_count < max_retries) {
        if (adau7118_read_reg(ADAU7118_REG_VENDOR_ID, &device_ID) != ESP_OK) {
            ESP_LOGW(TAG, "读取设备ID失败, 重试中 (%d/%d)", retry_count + 1, max_retries);
            vTaskDelay(10 / portTICK_PERIOD_MS);
            retry_count++;
            continue;
        }
        
        if (device_ID != Defult_VENDOR_ID) {
            ESP_LOGW(TAG, "设备ID不匹配: 预期 0x%02x, 实际 0x%02x", Defult_VENDOR_ID, device_ID);
            vTaskDelay(10 / portTICK_PERIOD_MS);
            retry_count++;
        }
    }
    
    if (device_ID != Defult_VENDOR_ID) {
        ESP_LOGE(TAG, "设备ID校验失败");
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "设备校验正确");

    // 2. 软复位
    uint8_t read_data; // 用于验证的读取数据
    
    ret = adau7118_write_reg(0x12, 0x01); // 软复位，不包括寄存器设置
    if (ret != ESP_OK) return ret;
   
    // 3. 启用所有通道和时钟
    ret = adau7118_write_reg(0x04, 0x3F); // 启用所有通道和PDM时钟输出
    if (ret != ESP_OK) return ret;
    vTaskDelay(5 / portTICK_PERIOD_MS);
    ret = adau7118_read_reg(0x04, &read_data);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Reg 0x04 写入: 0x3F, 读回: 0x%02x, %s", 
                 read_data, (read_data == 0x3F) ? "成功" : "失败");
    } else {
        ESP_LOGW(TAG, "Reg 0x04 验证读取失败: %s", esp_err_to_name(ret));
    }
    
    // 4. 设置抽取比率和PDM时钟映射
    ret = adau7118_write_reg(0x05, 0b11000001);
    if (ret != ESP_OK) return ret;
    vTaskDelay(5 / portTICK_PERIOD_MS);
    ret = adau7118_read_reg(0x05, &read_data);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Reg 0x05 写入: 0xC0, 读回: 0x%02x, %s", 
                 read_data, (read_data == 0xC0) ? "成功" : "失败");
    } else {
        ESP_LOGW(TAG, "Reg 0x05 验证读取失败: %s", esp_err_to_name(ret));
    }
    
    // 5. 配置高通滤波器
    ret = adau7118_write_reg(0x06, 0xD0);
    if (ret != ESP_OK) return ret;
    vTaskDelay(5 / portTICK_PERIOD_MS);
    ret = adau7118_read_reg(0x06, &read_data);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Reg 0x06 写入: 0xD0, 读回: 0x%02x, %s", 
                 read_data, (read_data == 0xD0) ? "成功" : "失败");
    } else {
        ESP_LOGW(TAG, "Reg 0x06 验证读取失败: %s", esp_err_to_name(ret));
    }
    
    // 6. 配置串行音频接口
    ret = adau7118_write_reg(0x07, 0b01010011);
    if (ret != ESP_OK) return ret;
    vTaskDelay(5 / portTICK_PERIOD_MS);
    ret = adau7118_read_reg(0x07, &read_data);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Reg 0x07 写入: 0x53, 读回: 0x%02x, %s", 
                 read_data, (read_data == 0x53) ? "成功" : "失败");
    } else {
        ESP_LOGW(TAG, "Reg 0x07 验证读取失败: %s", esp_err_to_name(ret));
    }
    
    // 7. 配置时钟极性
    ret = adau7118_write_reg(0x08, 0x00);
    if (ret != ESP_OK) return ret;
    vTaskDelay(5 / portTICK_PERIOD_MS);
    ret = adau7118_read_reg(0x08, &read_data);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Reg 0x08 写入: 0x00, 读回: 0x%02x, %s", 
                 read_data, (read_data == 0x00) ? "成功" : "失败");
    } else {
        ESP_LOGW(TAG, "Reg 0x08 验证读取失败: %s", esp_err_to_name(ret));
    }
    
    // 9. 设置输出引脚驱动强度
    ret = adau7118_write_reg(0x11, 0x2A);
    if (ret != ESP_OK) return ret;
    vTaskDelay(5 / portTICK_PERIOD_MS);
    ret = adau7118_read_reg(0x11, &read_data);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Reg 0x11 写入: 0x2A, 读回: 0x%02x, %s", 
                 read_data, (read_data == 0x2A) ? "成功" : "失败");
    } else {
        ESP_LOGW(TAG, "Reg 0x11 验证读取失败: %s", esp_err_to_name(ret));
    }
    
    ESP_LOGI(TAG, "ADAU7118 initialized successfully");
    return ESP_OK;
}

// 释放I2C资源
void adau7118_deinit(void) {
    if (adau7118_dev_handle) {
        i2c_master_bus_rm_device(adau7118_dev_handle);
        adau7118_dev_handle = NULL;
    }
    
    if (i2c_bus_handle) {
        i2c_del_master_bus(i2c_bus_handle);
        i2c_bus_handle = NULL;
    }
}