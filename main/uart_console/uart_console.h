#ifndef UART_CONSOLE_H
#define UART_CONSOLE_H

#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include "AudioCapture.h"


// 函数声明
void start_repl(void);
void register_console_commands(void);



#endif /* UART_CONSOLE_H */