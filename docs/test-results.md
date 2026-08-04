\# 검증 및 테스트 결과



기능 동작 확인에 그치지 않고 정상 요청, 잘못된 요청, 통신 오류, 하드웨어 단선, Task 정지 상황을 직접 발생시켜 시스템의 처리와 복구 동작을 검증했습니다.



\## Binary Protocol 자동 테스트



Python과 `pyserial`을 이용해 요청 Packet 생성, CRC 계산, 응답 수신, 응답 내용 검증을 자동화했습니다.



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

총 테스트 결과: 110/110 PASS

```



\## 통신 오류 주입 결과



10회 반복 테스트 후 누적 통계는 다음과 같습니다.



```text

VALID 140 | DUPLICATE 20

CRC ERROR 10 | TIMEOUT 10 | RX DROP 0 | TX FAIL 0

```



검증 결과:



\- CRC가 손상된 Packet 10개를 모두 검출하고 실행하지 않음

\- 불완전 Packet 10개를 Timeout 처리한 뒤 Parser 정상 복구

\- 중복 요청 20개를 감지하고 Cached Response 재전송

\- UART 수신 Byte 유실 0회

\- UART TX Queue 등록 실패 0회



\## 센서 단선 및 자동 복구 테스트



HC-SR04의 ECHO 신호선을 분리해 센서 입력 이상 상황을 만들었습니다.



```text

ECHO 단선

→ 거리 데이터 Invalid 판정

→ 다른 Task와 UART 명령은 계속 동작

→ ECHO 재연결

→ 정상 거리 측정 자동 복구

```



센서 하나의 이상으로 전체 시스템이 정지하지 않았으며, 재연결 후 별도의 Reset 없이 정상 상태로 돌아오는 것을 확인했습니다.



\## Watchdog 복구 테스트



디버그 명령으로 `appTask`를 강제로 정지시켜 `HEALTH\_APP` Bit가 더 이상 보고되지 않도록 했습니다.



```text

FAULT WATCHDOG 입력

→ appTask 정지

→ HEALTH\_APP 미보고

→ watchdogTask가 IWDG Refresh 중단

→ IWDG Reset 발생

→ 시스템 재부팅

→ RESET CAUSE: IWDG 확인

```



주요 Task가 정상적으로 동작하지 않을 때 Watchdog이 실제로 시스템을 재부팅하는 것을 확인했습니다.



\## 부팅 Self-Test



시스템 시작 시 주요 RTOS 객체와 연결 장치의 준비 상태를 검사했습니다.



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



센서 데이터가 아직 준비되지 않은 경우에는 즉시 고장으로 판단하지 않고 `WAIT` 상태로 구분했습니다.



\## Heap 안정성 확인



반복 테스트 전후 FreeRTOS Heap 상태를 확인했습니다.



```text

FREE HEAP     : 5,464Byte

MIN FREE HEAP : 5,464Byte

```



10회 반복 테스트 이후에도 현재 Free Heap과 Minimum Free Heap이 동일하게 유지돼 반복 실행 중 Heap이 지속적으로 감소하는 현상이 없음을 확인했습니다.



\## Task Stack 측정 결과



| Task | 최소 Stack 여유 |

|---|---:|

| `appTask` | 330Word |

| `heartbeatTask` | 96Word |

| `consumerTask` | 76Word |

| `eventTask` | 24Word |

| `monitorTask` | 22Word |

| `commandTask` | 99Word |

| `watchdogTask` | 58Word |

| `uartTxTask` | 160Word |



현재 기능에서는 Stack Overflow가 발생하지 않았습니다.



다만 `eventTask`와 `monitorTask`의 Stack 여유가 상대적으로 작으므로, 향후 문자열 처리나 로그 기능을 추가하면 High Water Mark를 다시 측정해야 합니다.



\## 최종 결과



| 검증 항목 | 결과 |

|---|---|

| Binary Protocol 자동 테스트 | 110/110 PASS |

| CRC 오류 검출 및 복구 | PASS |

| Parser Timeout 복구 | PASS |

| 중복 요청 실행 방지 | PASS |

| UART RX Drop | 0회 |

| UART TX Queue Failure | 0회 |

| 센서 단선 후 자동 복구 | PASS |

| IWDG 자동 Reset | PASS |

| 부팅 Self-Test | 전체 PASS |

| 반복 시험 후 Heap 감소 | 없음 |

