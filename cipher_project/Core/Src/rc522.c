#include "rc522.h"

#include "spi.h"

static void RC522_Write(uint8_t reg, uint8_t val) {
  uint8_t buf[2] = {(reg << 1) & 0x7E, val};
  RC522_CS_LOW();
  HAL_SPI_Transmit(&hspi1, buf, 2, 10);
  RC522_CS_HIGH();
}

static uint8_t RC522_Read(uint8_t reg) {
  uint8_t addr = ((reg << 1) & 0x7E) | 0x80;
  uint8_t val = 0;
  RC522_CS_LOW();
  HAL_SPI_Transmit(&hspi1, &addr, 1, 10);
  HAL_SPI_Receive(&hspi1, &val, 1, 10);
  RC522_CS_HIGH();
  return val;
}

static void RC522_Reset(void) {
  RC522_Write(RC522_REG_COMMAND, RC522_CMD_SOFT_RESET);
  HAL_Delay(50);
}

void RC522_Init(void) {
  RC522_RST_HIGH();
  HAL_Delay(10);
  RC522_Reset();

  RC522_Write(RC522_REG_T_MODE, 0x8D);
  RC522_Write(RC522_REG_T_PRESCALER, 0x3E);
  RC522_Write(RC522_REG_T_RELOAD_H, 0x00);
  RC522_Write(RC522_REG_T_RELOAD_L, 0x1E);
  RC522_Write(RC522_REG_TX_ASK, 0x40);
  RC522_Write(RC522_REG_MODE, 0x3D);

  // Enable antenna
  uint8_t val = RC522_Read(RC522_REG_TX_CONTROL);
  if ((val & 0x03) != 0x03) RC522_Write(RC522_REG_TX_CONTROL, val | 0x03);
}

uint8_t RC522_Detect(void) {
  // Send REQA command and check for any response (card present)
  RC522_Write(RC522_REG_FIFO_LEVEL, 0x80);  // flush FIFO
  RC522_Write(RC522_REG_COMMAND, RC522_CMD_IDLE);
  RC522_Write(RC522_REG_COM_IRQ, 0x7F);
  RC522_Write(RC522_REG_BIT_FRAMING, 0x07);

  uint8_t cmd = PICC_CMD_REQA;
  RC522_Write(RC522_REG_FIFO_DATA, cmd);
  RC522_Write(RC522_REG_COMMAND, RC522_CMD_TRANSCEIVE);
  RC522_Write(RC522_REG_BIT_FRAMING, 0x87);  // StartSend=1

  uint32_t start = HAL_GetTick();
  uint8_t irq = 0;
  while (HAL_GetTick() - start < 25) {
    irq = RC522_Read(RC522_REG_COM_IRQ);
    if (irq & 0x31) break;  // RxIRq or IdleIRq or TimerIRq
  }

  RC522_Write(RC522_REG_BIT_FRAMING, 0x00);

  uint8_t error = RC522_Read(RC522_REG_ERROR);
  if ((irq & 0x01) || (error & 0x13)) return 0;  // timeout or error

  return (irq & 0x21) ? 1 : 0;  // RxIRq set = card responded
}
