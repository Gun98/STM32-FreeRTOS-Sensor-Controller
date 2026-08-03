/*
 * buzzer.h
 *
 *  Created on: 2026. 7. 20.
 *      Author: ACER
 */

#ifndef INC_BUZZER_H_
#define INC_BUZZER_H_

#include "main.h"
#include "app_types.h"

void Buzzer_Init(TIM_HandleTypeDef *htim,
                 uint32_t channel);

void Buzzer_Update(uint32_t now,
                   SystemState_t state);


#endif /* INC_BUZZER_H_ */
