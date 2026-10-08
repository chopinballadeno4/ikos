# Parameter-Origin Symbolic Exit Summary 기반 Interprocedural Pruning

## 1. 연구 배경

정적 분석기는 프로그램을 실제로 실행하지 않고도 변수의 가능한 값과 메모리
상태를 추론하여 오류 가능성을 탐지한다. 그러나 함수 호출 경계를 넘는 순간
분석에 유용한 정보가 약화될 수 있다. Callee 내부에서 얻은 조건은 대개 해당
함수의 SSA value나 local variable을 기준으로 표현되기 때문에 caller가 호출
이후의 상태를 분석할 때 직접 재사용하기 어렵다.

특히 함수 호출 다음의 제어 흐름에는 callee의 종료 방식에 관한 중요한 정보가
암묵적으로 포함되어 있다. Caller가 정상적으로 다음 statement를 실행하고
있다면 callee는 정상 종료 조건을 만족한 상태로 반환한 것이다. 반대로 exception
continuation에 도달했다면 callee가 예외를 외부로 전파할 수 있는 조건이
성립했다는 뜻이다. Callee가 `abort`와 같이 프로그램을 종료했다면 해당 실행은
어느 continuation에도 도달하지 않는다.

이러한 종료 의미가 call site 이후의 분석 상태에 충분히 반영되지 않으면 이미
callee가 배제한 입력이 caller에서 다시 가능한 상태로 남는다. 그 결과 실제로는
도달할 수 없는 오류 경로가 분석에 포함되어 불필요한 경고가 발생하거나, 함수가
보장한 메모리 상태를 caller가 활용하지 못할 수 있다.

## 2. 기존 분석의 한계

예를 들어 다음 함수는 `x`가 양수일 때만 caller로 정상 복귀한다.

```cpp
void require_positive(int x) {
    if (x <= 0) {
        abort();
    }
}

void caller(int value) {
    require_positive(value);
    use(value);
}
```

`use(value)`에 도달했다는 사실은 곧 `value > 0`임을 의미한다. 하지만 이 조건이
callee 내부 변수와 제어 흐름에만 남아 있거나 함수 종료 시 과도하게 추상화되면,
caller 분석은 호출 이후에도 `value <= 0`인 상태를 유지할 수 있다.

Pointer 또는 reference parameter를 통해 변경된 메모리도 같은 문제를 가진다.
Callee가 특정 입력 조건에서 caller-visible memory를 갱신하더라도 그 관계가 함수
경계에서 사라지면 caller는 변경된 값과 최초 입력 사이의 관계를 이용할 수 없다.

이 문제의 핵심은 기존 분석기가 함수 내부 상태를 전혀 계산하지 못한다는 데 있지
않다. 기존 abstract interpretation은 numerical constraint, branch feasibility,
pointer/alias 정보, exception flow를 계산한다. 그러나 이 정보가 callee의 local
표현에 묶여 있거나 종료 경로들이 하나의 추상 상태로 합쳐지면, caller에서
재사용할 수 있는 함수 입력 기준의 종료 조건과 memory effect가 충분히 보존되지
않을 수 있다.

## 3. 제안하는 접근 방법

본 연구는 callee 내부에서 계산된 정보를 함수 진입 시점의 parameter와
caller-visible memory를 기준으로 다시 표현한 **Parameter-Origin Symbolic Exit
Summary**를 생성한다. 이 summary를 call site의 actual argument에 맞게 해석하여
caller의 normal 또는 exception continuation을 정제한다.

제안 기법은 기존 abstract interpretation을 대체하지 않는다. 기존 IKOS 분석기가
계산한 제약, pointer/alias 정보, exception semantics를 그대로 활용하면서, 현재
값이 함수 입력에서 어떻게 유도되었는지를 나타내는 symbolic relation을 보조
정보로 추적한다. 함수 종료 시에는 callee의 전체 내부 상태를 전달하는 대신
caller가 관찰할 수 있는 정보만 summary로 압축한다.

함수 `g`에 대해 생성되는 핵심 정보는 다음과 같다.

```text
NormalSummary(g)
= (정상 반환이 가능한 입력 조건,
   정상 반환 시 caller-visible memory effect)

ExceptionSummary(g)
= (예외 전파가 가능한 입력 조건,
   예외 전파 시 caller-visible memory effect)
```

프로그램 자체를 종료하는 경로는 normal 또는 exception summary에 섞지 않는다.
이를 통해 "함수가 반환하지 않는 실행"이 caller의 후속 상태에 잘못 포함되는 것을
막는다.

## 4. Caller에서의 정보 재사용

생성된 summary는 callee의 formal parameter를 caller의 actual argument에 대응시켜
call site에 적용한다. 정상적으로 반환한 흐름에는 normal summary를, 예외가
전파된 흐름에는 exception summary를 사용한다.

앞의 예에서 callee의 정상 종료 조건이 다음과 같이 요약되었다고 하자.

```text
require_positive의 정상 반환 조건: 첫 번째 입력 > 0
```

`require_positive(value)` 호출 이후에는 이 조건을 caller의 `value`에 대응시켜
기존 abstract state를 정제할 수 있다. 조건과 모순되는 상태는 제거되므로 이후
분석은 `value > 0`인 실행만 고려한다.

Pointer/reference parameter를 통해 관찰 가능한 메모리가 변경되었다면 종료 시점의
memory summary도 caller 상태에 반영한다. 따라서 caller는 단순히 "메모리가 변경될
수 있다"는 사실뿐 아니라, 표현 가능한 경우 변경된 값이 입력 parameter 또는
호출 전 memory와 어떤 관계를 가지는지도 후속 분석에 활용할 수 있다.

## 5. 기대 효과

본 연구를 통해 함수 호출 이후의 분석은 callee의 종료 의미를 다음과 같이
재사용할 수 있다.

- 정상 반환이 불가능한 입력 상태를 normal continuation에서 제거한다.
- 예외 전파가 불가능한 입력 상태를 exception continuation에서 제거한다.
- 프로그램 종료 경로가 caller의 후속 흐름으로 잘못 합류하는 것을 방지한다.
- Pointer/reference를 통해 발생한 caller-visible memory effect를 후속 분석에
  전달한다.
- Callee 내부의 관계 정보를 caller의 입력 문맥에 맞게 재사용한다.

그 결과 기존 분석에 남아 있던 infeasible state가 줄어들고, 함수 호출 뒤에서
발생하는 오탐 가능성을 낮출 수 있다. 동시에 callee의 전체 CFG나 모든 중간 상태를
caller에 복제하지 않고 요약된 정보만 전달하므로 함수 단위의 재사용 가능한
interprocedural 분석 구조를 제공한다.

## 6. 연구 범위와 원칙

현재 연구는 C/C++ 프로그램을 LLVM IR과 IKOS AR로 변환하여 분석하는 환경을
대상으로 한다. 주요 범위는 scalar affine relation, pointer/reference parameter,
parameter-relative memory, normal/exception termination condition 및 Call/Invoke
이후의 pruning이다.

분석의 soundness를 위해 정확히 표현할 수 있는 symbolic relation만 유지한다.
비선형 연산, 불명확한 pointer target, 지원하지 않는 값 또는 메모리 관계에 대해
임의의 조건을 만들지 않으며, 표현할 수 없는 정보는 보수적으로 추상화한다. 이
정책은 잘못된 pruning을 피하는 대신 일부 정밀도 향상을 포기하는 방향이다.

Global variable, 정밀한 aggregate/vector relation, return-value summary, 복잡한
alias relation은 후속 확장 대상으로 둔다. IKOS AR statement별 transfer function,
CFG merge와 loop 처리, summary 생성 및 caller 적용 규칙의 구체적인 알고리즘은
별도의 방법론 명세에서 정의한다.

본 연구의 핵심은 새로운 numerical abstract domain을 제안하는 것이 아니라,
기존 분석기가 이미 계산한 함수 내부 정보를 **함수 입력 기준의 종료 조건과
memory effect로 재구성하고, 이를 caller의 continuation에서 다시 활용하는
interprocedural mechanism**을 제공하는 데 있다.
