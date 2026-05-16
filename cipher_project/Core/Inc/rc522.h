#ifndef RC522_H
#define RC522_H

#include <stdint.h>

#include "stm32f1xx_hal.h"

// RC522 Registers
#define RC522_REG_COMMAND 0x01
#define RC522_REG_COM_IRQ 0x04
#define RC522_REG_ERROR 0x06
#define RC522_REG_FIFO_DATA 0x09
#define RC522_REG_FIFO_LEVEL 0x0A
#define RC522_REG_CONTROL 0x0C
#define RC522_REG_BIT_FRAMING 0x0D
#define RC522_REG_MODE 0x11
#define RC522_REG_TX_CONTROL 0x14
#define RC522_REG_TX_ASK 0x15
#define RC522_REG_CRC_RESULT_H 0x21
#define RC522_REG_CRC_RESULT_L 0x22
#define RC522_REG_T_MODE 0x2A
#define RC522_REG_T_PRESCALER 0x2B
#define RC522_REG_T_RELOAD_H 0x2C
#define RC522_REG_T_RELOAD_L 0x2D

// RC522 Commands
#define RC522_CMD_IDLE 0x00
#define RC522_CMD_TRANSCEIVE 0x0C
#define RC522_CMD_SOFT_RESET 0x0F

// PICC Commands
#define PICC_CMD_REQA 0x26

// SPI Pins (from our CubeMX config)
extern SPI_HandleTypeDef hspi1;
#define RC522_CS_LOW() HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET)
#define RC522_CS_HIGH() HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET)
#define RC522_RST_LOW() HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, GPIO_PIN_RESET)
#define RC522_RST_HIGH() HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, GPIO_PIN_SET)

void RC522_Init(void);
uint8_t RC522_Detect(void);  // returns 1 if card present, 0 if not

#endif /* RC522_H */
