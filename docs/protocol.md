\# UART Binary Protocol 상세 설계



\## UART Binary Protocol



Text 명령과 별도로 길이 기반 Binary Protocol을 구현했습니다.



문자열 종료 문자에 의존하지 않고 `Length` 필드를 기준으로 Payload를 처리하며,  

CRC-16, Parser Timeout, Retry, 중복 요청 방지를 적용해 통신 신뢰성을 높였습니다.



\### Packet 구조



| 필드 | 크기 | 설명 |

|---|---:|---|

| SOF1 | 1Byte | 시작 Byte `0xAA` |

| SOF2 | 1Byte | 시작 Byte `0x55` |

| Version | 1Byte | Protocol Version, 현재 `0x01` |

| Type | 1Byte | 요청 또는 응답 종류 |

| Sequence | 1Byte | 요청과 응답을 식별하는 번호 |

| Length | 1Byte | Payload 길이, 최대 32Byte |

| Payload | 0\~32Byte | 명령 또는 응답 데이터 |

| CRC Low | 1Byte | CRC 하위 Byte |

| CRC High | 1Byte | CRC 상위 Byte |



```text

AA 55 | VERSION | TYPE | SEQUENCE | LENGTH | PAYLOAD | CRC\_LOW CRC\_HIGH

```



CRC 계산 대상에는 SOF를 제외한 다음 필드를 포함합니다.



```text

VERSION + TYPE + SEQUENCE + LENGTH + PAYLOAD

```



\### CRC 설정



\- \*\*알고리즘:\*\* CRC-16/CCITT-FALSE

\- \*\*Polynomial:\*\* `0x1021`

\- \*\*Initial Value:\*\* `0xFFFF`

\- \*\*Final XOR:\*\* `0x0000`

\- \*\*Input/Output Reflection:\*\* 사용하지 않음

\- \*\*전송 순서:\*\* Low Byte 먼저 전송



SOF는 Parser가 Packet 경계를 탐색하는 용도로 사용하고,  

실제 Packet 내용의 무결성은 Version부터 Payload까지 검증하도록 구분했습니다.



\### 요청 Packet



| Type | 값 | 설명 |

|---|---:|---|

| `PING` | `0x01` | 통신 연결 확인 |

| `GET\_STATUS` | `0x02` | 센서 상태 조회 |

| `LED\_SET` | `0x03` | LED ON/OFF 제어 |



\### 응답 Packet



| Type | 값 | 설명 |

|---|---:|---|

| `PONG` | `0x81` | PING 정상 응답 |

| `STATUS` | `0x82` | 거리값과 센서 유효 상태 응답 |

| `ACK` | `0x83` | 제어 명령 접수 성공 |

| `ERROR` | `0xFF` | 요청 형식 또는 처리 오류 |



\### Error Code



| Error Code | 값 | 발생 조건 |

|---|---:|---|

| `OK` | `0x00` | 정상 처리 |

| `INVALID\_LENGTH` | `0x01` | 요청 Payload 길이가 잘못된 경우 |

| `INVALID\_PAYLOAD` | `0x02` | 허용되지 않은 Payload 값 |

| `UNKNOWN\_TYPE` | `0x03` | 정의되지 않은 Packet Type |

| `SNAPSHOT\_FAILED` | `0x04` | 센서 Snapshot을 읽지 못한 경우 |

| `CONTROL\_QUEUE\_FULL` | `0x05` | 제어 Queue에 명령을 넣지 못한 경우 |



\### Binary Packet 처리 흐름



```mermaid

flowchart LR

&#x20;   UART\[USART2 RX ISR] -->|1Byte 수신| SB\[Stream Buffer]

&#x20;   SB --> CMD\[commandTask]

&#x20;   CMD --> PARSER\[Binary Parser State Machine]



&#x20;   PARSER -->|CRC 정상| HANDLER\[Packet Handler]

&#x20;   PARSER -->|CRC 오류| CRCERR\[CRC Error Count 증가]

&#x20;   PARSER -->|100ms 초과| TIMEOUT\[Parser 초기화 및 Timeout Count 증가]



&#x20;   HANDLER --> DUP{이전 요청과 동일한가?}



&#x20;   DUP -->|예| CACHE\[Cached Response 재전송]

&#x20;   DUP -->|아니오| EXEC\[요청 검증 및 명령 처리]



&#x20;   EXEC --> PING\[PING 처리]

&#x20;   EXEC --> STATUS\[Sensor Snapshot 조회]

&#x20;   EXEC --> LED\[controlQueue에 LED 요청]

&#x20;   EXEC --> ERROR\[Error Response 생성]



&#x20;   PING --> RESP\[응답 Frame 생성]

&#x20;   STATUS --> RESP

&#x20;   LED --> RESP

&#x20;   ERROR --> RESP



&#x20;   RESP --> STORE\[요청과 응답 Cache 저장]

&#x20;   STORE --> TXQ\[uartTxQueue]

&#x20;   CACHE --> TXQ



&#x20;   TXQ --> TXTASK\[uartTxTask]

&#x20;   TXTASK --> DMA\[UART DMA 송신]

```



\## 통신 오류 처리



\### 1. CRC 오류 검출



수신 완료된 Packet의 CRC를 계산한 뒤 수신 CRC와 비교합니다.



CRC가 일치하지 않으면 다음과 같이 처리합니다.



\- Packet Handler를 호출하지 않음

\- 명령을 실행하지 않음

\- 응답을 전송하지 않음

\- CRC Error Count 증가

\- Parser를 초기 상태로 복구



손상된 명령이 실제 장치 제어로 이어지지 않도록 검증과 실행 단계를 분리했습니다.



\### 2. 불완전 Packet Timeout



SOF 수신 후 Packet이 완성되지 않은 상태가 100ms 이상 지속되면  

부분 Packet을 폐기하고 Parser를 초기 상태로 되돌립니다.



`commandTask`는 Stream Buffer를 최대 20ms 동안 대기하며,  

수신 데이터가 없더라도 `UartProtocol\_CheckTimeout()`을 호출해  

Parser Timeout을 검사합니다.



이를 통해 중간에 끊긴 Packet 때문에 다음 정상 Packet까지  

잘못 해석되는 문제를 방지했습니다.



\### 3. 응답 유실 시 Retry



Python 테스트 프로그램은 응답이 0.5초 안에 도착하지 않으면  

동일 요청을 최대 3회까지 재전송합니다.



Retry 시에는 새로운 Sequence를 만들지 않고 기존 Sequence를 유지합니다.



```text

첫 번째 요청 : TYPE 0x01 / SEQ 0x50

재전송 요청  : TYPE 0x01 / SEQ 0x50

```



동일한 Sequence를 사용해야 MCU가 같은 논리적 요청의 재전송인지  

판단할 수 있습니다.



\### 4. 중복 명령 실행 방지



Retry 요청을 그대로 다시 실행하면 LED 제어와 같은 명령이  

두 번 수행될 수 있습니다.



이를 방지하기 위해 가장 최근의 요청과 응답 Frame을 Cache에 저장합니다.



다음 항목이 모두 같을 때 중복 요청으로 판단합니다.



\- Version

\- Type

\- Sequence

\- Length

\- Payload



중복 요청이면 Packet Handler의 명령 처리 코드를 다시 실행하지 않고,  

저장된 응답 Frame만 재전송합니다.



```text

첫 번째 요청

→ 명령 실행

→ 응답 생성

→ 요청과 응답 Cache 저장



동일 요청 Retry

→ 중복 요청 감지

→ 명령 재실행하지 않음

→ Cached Response만 재전송

```



Sequence가 같더라도 Payload가 다르면 새로운 요청으로 처리합니다.



\### 5. UART 송신 실패 측정



모든 Binary Frame 송신은 공통 Queue 함수를 거치도록 구성했습니다.



`uartTxQueue` 등록에 실패하면 `TX FAIL` 카운터를 증가시켜  

송신 경로의 오류 발생 여부를 확인할 수 있도록 했습니다.



\## 통신 진단 통계



PuTTY에서 다음 명령으로 누적 통계를 확인할 수 있습니다.



```text

PKT STAT

```



| 항목 | 의미 |

|---|---|

| `VALID` | CRC 검증을 통과해 Handler까지 전달된 Packet 수 |

| `DUPLICATE` | 중복 요청으로 감지된 Packet 수 |

| `CRC ERROR` | CRC가 일치하지 않은 Packet 수 |

| `TIMEOUT` | 완성되지 못하고 Timeout 처리된 Packet 수 |

| `RX DROP` | Stream Buffer에 저장하지 못한 UART 수신 Byte 수 |

| `TX FAIL` | UART TX Queue 등록 실패 횟수 |



10회 반복 테스트 후 측정 결과는 다음과 같습니다.



```text

VALID 140 | DUPLICATE 20

CRC ERROR 10 | TIMEOUT 10 | RX DROP 0 | TX FAIL 0

```



UART Binary Protocol

통신 오류 처리

통신 진단 통계

