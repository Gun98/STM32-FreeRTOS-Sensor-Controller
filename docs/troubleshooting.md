\## 핵심 문제 해결 사례



\### 1. ASCII 문자열 전송에서 길이 기반 Binary Protocol로 개선



\#### 문제 상황



초기 UART 통신에서는 사람이 읽을 수 있는 문자열 중심으로 명령을 처리했습니다.



하지만 Binary Packet을 구현하면서 다음 두 데이터가 서로 다르다는 점을 확인했습니다.



```text

문자열 "AA"

→ ASCII Byte: 0x41 0x41



실제 Binary 0xAA

→ Binary Byte: 0xAA

```



HEX 형태로 보이는 문자열을 전송해도 MCU에는 실제 Binary 값이 아니라  

각 문자의 ASCII 코드가 수신되기 때문에 Packet Parser가 정상적으로 동작하지 않았습니다.



\#### 원인 분석



문자열 통신은 `\\r`, `\\n`, `\\0`과 같은 종료 문자를 기준으로 데이터를 구분할 수 있지만,  

Binary Payload에는 종료 문자와 같은 값이 정상 데이터로 포함될 수 있습니다.



따라서 문자열 종료 방식으로는 Binary Packet의 정확한 경계를 판단하기 어렵다고 판단했습니다.



\#### 수정 방법



고정된 Header와 `Length` 필드를 사용하는 Binary Frame 구조로 변경했습니다.



```text

SOF | VERSION | TYPE | SEQUENCE | LENGTH | PAYLOAD | CRC

```



Parser는 `Length` 값을 기준으로 Payload 수신 완료 여부를 판단하도록 구성했습니다.



Python 테스트 프로그램에서도 문자열 형태의 HEX 값을 전송하지 않고,  

`bytes` 객체로 실제 Binary Byte 배열을 생성해 UART로 전송했습니다.



\#### 검증 결과



\- PING, 상태 조회, LED 제어 Binary Packet 정상 처리

\- Payload 내부 값과 관계없이 `Length` 기준으로 Packet 조립

\- CRC-16 검증을 통해 손상된 Packet 실행 차단

\- Python 자동 테스트 11개 통과



\#### 배운 점



통신 로그에 같은 HEX 값이 표시되더라도 실제 메모리에 저장되거나  

전송되는 데이터 형식이 문자열인지 Binary인지 구분해야 한다는 점을 배웠습니다.



또한 Binary Protocol에서는 종료 문자보다 명시적인 길이 정보와  

상태 기반 Parser가 더 적합하다는 것을 확인했습니다.



\### 2. Retry 요청에 의한 명령 중복 실행 방지



\#### 문제 상황



PC가 요청을 전송한 뒤 MCU의 응답을 받지 못하면 같은 요청을 Retry하도록 구성했습니다.



하지만 첫 번째 요청이 MCU에서 이미 처리됐고 응답만 유실된 상황이라면,  

재전송된 요청을 다시 실행하면서 LED 제어와 같은 명령이 중복 수행될 수 있습니다.



```text

첫 번째 요청

→ MCU에서 명령 실행

→ 응답 전송

→ PC가 응답을 받지 못함



Retry 요청

→ 같은 명령을 다시 실행할 가능성

```



PING이나 상태 조회처럼 부작용이 적은 요청은 문제가 잘 드러나지 않지만,  

출력 제어 명령은 중복 실행 여부를 명확하게 관리해야 한다고 판단했습니다.



\#### 원인 분석



초기 Retry 구조에서는 PC가 같은 Sequence로 요청을 다시 보내더라도  

MCU가 이를 새로운 요청과 구분하지 않고 Packet Handler를 다시 실행했습니다.



Sequence만 비교할 경우에는 같은 Sequence를 가진 다른 명령을  

잘못 중복 요청으로 판단할 가능성도 있었습니다.



\#### 수정 방법



가장 최근에 처리한 요청 Packet과 해당 응답 Frame을 Cache에 저장했습니다.



다음 항목이 모두 같을 때만 동일 요청의 Retry로 판단하도록 구성했습니다.



\- Version

\- Type

\- Sequence

\- Length

\- Payload



중복 요청으로 판단되면 명령 처리 `switch`문에 진입하지 않고,  

Cache에 저장된 이전 응답 Frame만 UART TX Queue로 다시 전송합니다.



```text

신규 요청

→ 명령 실행

→ 응답 Frame 생성

→ 요청과 응답 Cache 저장

→ 응답 전송



동일 요청 Retry

→ Cache와 요청 비교

→ 명령 재실행 생략

→ Cached Response 재전송

```



응답을 TX Queue에 넣지 못하더라도 명령은 이미 실행됐을 수 있으므로,  

응답 Frame Encode가 성공하면 실제 송신보다 먼저 Cache에 저장하도록 했습니다.



\#### 검증 방법



Python 자동 테스트에 다음 경우를 추가했습니다.



\- 같은 LED ON 요청을 두 번 전송

\- 두 응답 Frame이 동일한지 비교

\- 같은 Sequence에서 Payload만 바꿔 LED ON과 LED OFF 전송

\- 응답 유실을 가정하고 동일 Sequence로 Retry

\- MCU의 `DUPLICATE` 통계 증가 여부 확인



\#### 검증 결과



\- 동일 요청 Retry 시 명령 재실행 없이 Cached Response 재전송

\- 같은 Sequence라도 Payload가 다르면 새로운 요청으로 처리

\- 10회 반복 시험에서 중복 요청 20개 정상 감지

\- 전체 자동 테스트 110/110 통과

\- UART RX Drop 0회, TX Queue Failure 0회



```text

VALID 140 | DUPLICATE 20

CRC ERROR 10 | TIMEOUT 10 | RX DROP 0 | TX FAIL 0

```



\#### 배운 점



Retry는 단순히 같은 Packet을 다시 보내는 기능만으로 끝나지 않고,  

이미 실행된 명령의 부작용을 고려해야 한다는 점을 배웠습니다.



또한 요청 식별에는 Sequence만 사용하는 것보다  

Type, Length, Payload까지 함께 비교해야 안전하다는 것을 확인했습니다.

