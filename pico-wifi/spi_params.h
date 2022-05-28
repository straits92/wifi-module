#ifndef SPI_PARAMS_H
#define SPI_PARAMS_H

// #include "pico/stdlib.h"

// for SPI0
#define SPI_PORT spi0
#define COMM_SPEED 500000 // 500kHz
#define SCK 2
#define SPI_TX 3 // MOSI
#define SPI_RX 4 // unused for digipot control
#define CS 5

#endif