/*
 * ds3231.h
 *
 *  Created on: 2026. 7. 20.
 *      Author: ACER
 */

#ifndef INC_DS3231_H_
#define INC_DS3231_H_
#include "main.h"

void DS3231_Init(I2C_HandleTypeDef *hi2c);

HAL_StatusTypeDef DS3231_ReadTime(
    uint8_t *hour,
    uint8_t *minute,
    uint8_t *second);


#endif /* INC_DS3231_H_ */
