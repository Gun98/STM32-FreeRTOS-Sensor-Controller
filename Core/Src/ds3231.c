/*
 * ds3231.c
 *
 *  Created on: 2026. 7. 20.
 *      Author: ACER
 */

#include "ds3231.h"

#define DS3231_I2C_ADDRESS       (0x68U << 1)
#define DS3231_TIME_REGISTER      0x00U
#define DS3231_I2C_TIMEOUT_MS    100U

static I2C_HandleTypeDef *ds3231_i2c = NULL;

static uint8_t DS3231_BcdToDecimal(uint8_t bcd)
{
    return (uint8_t)(
        ((bcd >> 4U) * 10U) +
        (bcd & 0x0FU));
}

void DS3231_Init(I2C_HandleTypeDef *hi2c)
{
    ds3231_i2c = hi2c;
}

HAL_StatusTypeDef DS3231_ReadTime(
    uint8_t *hour,
    uint8_t *minute,
    uint8_t *second)
{
    uint8_t time_data[3];
    HAL_StatusTypeDef status;

    if ((ds3231_i2c == NULL) ||
        (hour == NULL) ||
        (minute == NULL) ||
        (second == NULL))
    {
        return HAL_ERROR;
    }

    status = HAL_I2C_Mem_Read(
        ds3231_i2c,
        DS3231_I2C_ADDRESS,
        DS3231_TIME_REGISTER,
        I2C_MEMADD_SIZE_8BIT,
        time_data,
        sizeof(time_data),
        DS3231_I2C_TIMEOUT_MS);

    if (status != HAL_OK)
    {
        return status;
    }

    *second = DS3231_BcdToDecimal(
        time_data[0] & 0x7FU);

    *minute = DS3231_BcdToDecimal(
        time_data[1] & 0x7FU);

    *hour = DS3231_BcdToDecimal(
        time_data[2] & 0x3FU);

    return HAL_OK;
}
