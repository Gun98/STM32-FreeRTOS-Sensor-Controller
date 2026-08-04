\# 검증 및 테스트 결과



기능이 정상적으로 동작하는지만 확인하지 않고, 정상 요청, 잘못된 요청, 통신 오류, 센서 단선, Task 정지 상황을 직접 발생시켜 시스템의 처리와 복구 동작을 검증했습니다.



\## Binary Protocol 자동 테스트



Python과 `pyserial`을 이용해 다음 과정을 자동화했습니다.



```text

요청 Packet 생성

→ CRC 계산

→ UART 전송

→ MCU 응답 수신

→ 응답 Type, Sequence, Payload 검증

→ PASS 또는 FAIL 판정

```



\### 자동 테스트 항목



| 번호 | 테스트 항목 | 검증 내용 |

|---:|---|---|

| 1 | PING | `PING` 요청에 대한 `PONG` 응답 확인 |

| 2 | 상태 조회 | 거리값과 센서 유효 상태가 포함된 `STATUS` 응답 확인 |

| 3 | LED ON | Binary 명령을 통한 LED ON 제어 |

| 4 | LED OFF | Binary 명령을 통한 LED OFF 제어 |

| 5 | 잘못된 LED Payload | `INVALID\_PAYLOAD` 응답 확인 |

| 6 | 알 수 없는 Type | `UNKNOWN\_TYPE` 응답 확인 |

| 7 | 응답 유실 및 Retry | 첫 응답을 유실시킨 뒤 동일 Sequence로 재전송 |

| 8 | 동일 요청 재전송 | 명령 재실행 없이 Cached Response 재전송 |

| 9 | 동일 Sequence, 다른 Payload | 중복 요청이 아닌 새로운 요청으로 처리 |

| 10 | 잘못된 CRC 후 복구 | 손상 Packet 폐기 후 다음 정상 Packet 처리 |

| 11 | 불완전 Packet Timeout | 부분 Packet 폐기 후 다음 정상 Packet 처리 |



11개 테스트를 10회 반복해 총 110회의 테스트를 수행했습니다.



```text

11개 테스트 × 10회 반복

총 결과: 110/110 PASS

```



\## 통신 오류 주입 결과



정상 기능뿐 아니라 손상 Packet과 불완전 Packet을 직접 전송해 오류 처리와 복구 동작을 확인했습니다.



10회 반복 테스트 후 누적 통계는 다음과 같습니다.



```text

VALID 140 | DUPLICATE 20

CRC ERROR 10 | TIMEOUT 10 | RX DROP 0 | TX FAIL 0

```



\### 측정 결과



| 측정 항목 | 결과 |

|---|---:|

| CRC 검증 통과 Packet | 140개 |

| 중복 요청 감지 | 20개 |

| CRC 오류 감지 | 10개 |

| Parser Timeout 감지 | 10개 |

| UART RX Drop | 0회 |

| UART TX Queue Failure | 0회 |



\### CRC 오류 검증



CRC가 잘못된 Binary Packet을 전송해 다음 동작을 확인했습니다.



```text

손상 Packet 수신

→ CRC 불일치 검출

→ Packet Handler 실행하지 않음

→ 명령 실행하지 않음

→ CRC ERROR 증가

→ Parser 초기화

→ 다음 정상 Packet 처리

```



CRC가 손상된 Packet 10개를 모두 감지했으며, 손상된 명령은 실행되지 않았습니다.



CRC 오류 이후 전송한 정상 Packet은 문제없이 처리됐습니다.



\### Parser Timeout 검증



SOF와 Header 일부만 전송한 뒤 나머지 데이터를 보내지 않아 불완전 Packet 상황을 만들었습니다.



```text

부분 Packet 수신

→ 100ms 동안 Packet 미완성

→ Parser Timeout 발생

→ 부분 Packet 폐기

→ Parser 초기화

→ 다음 정상 Packet 처리

```



불완전 Packet 10개를 모두 Timeout 처리했으며, Timeout 이후 정상 Packet 수신도 문제없이 복구됐습니다.



\### 중복 요청 검증



응답 유실을 가정해 동일한 요청을 같은 Sequence로 다시 전송했습니다.



```text

첫 번째 요청

→ MCU 명령 실행

→ 응답 생성 및 Cache 저장

→ PC에서 첫 응답을 의도적으로 무시



Retry 요청

→ 동일 요청 감지

→ 명령 재실행하지 않음

→ Cached Response 재전송

```



총 20개의 중복 요청을 감지했고, 명령을 다시 실행하지 않고 기존 응답만 재전송했습니다.



같은 Sequence라도 Payload가 다르면 새로운 요청으로 처리되는 것도 확인했습니다.



\## 센서 단선 및 자동 복구 테스트



HC-SR04의 ECHO 신호선을 분리해 센서 입력 이상 상황을 만들었습니다.



```text

ECHO 단선

→ 거리 측정값 Invalid 판정

→ 센서 상태 이상 표시

→ 다른 Task와 UART 명령은 계속 동작

→ ECHO 재연결

→ 정상 거리 측정 자동 복구

```



센서 하나에 문제가 발생해도 전체 시스템은 정지하지 않았습니다.



ECHO 신호선을 다시 연결한 뒤 별도의 Reset 없이 거리 측정이 자동으로 정상 복구되는 것을 확인했습니다.



\## Watchdog 복구 테스트



디버그 명령으로 `appTask`를 강제로 정지시켜 `HEALTH\_APP` Bit가 더 이상 보고되지 않도록 했습니다.



사용한 명령은 다음과 같습니다.



```text

FAULT WATCHDOG

```



실제 처리 흐름:



```text

FAULT WATCHDOG 입력

→ appTask 정지

→ HEALTH\_APP 미보고

→ watchdogTask가 Health 조건 불충족 감지

→ IWDG Refresh 중단

→ IWDG Reset 발생

→ 시스템 재부팅

→ RESET CAUSE: IWDG 출력

```



주요 Task가 정상적으로 실행되지 않을 때 Watchdog이 계속 갱신되는 것이 아니라, 실제로 시스템을 재부팅하는 것을 확인했습니다.



재부팅 후 Reset 원인이 다음과 같이 출력되는 것도 확인했습니다.



```text

RESET CAUSE: IWDG

```



\## 부팅 Self-Test



시스템 시작 시 주요 RTOS 객체와 연결 장치의 준비 상태를 검사했습니다.



정상 부팅 시 출력 결과:



```text

=== SYSTEM SELF TEST ===

\[SELF TEST] RTOS OBJECTS : PASS

\[SELF TEST] OLED 0x3C    : PASS

\[SELF TEST] EEPROM 0x57  : PASS

\[SELF TEST] RTC 0x68     : PASS

\[SELF TEST] SENSOR       : PASS

\[SELF TEST] RESULT       : PASS

========================

```



\### Self-Test 검사 항목



| 검사 항목 | 검사 내용 |

|---|---|

| RTOS Objects | Queue, Event Flag, Stream Buffer 등 주요 RTOS 객체 생성 여부 |

| OLED | I2C 주소 `0x3C` 응답 여부 |

| EEPROM | I2C 주소 `0x57` 응답 여부 |

| RTC | I2C 주소 `0x68` 응답 여부 |

| Sensor | 유효한 Sensor Snapshot 준비 여부 |



센서 데이터가 아직 준비되지 않은 경우에는 즉시 하드웨어 고장으로 판단하지 않고 `WAIT` 상태로 구분했습니다.



이를 통해 부팅 직후 데이터가 아직 생성되지 않은 상황과 실제 센서 이상을 구분했습니다.



\## Heap 안정성 확인



반복 테스트 전후 FreeRTOS Heap 상태를 확인했습니다.



```text

FREE HEAP     : 5,464Byte

MIN FREE HEAP : 5,464Byte

```



현재 Free Heap과 Minimum Free Heap이 동일하게 유지됐습니다.



이는 테스트 반복 중 현재 사용 가능한 Heap이 지속적으로 감소하거나, 일시적으로 더 큰 Heap 사용량이 발생하지 않았다는 의미입니다.



다만 이 결과만으로 모든 종류의 메모리 오류가 없다고 단정하지 않고, 반복 시험에서 Heap 감소 현상이 없었음을 확인한 결과로 사용했습니다.



\## Task Stack 측정 결과



FreeRTOS Stack High Water Mark를 이용해 각 Task의 최소 Stack 여유를 확인했습니다.



| Task | 최소 Stack 여유 | Byte 환산 |

|---|---:|---:|

| `appTask` | 330Word | 1,320Byte |

| `heartbeatTask` | 96Word | 384Byte |

| `consumerTask` | 76Word | 304Byte |

| `eventTask` | 24Word | 96Byte |

| `monitorTask` | 22Word | 88Byte |

| `commandTask` | 99Word | 396Byte |

| `watchdogTask` | 58Word | 232Byte |

| `uartTxTask` | 160Word | 640Byte |



현재 기능에서는 Stack Overflow가 발생하지 않았습니다.



다만 `eventTask`와 `monitorTask`의 Stack 여유가 상대적으로 작게 측정됐습니다.



향후 해당 Task에 지역변수, 문자열 처리, 로그 출력 등의 기능을 추가할 경우 Stack High Water Mark를 다시 측정하고 필요하면 Stack 크기를 조정할 예정입니다.



\## 최종 검증 결과



| 검증 항목 | 결과 |

|---|---|

| Binary Protocol 자동 테스트 | 11개 테스트를 10회 반복, 총 110/110 PASS |

| PING 및 상태 조회 | PASS |

| LED ON/OFF 제어 | PASS |

| 잘못된 Payload 처리 | PASS |

| 정의되지 않은 Type 처리 | PASS |

| CRC 오류 검출 | 10회 정상 감지 |

| CRC 오류 이후 Parser 복구 | PASS |

| 불완전 Packet Timeout | 10회 정상 감지 |

| Timeout 이후 Parser 복구 | PASS |

| 중복 요청 실행 방지 | 20개 정상 감지 |

| 동일 Sequence, 다른 Payload 처리 | 새로운 요청으로 정상 처리 |

| UART RX Drop | 0회 |

| UART TX Queue Failure | 0회 |

| 센서 ECHO 단선 감지 | PASS |

| 센서 재연결 자동 복구 | PASS |

| IWDG 자동 Reset | PASS |

| Reset 원인 확인 | `RESET CAUSE: IWDG` |

| 부팅 Self-Test | 전체 PASS |

| 반복 시험 후 Heap 감소 | 없음 |

| Stack Overflow | 발생하지 않음 |



\## 검증을 통해 확인한 점



이번 테스트를 통해 정상 기능뿐 아니라 다음과 같은 오류 상황에서도 시스템이 동작을 유지하거나 정상 상태로 복구되는 것을 확인했습니다.



\- 손상 Packet은 명령 실행 전에 차단

\- 불완전 Packet은 Timeout 후 폐기

\- 응답 유실 시 동일 Sequence로 Retry

\- 중복 요청은 명령을 다시 실행하지 않고 기존 응답만 재전송

\- 센서 단선이 전체 시스템 정지로 이어지지 않음

\- 주요 Task 정지 시 IWDG를 이용해 시스템 재부팅

\- 반복 테스트 후 Heap 감소와 UART Queue 오류가 발생하지 않음



단순히 기능이 한 번 동작하는 것을 확인하는 데서 끝내지 않고, 오류를 직접 주입하고 측정값을 기록해 펌웨어의 안정성과 복구 동작을 검증했습니다.

