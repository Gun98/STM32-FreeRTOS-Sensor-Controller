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





\# 주요 결과



\- UART Binary Protocol 자동 테스트 11개 구현

\- 10회 반복 실행, 총 110/110 테스트 통과

\- 정상 패킷 140개 처리 중 중복 요청 20개 감지 및 응답 재전송

\- 오류 주입 테스트: CRC 오류 10회, Parser Timeout 10회 정상 복구

\- UART RX Drop 0회, TX Queue Failure 0회

\- FreeRTOS Free Heap 및 Minimum Free Heap 5,464Byte 유지

\- HC-SR04 ECHO 단선 시 Sensor Invalid 감지 후 재연결 자동 복구

\- appTask 정지 시 IWDG Reset 및 `RESET CAUSE: IWDG` 확인

\- 부팅 시 RTOS 객체, OLED, EEPROM, RTC, 센서 Self-Test 전체 PASS

## 프로젝트 목표

STM32 주변장치 제어 실습에서 출발해, 여러 기능이 동시에 동작하는 환경에서
데이터 전달, 통신 오류 처리, 자원 관리, 장애 복구까지 경험할 수 있는
펌웨어 시스템을 구현하는 것을 목표로 했습니다.

현재 기능은 Bare-metal 구조로도 구현할 수 있지만, 센서 처리, 명령 파싱,
출력 제어, 상태 모니터링, UART DMA 송신의 실행 책임과 Blocking 영향을
분리하기 위해 FreeRTOS 기반 구조로 확장했습니다.

## 시스템 요구사항

### 기능적 요구사항

- HC-SR04 거리 데이터를 주기적으로 측정하고 유효성을 판정
- OLED와 UART를 통해 센서 및 시스템 상태 제공
- UART Text Command와 Binary Protocol 동시 지원
- Binary 명령을 통한 상태 조회 및 LED 제어
- RTC, OLED, EEPROM 등 I2C 장치의 부팅 Self-Test
- 버튼 입력, LED, Buzzer, PWM 출력 등 주변장치 제어

### 신뢰성 요구사항

- UART RX ISR에서는 수신 Byte 저장과 재수신 등록만 수행
- Stream Buffer를 이용해 ISR과 명령 처리 Task 분리
- UART 송신은 전용 Task만 수행하는 Single Writer 구조 사용
- CRC-16을 이용해 손상된 Binary Packet 거부
- 불완전 Packet은 Parser Timeout 후 폐기하고 정상 수신 상태로 복구
- 응답 유실 시 동일 Sequence로 Retry
- 동일 요청 재수신 시 명령을 다시 실행하지 않고 Cached Response 재전송
- Task Health 상태를 확인한 경우에만 IWDG Refresh
- 센서 단선과 Task 이상 발생 후 시스템이 자동 복구되어야 함
- Heap, Stack, RX Drop, TX Queue Failure를 측정해 안정성 확인


## 하드웨어 구성

| 부품 | 인터페이스 / 핀 | 용도 |
|---|---|---|
| NUCLEO-F401RE | STM32F401RET6, 84MHz | 메인 제어 보드 |
| HC-SR04 | TRIG: PB5, ECHO: PA6 / TIM3_CH1 | 초음파 거리 측정 |
| SSD1306 OLED | I2C1 PB8/PB9, 주소 0x3C | 센서값과 시스템 상태 표시 |
| DS3231 RTC | I2C1 PB8/PB9, 주소 0x68 | 날짜 및 시간 정보 제공 |
| DS3231 EEPROM | I2C1 PB8/PB9, 주소 0x57 | I2C 장치 확인 및 저장장치 인터페이스 |
| KY-006 부저 | PB6 / TIM4_CH1 | PWM 경고음 출력 |
| SG90 서보모터 | PB10 / TIM2_CH3 | 50Hz PWM 위치 제어 |
| NUCLEO LD2 | PA5 | Text 및 Binary 명령을 통한 LED 제어 |
| 사용자 버튼 | PC13 / EXTI | 외부 인터럽트 입력 |
| UART2 VCP | PA2 TX, PA3 RX | Text 명령, Binary Protocol, 디버그 로그 |
| USB 로직 애널라이저 | UART, I2C, PWM 신호선 | 통신 및 PWM 파형 검증 |

### 배선 및 전기적 고려사항

- 모든 모듈과 NUCLEO 보드는 GND를 공통으로 연결했습니다.
- HC-SR04는 5V 전원을 사용했습니다.
- HC-SR04의 ECHO 신호는 약 5V이므로, 10kΩ 2개의 저항을 이용한 전압 분배 회로를 거쳐 약 3.3V로 낮춘 뒤 PA6에 입력했습니다.
- OLED, RTC, EEPROM은 I2C1 버스를 공유하도록 구성했습니다.
- I2C1 통신 속도는 100kHz로 설정했습니다.
- 서보모터 제어 신호는 50Hz PWM을 사용했습니다.
- 외부 전원으로 서보모터를 구동할 경우, 외부 전원의 GND와 NUCLEO의 GND를 공통으로 연결해야 합니다.
