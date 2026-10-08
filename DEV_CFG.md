# CFG 구조 명세 --- Function Exit 분리

## 1. 목적

본 명세는 특정 분석기나 중간표현에 종속되지 않고, 함수의 종료 흐름을
의미에 따라 구분하기 위해 분석 대상 CFG가 가져야 하는 구조를 정의한다.

함수의 종료 흐름은 다음 세 종류로 분리한다.

``` text
normal_exit
program_exit
exception_exit
```

핵심 목적은 정상 반환, 프로그램 종료, 예외 전파처럼 서로 다른 의미의
종료 흐름이 하나의 exit에서 먼저 합쳐지는 것을 방지하는 것이다.

------------------------------------------------------------------------

## 2. CFG 분리 시점

Exit 종류의 분리는 정적 분석이 시작되기 전에 완료되어야 한다.

``` text
Program / Intermediate Representation
                ↓
           CFG 구성
                ↓
      Function Exit 분류 및 분리
                ↓
       분석 대상 CFG 확정
                ↓
        정적 분석 시작
```

즉 분석기는 처음부터 `normal_exit`, `program_exit`, `exception_exit`이
분리된 CFG를 대상으로 분석한다.

이 분리는 분석 결과에 따른 동적 CFG 변경이 아니라, 분석 입력 CFG를
구성하는 단계에서 수행되는 구조적 전처리이다.

------------------------------------------------------------------------

## 3. Exit 종류

### 3.1 Normal Exit

현재 함수가 정상적으로 종료되어 caller에게 제어가 반환되는 모든 흐름의
도착점이다.

``` text
normal termination flow(s)
          ↓
     normal_exit
          ↓
        caller
```

여러 정상 반환 경로가 존재하면 모두 `normal_exit` 종류에 속한다.

### 3.2 Program Exit

현재 함수에서 caller로 제어가 반환되지 않고 프로그램 실행 자체가
종료되는 흐름의 도착점이다.

``` text
program termination flow(s)
           ↓
      program_exit
           ↓
   no caller continuation
```

예를 들어 abort, process termination, fatal assertion failure, noreturn
termination 등의 의미를 가진 흐름이 해당될 수 있다.

구체적으로 어떤 IR 연산이나 terminal node가 이를 의미하는지는 대상
분석기의 표현에 따라 결정한다.

### 3.3 Exception Exit

현재 함수 내부에서 처리되지 않은 exception이 함수 밖으로 전파되어 caller
방향으로 전달되는 흐름의 도착점이다.

``` text
exception propagation flow(s)
             ↓
       exception_exit
             ↓
           caller
```

Exception handling path에 진입했다는 사실 자체는 `exception_exit`을
의미하지 않는다.

``` text
exception 발생
    ↓
handler
    ↓
함수 내부에서 처리
    ↓
normal execution
    ↓
normal_exit
```

반대로 최종적으로 함수 밖으로 전파되는 경우에만:

``` text
exception 발생
    ↓
handler / cleanup / rethrow
    ↓
exception propagation
    ↓
exception_exit
```

으로 분류한다.

------------------------------------------------------------------------

## 4. 목표 CFG 구조

``` text
[normal termination flow(s)] ─────→ [normal_exit]

[program termination flow(s)] ────→ [program_exit]

[exception propagation flow(s)] ─→ [exception_exit]
```

필요한 경우 framework 호환성을 위해 종류별 exit 이후에 하나의 통합
exit을 둘 수 있다.

``` text
normal flow(s) ─────→ normal_exit ─────┐
program flow(s) ────→ program_exit ────┼→ unified_exit
exception flow(s) ─→ exception_exit ───┘
```

`unified_exit`은 선택적이며, 핵심 구조는 세 종료 종류의 분리이다.

------------------------------------------------------------------------

## 5. 서로 다른 Exit 종류의 선행 병합 금지

다음 구조는 허용하지 않는다.

``` text
normal termination ─────┐
program termination ────┼→ common_exit → 종료 종류 판별
exception propagation ──┘
```

요구되는 순서는:

``` text
종료 의미 판별
    ↓
종류별 exit으로 분리
    ↓
필요한 경우에만 통합
```

이다.

즉:

``` text
normal termination → normal_exit
program termination → program_exit
exception propagation → exception_exit
```

후에만 선택적으로:

``` text
normal_exit ─────┐
program_exit ────┼→ unified_exit
exception_exit ──┘
```

을 구성할 수 있다.

------------------------------------------------------------------------

## 6. 동일 종류의 여러 종료 흐름

같은 종류의 종료 흐름은 여러 개 존재할 수 있다.

``` text
normal flow 1 ─┐
normal flow 2 ─┼→ normal_exit
normal flow 3 ─┘
```

``` text
program flow 1 ─┐
program flow 2 ─┼→ program_exit
program flow 3 ─┘
```

``` text
exception flow 1 ─┐
exception flow 2 ─┼→ exception_exit
exception flow 3 ─┘
```

종류별 exit이 하나의 merge node로 표현되더라도 해당 exit으로 들어오는
개별 predecessor/incoming flow 관계는 식별 가능해야 한다.

본 CFG 명세에서는 각 incoming flow의 분석 정보를 어떻게 계산하거나
결합하는지는 정의하지 않는다.

------------------------------------------------------------------------

## 7. Exception Handling Flow

Exception-related CFG에서는 다음을 구분해야 한다.

``` text
exception handling path에 진입
≠
exception이 현재 함수를 벗어남
```

함수 내부에서 처리되는 경우:

``` text
exception
    ↓
handler entry
    ↓
handler
    ↓
normal execution
    ↓
normal termination
    ↓
normal_exit
```

함수 밖으로 전파되는 경우:

``` text
exception
    ↓
handler entry
    ↓
cleanup / rethrow / propagation
    ↓
exception propagation terminal
    ↓
exception_exit
```

따라서 exit 종류는 exception-related node의 존재가 아니라 최종
control-flow 의미에 따라 결정한다.

------------------------------------------------------------------------

## 8. Program Termination Flow

Program termination은 normal termination 및 exception propagation과
구분한다.

``` text
             condition
              /     \
             /       \
      normal path   fatal path
           ↓            ↓
     normal_exit    program_exit
```

의미 차이는 다음과 같다.

``` text
normal_exit
→ caller로 정상적인 제어 반환

program_exit
→ 프로그램 실행 종료, caller continuation 없음

exception_exit
→ exception을 통해 caller 방향으로 제어 전파
```

------------------------------------------------------------------------

## 9. Exit이 존재하지 않는 경우

모든 함수가 세 exit을 모두 가질 필요는 없다.

``` text
항상 정상 반환
→ normal_exit만 존재 가능

항상 프로그램 종료
→ program_exit만 존재 가능

정상 반환 또는 exception 전파
→ normal_exit + exception_exit

세 종류 모두 가능
→ normal_exit + program_exit + exception_exit
```

존재하지 않는 종류의 exit을 강제로 생성할 필요는 없다.

단, 대상 framework가 고정된 exit node 집합을 요구한다면 incoming flow가
없는 빈 exit으로 표현할 수 있다.

------------------------------------------------------------------------

## 10. Unified Exit

대상 framework가 하나의 최종 function exit을 요구하면 `unified_exit`을
유지할 수 있다.

연결 순서는 반드시:

``` text
terminal flow
    ↓
종류별 exit
    ↓
unified_exit
```

이어야 한다.

허용:

``` text
normal flow ─────→ normal_exit ─────┐
program flow ────→ program_exit ────┼→ unified_exit
exception flow ─→ exception_exit ───┘
```

허용하지 않음:

``` text
normal flow ─────┐
program flow ────┼→ unified_exit → 종류별 exit
exception flow ──┘
```

`unified_exit`은 framework compatibility를 위한 후속 통합 지점일 뿐, 세
종료 의미를 대체하지 않는다.

------------------------------------------------------------------------

## 11. CFG 구성 요구사항

``` text
R1.
Exit 종류의 분리는 정적 분석 시작 전에 완료한다.

R2.
함수의 terminal flow를 control-flow 의미에 따라
normal / program / exception 종류로 분류한다.

R3.
caller에게 정상적으로 제어를 반환하는 flow는 normal_exit에 연결한다.

R4.
프로그램 실행 자체를 종료하여 caller로 제어가 반환되지 않는 flow는
program_exit에 연결한다.

R5.
현재 함수에서 처리되지 않은 exception을 caller 방향으로 전파하는 flow는
exception_exit에 연결한다.

R6.
서로 다른 종료 종류의 flow를 종류별 exit 이전에 하나의 공통 exit으로 병합하지 않는다.

R7.
동일 종류의 종료 flow가 여러 개 존재할 수 있으며,
이들은 해당 종류의 exit으로 연결할 수 있다.

R8.
종류별 exit으로 들어오는 개별 predecessor/incoming flow 관계를 식별할 수 있어야 한다.

R9.
exception handling path에 진입했다는 사실만으로 exception_exit으로 분류하지 않는다.

R10.
함수 내부에서 exception이 처리되고 최종적으로 정상 반환하는 flow는 normal_exit에 연결한다.

R11.
필요한 경우 종류별 exit 이후에 unified_exit을 둘 수 있다.

R12.
unified_exit이 존재하더라도 종류별 exit 구조는 유지한다.
```

------------------------------------------------------------------------

## 12. CFG Invariant

``` text
Invariant 1.
모든 정상 종료 flow는 normal_exit 종류에 속한다.

Invariant 2.
모든 프로그램 종료 flow는 program_exit 종류에 속한다.

Invariant 3.
모든 미처리 exception 전파 flow는 exception_exit 종류에 속한다.

Invariant 4.
하나의 terminal flow를 서로 다른 exit 종류에 동시에 분류하지 않는다.

Invariant 5.
서로 다른 exit 종류의 flow는 종류별 exit 이전에 병합되지 않는다.

Invariant 6.
exception handler 진입 node 자체는 exception_exit으로 간주하지 않는다.

Invariant 7.
exception이 함수 내부에서 처리된 후 정상 반환하면 normal_exit에 속한다.

Invariant 8.
exception이 현재 함수 밖으로 전파될 때만 exception_exit에 속한다.

Invariant 9.
program_exit flow에는 caller로 돌아가는 정상 continuation이 존재하지 않는다.

Invariant 10.
종류별 exit의 predecessor/incoming flow 관계를 식별할 수 있어야 한다.

Invariant 11.
unified_exit이 존재하는 경우 종류별 exit 이후에 위치한다.

Invariant 12.
CFG exit 분리는 정적 분석 결과에 의존하지 않고 분석 시작 전에 완료된다.
```

------------------------------------------------------------------------

## 13. 대표 CFG 예시

### 13.1 Normal Return만 존재

``` text
entry
  ↓
 ...
  ↓
normal termination
  ↓
normal_exit
```

### 13.2 Normal Return / Program Termination

``` text
               entry
                 ↓
             condition
              /     \
             /       \
      normal path   fatal path
           ↓            ↓
 normal termination  program termination
           ↓            ↓
     normal_exit    program_exit
```

### 13.3 Normal Return / Exception Propagation

``` text
                entry
                  ↓
              operation
               /     \
          normal     exception
            ↓            ↓
           ...      exception handling
            ↓            ↓
 normal termination   propagation
            ↓            ↓
      normal_exit   exception_exit
```

### 13.4 Exception이 함수 내부에서 처리됨

``` text
               operation
               /       \
          normal       exception
            ↓              ↓
            │          handler entry
            │              ↓
            │            catch
            │              ↓
            └──────→ normal execution
                           ↓
                  normal termination
                           ↓
                      normal_exit
```

Exception path가 존재하더라도 현재 함수에서 처리되어 정상 반환하면
`exception_exit`으로 종료되지 않는다.

### 13.5 세 종료 종류가 모두 존재

``` text
                         entry
                           ↓
                          ...
                 ┌─────────┼─────────┐
                 ↓         ↓         ↓
          normal flow  fatal flow  exception flow
                 ↓         ↓         ↓
          normal term. program term. propagation
                 ↓         ↓         ↓
          normal_exit program_exit exception_exit
                 \         |         /
                  \        |        /
                   └── unified_exit ┘
                       (optional)
```

------------------------------------------------------------------------

## 14. 최종 구조

``` text
                    ┌─────────────────────┐
normal flow(s) ────→│     normal_exit     │──┐
                    └─────────────────────┘  │
                                             │
                    ┌─────────────────────┐  │
program-ending ────→│     program_exit    │──┼→ unified_exit
flow(s)             └─────────────────────┘  │   (optional)
                                             │
                    ┌─────────────────────┐  │
exception- ────────→│    exception_exit   │──┘
propagating flow(s) └─────────────────────┘
```

핵심 원칙:

``` text
1. 분석 시작 전에 exit 종류를 분리한다.

2. 서로 다른 종료 의미의 flow를 종류별 exit 이전에 합치지 않는다.

3. 동일 종류의 여러 flow는 해당 종류의 exit으로 연결할 수 있다.

4. 종류별 exit의 predecessor/incoming flow 관계를 보존한다.

5. unified_exit이 필요하면 종류별 exit 이후에만 구성한다.
```
