#ifndef UART_PARAMS_H
#define UART_PARAMS_H

// #include "pico/stdlib.h"

#define UART_ID uart0
#define PARITY UART_PARITY_NONE
#define UART_TX_PIN 0
#define UART_RX_PIN 1

const int baud = 115200;
const int data = 8;
const int stop = 1;

#endif