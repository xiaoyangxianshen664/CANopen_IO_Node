//#include "./I2C/I2C_EE.h"

//I2C_HandleTypeDef hi2c_eeprom;  /* I2C1 句柄 */

///*
// * ────────────────────────────────────────────────
// *  I2C1 初始化 + GPIO
// * ────────────────────────────────────────────────
// */
//void I2C_EE_Init(void)
//{
//    /* ① 开时钟 */
//    I2Cx_GPIO_CLK_ENABLE();
//    I2Cx_CLK_ENABLE();

//    /* ② GPIO：I2C 必须用开漏模式 */
//    GPIO_InitTypeDef g = {0};
//    g.Mode      = GPIO_MODE_AF_OD;      /* 开漏复用 */
//    g.Pull      = GPIO_NOPULL;          /* 不加上下拉，靠外部电阻 */
//    g.Speed     = GPIO_SPEED_FAST;
//    g.Alternate = I2Cx_SCL_AF;

//    g.Pin = I2Cx_SCL_PIN;
//    HAL_GPIO_Init(I2Cx_SCL_PORT, &g);

//    g.Pin = I2Cx_SDA_PIN;
//    HAL_GPIO_Init(I2Cx_SDA_PORT, &g);

//    /* ③ I2C 句柄配置 */
//    hi2c_eeprom.Instance             = I2Cx;
//    hi2c_eeprom.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;   /* 7 位地址 */
//    hi2c_eeprom.Init.ClockSpeed      = 400000;                     /* 400KHz */
//    hi2c_eeprom.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
//    hi2c_eeprom.Init.DutyCycle       = I2C_DUTYCYCLE_2;
//    hi2c_eeprom.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
//    hi2c_eeprom.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;     /* 允许时钟拉伸 */
//    hi2c_eeprom.Init.OwnAddress1     = 0x0A;
//    hi2c_eeprom.Init.OwnAddress2     = 0;
//    HAL_I2C_Init(&hi2c_eeprom);
//}

///*
// * ────────────────────────────────────────────────
// *  写 1 字节到 EEPROM 指定地址（阻塞）
// * ────────────────────────────────────────────────
// */
//uint8_t I2C_EE_ByteWrite(uint8_t addr, uint8_t data)
//{
//    if (HAL_I2C_Mem_Write(&hi2c_eeprom,
//            EEPROM_ADDRESS,          /* 设备地址 */
//            addr,                    /* 寄存器地址 */
//            I2C_MEMADD_SIZE_8BIT,    /* 地址占 1 字节 */
//            &data,                   /* 数据 */
//            1,                       /* 1 字节 */
//            100) != HAL_OK)          /* 超时 100ms */
//    {
//        return 0; /* 失败 */
//    }

//    /* AT24C02 写入需要时间，等设备重新就绪 */
//    HAL_Delay(5);
//    return 1; /* 成功 */
//}

///*
// * ────────────────────────────────────────────────
// *  从 EEPROM 指定地址读 1 字节（阻塞）
// * ────────────────────────────────────────────────
// */
//uint8_t I2C_EE_ByteRead(uint8_t addr)
//{
//    uint8_t data = 0;

//    HAL_I2C_Mem_Read(&hi2c_eeprom,
//            EEPROM_ADDRESS,
//            addr,
//            I2C_MEMADD_SIZE_8BIT,
//            &data,
//            1,
//            1000);

//    return data;
//}

///*
// * ────────────────────────────────────────────────
// *  写 N 字节（简易版，不处理跨页）
// * ────────────────────────────────────────────────
// */
//void I2C_EE_BufferWrite(uint8_t *pData, uint8_t addr, uint16_t len)
//{
//    HAL_I2C_Mem_Write(&hi2c_eeprom,
//            EEPROM_ADDRESS,
//            addr,
//            I2C_MEMADD_SIZE_8BIT,
//            pData,
//            len,
//            100);
//    HAL_Delay(5);
//}

///*
// * ────────────────────────────────────────────────
// *  读 N 字节
// * ────────────────────────────────────────────────
// */
//void I2C_EE_BufferRead(uint8_t *pData, uint8_t addr, uint16_t len)
//{
//    HAL_I2C_Mem_Read(&hi2c_eeprom,
//            EEPROM_ADDRESS,
//            addr,
//            I2C_MEMADD_SIZE_8BIT,
//            pData,
//            len,
//            1000);
//}
