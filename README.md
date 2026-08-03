\# STM32 FreeRTOS Sensor Controller



STM32F401RE에서 센서 데이터 수집, 장치 제어, UART 통신을

FreeRTOS 태스크로 분리하고, 통신 오류와 태스크 이상이 발생해도

검출·복구할 수 있도록 구현한 개인 펌웨어 프로젝트입니다.



단순한 센서 동작 확인을 넘어 UART Binary Protocol의 CRC-16,

Parser Timeout, Retry, 중복 요청 방지와 Task Health Monitoring,

IWDG 기반 자동 복구 구조를 구현하고 반복 테스트로 안정성을 검증했습니다.



\## Tech Stack



\- Language : C, Python

\- MCU : STM32F401RET6 / NUCLEO-F401RE

\- Framework : STM32 HAL, FreeRTOS, CMSIS-RTOS2

\- Peripheral : GPIO, UART, I2C, Timer, PWM, Input Capture, DMA, IWDG

\- RTOS : Queue, Event Flag, Stream Buffer, Direct Task Notification

\- Protocol : Binary Frame, CRC-16/CCITT-FALSE, Timeout, Retry, Duplicate Cache

