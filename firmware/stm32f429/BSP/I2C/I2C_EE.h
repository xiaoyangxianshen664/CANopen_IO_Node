//#ifndef __I2C_EE_H
//#define __I2C_EE_H

//#include "stm32f4xx.h"

///* ── I2C1: PB6=SCL, PB7=SDA ── */
//#define I2Cx                     I2C1
//#define I2Cx_CLK_ENABLE()        __HAL_RCC_I2C1_CLK_ENABLE()
//#define I2Cx_SCL_PIN             GPIO_PIN_6
//#define I2Cx_SCL_PORT            GPIOB
//#define I2Cx_SCL_AF              GPIO_AF4_I2C1
//#define I2Cx_SDA_PIN             GPIO_PIN_7
//#define I2Cx_SDA_PORT            GPIOB
//#define I2Cx_SDA_AF              GPIO_AF4_I2C1
//#define I2Cx_GPIO_CLK_ENABLE()   __HAL_RCC_GPIOB_CLK_ENABLE()

///* ── AT24C02 ── */
//#define EEPROM_ADDRESS           0xA0   /* 设备地址 */
//#define EEPROM_PAGESIZE          8      /* 每页 8 字节 */
//#define EEPROM_SIZE              256    /* 总共 256 字节 */

//extern I2C_HandleTypeDef hi2c_eeprom;

//void I2C_EE_Init(void);
//uint8_t I2C_EE_ByteWrite(uint8_t addr, uint8_t data);
//uint8_t I2C_EE_ByteRead(uint8_t addr);
//void I2C_EE_BufferWrite(uint8_t *pData, uint8_t addr, uint16_t len);
//void I2C_EE_BufferRead(uint8_t *pData, uint8_t addr, uint16_t len);

//#endif
