#ifndef ADAU7118_H
#define ADAU7118_H

#include "esp_err.h"
#include "driver/i2c.h"
#include "esp_log.h"

// ADAU7118 I2C地址
#define ADAU7118_I2C_ADDR        0x17  // ADAU7118的7位I2C地址

/* 设备ID默认值 */
#define Defult_VENDOR_ID		0x41
#define Defult_DEVICE_ID1		0x71	
#define Defult_DEVICE_ID2		0x18
#define Defult_REVISION_ID	    0x00

// ADAU7118寄存器地址
#define ADAU7118_REG_VENDOR_ID       0x00
#define ADAU7118_REG_DEVICE_ID1      0x01
#define ADAU7118_REG_DEVICE_ID2      0x02
#define ADAU7118_REG_REVISION_ID     0x03
#define ADAU7118_REG_ENABLES         0x04
#define ADAU7118_REG_DEC_RATIO_CLK_MAP 0x05
#define ADAU7118_REG_HPF_CONTROL     0x06
#define ADAU7118_REG_SPT_CTRL1       0x07
#define ADAU7118_REG_SPT_CTRL2       0x08
/* 寄存器地址 */
#define ADAU7118_REG_VENDOR_ID		0x00
#define ADAU7118_REG_DEVICE_ID1		0x01
#define ADAU7118_REG_DEVICE_ID2		0x02
#define ADAU7118_REG_REVISION_ID	0x03
#define ADAU7118_REG_ENABLES		0x04
#define ADAU7118_REG_DEC_RATIO_CLK_MAP	0x05
#define ADAU7118_REG_HPF_CONTROL	0x06
#define ADAU7118_REG_SPT_CTRL1		0x07
#define ADAU7118_REG_SPT_CTRL2		0x08
#define ADAU7118_REG_SPT_CX(num)	(0x09 + (num))
#define ADAU7118_REG_DRIVE_STRENGTH	0x11
#define ADAU7118_REG_RESET		0x12

/* 设备ID默认值 */
#define Defult_VENDOR_ID		0x41
#define Defult_DEVICE_ID1		0x71	
#define Defult_DEVICE_ID2		0x18
#define Defult_REVISION_ID	0x00


/* ENABLES rigister config */
#define PDM_CLK1_ENABLE		0x20
#define PDM_CLK0_ENABLE 	0x10
#define CHAN_67_ENABLE 		0x08
#define CHAN_67_DISABLE 	0x00
#define CHAN_45_ENABLE 		0x04
#define CHAN_45_DISABLE 	0x00
#define CHAN_23_ENABLE 		0x02
#define CHAN_23_DISABLE 	0x00
#define CHAN_01_ENABLE 		0x01
#define CHAN_01_DISABLE 	0x00

/* DEC_RATIO_CLK_MAP rigister config */
#define PDM_DAT3_CLK0			0x00
#define PDM_DAT3_CLK1			0x80
#define PDM_DAT2_CLK0			0x00
#define PDM_DAT2_CLK1			0x40
#define PDM_DAT1_CLK0 		0x00
#define PDM_DAT1_CLK1 		0x20
#define PDM_DAT0_CLK0 		0x00
#define PDM_DAT0_CLK1 		0x10
#define DEC_RATIO_64 			0x00
#define DEC_RATIO_32 			0x01
#define DEC_RATIO_16 			0x02


/* HPF_CONTROL rigister config */
#define  HPF_Enable 		0x01
#define  HPF_Disable 		0x00
#define	 Defult_Cutoff_freq 0xD0

/* SPT_CTRL1 rigister config */
#define TRI_STATE_Enable		 0x40 
#define TRI_STATE_Disable		 0x00 
#define SPT_SLOT_WIDTH_32		 0x00    
#define SPT_SLOT_WIDTH_16		 0x10  
#define SPT_SLOT_WIDTH_24		 0x20
#define SPT_DATA_Left				 0x02
#define SPT_DATA_I2S_delay1  0x00
#define SPT_DATA_I2S_delay8  0x04
#define SPT_DATA_I2S_delay12 0x06
#define SPT_DATA_I2S_delay16 0x08
#define SPT_SAI_Stereo 			 0x00
#define SPT_SAI_TDM 			 	 0x01

/* SPT_CTRL2 rigister config */
#define LRCLK_POL_Normal		 0x00 
#define LRCLK_POL_Invert		 0x02 
#define BCLK_POL_Rising		 	 0x00    
#define BCLK_POL_Falling		 0x01  


// 使能控制位定义
#define CHAN_01_ENABLE               0x01
#define PDM_CLK0_ENABLE              0x10
// 根据需要添加其他的控制位定义

#define I2C_MASTER_SCL_IO    9    // 在这里设置I2C SCL引脚号
#define I2C_MASTER_SDA_IO    10    // 在这里设置I2C SDA引脚号
#define I2C_MASTER_NUM       I2C_NUM_0
#define I2C_MASTER_FREQ_HZ   10000  //35kHz 
#define I2C_MASTER_TX_BUF_DISABLE   0
#define I2C_MASTER_RX_BUF_DISABLE   0

// 函数声明
// 初始化ADAU7118，需要提供SCL和SDA引脚编号
esp_err_t Init_ADAU7118();
// 写入寄存器
esp_err_t adau7118_write_reg(uint8_t reg_addr, uint8_t reg_data);
// 读取寄存器
esp_err_t adau7118_read_reg(uint8_t reg_addr, uint8_t *reg_data);
// 释放资源
void adau7118_deinit(void);
#endif // ADAU7118_H
