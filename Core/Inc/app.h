/*
 * app.h
 *
 *  Created on: 2026. 7. 20.
 *      Author: ACER
 */

#ifndef INC_APP_H_
#define INC_APP_H_

#include "main.h"
#include "app_types.h"
#include "cmsis_os2.h"


typedef struct
{
    I2C_HandleTypeDef *i2c;
    UART_HandleTypeDef *uart;

    TIM_HandleTypeDef *hcsr04_timer;
    uint32_t hcsr04_channel;
    GPIO_TypeDef *hcsr04_trig_port;
    uint16_t hcsr04_trig_pin;

    TIM_HandleTypeDef *buzzer_timer;
    uint32_t buzzer_channel;

    TIM_HandleTypeDef *servo_timer;
    uint32_t servo_channel;
} AppHardware_t;

void App_Init(const AppHardware_t *hardware);

void App_Run(void);

HAL_StatusTypeDef App_UartTransmit(
    const char *message);

uint8_t App_GetSensorSnapshot(SensorMessage_t *message);



void App_SetUartTxQueue(
    osMessageQueueId_t queue);

HAL_StatusTypeDef App_UartTransmit(
    const char *message);

#endif /* INC_APP_H_ */

HAL_StatusTypeDef App_UartTransmitBytes(
    const uint8_t *data,
    uint16_t length);
