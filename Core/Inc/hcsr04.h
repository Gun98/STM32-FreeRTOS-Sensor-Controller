/*
 * hcsr04.h
 *
 *  Created on: 2026. 7. 20.
 *      Author: ACER
 */

#ifndef INC_HCSR04_H_
#define INC_HCSR04_H_

#include "main.h"

typedef enum
{
    HCSR04_EVENT_NONE = 0,
    HCSR04_EVENT_MEASUREMENT_DONE,
    HCSR04_EVENT_TIMEOUT
} HCSR04_Event_t;

void HCSR04_Init(TIM_HandleTypeDef *htim,
                 uint32_t channel,
                 GPIO_TypeDef *trig_port,
                 uint16_t trig_pin);

void HCSR04_StartMeasurement(uint32_t now);

void HCSR04_Update(uint32_t now);

HCSR04_Event_t HCSR04_GetEvent(
    uint32_t *echo_time_us);

uint8_t HCSR04_IsBusy(void);

void HCSR04_IC_CaptureCallback(TIM_HandleTypeDef *htim);




#endif /* INC_HCSR04_H_ */
