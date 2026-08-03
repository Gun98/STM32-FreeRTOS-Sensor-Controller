\# STM32 FreeRTOS Sensor Controller



STM32F401RE에서 센서 데이터 수집, 장치 제어, UART 통신을

FreeRTOS 태스크로 분리하고, 통신 오류와 태스크 이상이 발생해도

검출·복구할 수 있도록 구현한 개인 펌웨어 프로젝트입니다.



단순한 센서 동작 확인을 넘어 UART Binary Protocol의 CRC-16,

Parser Timeout, Retry, 중복 요청 방지와 Task Health Monitoring,

IWDG 기반 자동 복구 구조를 구현하고 반복 테스트로 안정성을 검증했습니다.



\# Tech Stack



\- Language : C, Python

\- MCU : STM32F401RET6 / NUCLEO-F401RE

\- Framework : STM32 HAL, FreeRTOS, CMSIS-RTOS2

\- Peripheral : GPIO, UART, I2C, Timer, PWM, Input Capture, DMA, IWDG

\- RTOS : Queue, Event Flag, Stream Buffer, Direct Task Notification

\- Protocol : Binary Frame, CRC-16/CCITT-FALSE, Timeout, Retry, Duplicate Cache





\# Key Results



\- UART Binary Protocol 자동 테스트 11개 구현

\- 10회 반복 실행, 총 110/110 테스트 통과

\- 정상 패킷 140개 처리 중 중복 요청 20개 감지 및 응답 재전송

\- 오류 주입 테스트: CRC 오류 10회, Parser Timeout 10회 정상 복구

\- UART RX Drop 0회, TX Queue Failure 0회

\- FreeRTOS Free Heap 및 Minimum Free Heap 5,464Byte 유지

\- HC-SR04 ECHO 단선 시 Sensor Invalid 감지 후 재연결 자동 복구

\- appTask 정지 시 IWDG Reset 및 `RESET CAUSE: IWDG` 확인

\- 부팅 시 RTOS 객체, OLED, EEPROM, RTC, 센서 Self-Test 전체 PASS

