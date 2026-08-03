/*
 * servo.h
 *
 *  Created on: 2026. 7. 20.
 *      Author: ACER
 */

#ifndef INC_SERVO_H_
#define INC_SERVO_H_

#include "main.h"
#include "app_types.h"

void Servo_Init(TIM_HandleTypeDef *htim,
                uint32_t channel);

void Servo_Update(SystemState_t state);


#endif /* INC_SERVO_H_ */
