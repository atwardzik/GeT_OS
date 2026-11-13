//
// Created by Artur Twardzik on 13/11/2026.
//
///
// This file provides pin configuration for connected devices.
// It may contain only #defines as it is also read by assembly files.
///

#ifndef OS_PIN_CONFIG_H
#define OS_PIN_CONFIG_H

/*
 * UART
 */
#define UART_TX          0
#define UART_RX          1

/*
 * SD card
 */

#define SD_SPI_TX       15
#define SD_SPI_RX       12
#define SD_SPI_CLK      14
#define SD_SPI_CS       13
#define SD_SPI_BLOCK     1


/*
 * PS2 Keyboard
 */

#define PS2_CLK         26
#define PS2_DAT         27


/*
 * VGA
 */

#define VGA_HSYNC        9
#define VGA_VSYNC       10
#define VGA_RED0         3


/*
 * Ethernet Chip
 */

#define ETH_SPI_TX       15
#define ETH_SPI_RX       16
#define ETH_SPI_CLK      18
#define ETH_SPI_CS       17
#define ETH_SPI_BLOCK     0
#define ETH_INT          21


/*
 * User LEDs
 */

#define LED1            11
#define LED2            28

/*
 * User buttons
 */

#define BTN             22


#endif //OS_PIN_CONFIG_H
