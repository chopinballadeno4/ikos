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

현재 범위에는 pointer parameter 값의 nullity 조건과, 그 null 비교에서 생성된
Boolean 값의 정확한 유래를 보존하는 제한적인 분기 합류 규칙이 포함된다. 예를 들어
`guard_check(p != nullptr)`가 정상 반환하면 Boolean-origin을 통해 wrapper의 종료
조건을 `p != nullptr`로 합성한다. 일반적인 경로 전체 보존은 요구하지 않는다.

함수 `g`에 대해 생성되는 핵심 정보는 다음과 같다.

Pointer parameter로 메모리에 쓰는 경우 summary에 명시적 진입 전제 P_memory를
함께 기록한다. 예를 들어 Writable(divisor, int)는 null 여부뿐 아니라 살아 있는
쓰기 가능한 객체의 크기와 정렬을 요구한다. 종료 조건과 Complete coverage는 이
전제 안에서 성립하며, caller의 호출 전 상태가 전제를 보장할 때만 조건과 효과를
재사용한다. 첫 지원 범위는 offset 0의 정수 scalar store와, 실제 인자가 유효한
타입 일치 stack scalar임을 증명할 수 있는 호출이다. 미확인 전제는 기존 분석의
flow를 유지한다(상세: DEV_SUMMARY.md §3, DEV_PRUNING.md §3.4).

```text
NormalSummary(g)
= (정상 반환을 보장하는 입력 충분조건,
   해당 flow 입력의 coverage: Partial 또는 Complete,
   보장 영역에서 정상 반환 시 caller-visible memory effect)

ExceptionSummary(g)
= (예외 전파를 보장하는 입력 충분조건,
   해당 flow 입력의 coverage: Partial 또는 Complete,
   보장 영역에서 예외 전파 시 caller-visible memory effect)
```

프로그램 자체를 종료하는 경로는 normal 또는 exception summary에 섞지 않는다.
이를 통해 "함수가 반환하지 않는 실행"이 caller의 후속 상태에 잘못 포함되는 것을
막는다.

### 3.1 종료 보장과 안전성의 방향

Summary의 충분조건 C_X는 정의된 실행을 전제로 실제로 해당 종료를 보장하는 입력만
포함해야 한다. No-wrap overflow 배제 조건은 자동 생성하지 않으며, C_X가 UB 부재를
인증하는 것은 아니다. 기존 산술 오류 검사와 소스의 명시적 범위 검사는 유지한다.
정확한 포함 관계와 nondeterminism/종료성 전제는 DEV_SUMMARY.md §1.1을 따른다.
실제 정상 종료 범위가 `[1,1000]`이라면 `[1,500]`으로 일부만 보장하는 것은
허용하고 `[1,1500]`으로 보장 범위를 넓히는 것은 금지한다. 보장 영역을 일부
놓치더라도 확인하지 못한 입력을 안전하다고 판단하지 않는다.

C_X 밖의 입력은 미확인이지 불가능한 입력이 아니다. Partial 조건을 caller의
모든 정상 실행에 강제하면 실제 오류 실행을 삭제할 수 있으므로 금지한다.
명시된 메모리 전제를 call entry 전체에서 검증하고 Complete coverage가 증명된
경우에만 종료 조건으로 전체 flow를 pruning하며,
나머지 경우 미확인 영역은 기존 IKOS 분석으로 검사한다. 전체 분석은 실제
도달 가능 실행을 모두 포함해야 하므로 false positive는 허용하고 false negative를
초래하는 근거 없는 실행 삭제는 허용하지 않는다.

### 3.2 표현식 자체의 범위 보존

개별 변수의 interval과 별도로 표현식 범위를 `ρ : ExprKey → MachineIntInterval`에
저장하며 보조 state를 `Σ=(σ,μ,ρ)`로 확장한다. `σ/μ`의 선형 값 관계 추적은
유지한다. 예를 들어 a와 b 각각의 범위가 불명확해도 검사를 통과한 합의 범위는
다음과 같이 보존할 수 있다.

```text
guard_pos(a+b) 정상 반환 뒤:
ρ(UAdd_w(ValueId(a),ValueId(b))) = [1,10000]

다음 a+b 계산 시:
같은 typed 식임을 확인 → 결과 변수의 interval을 [1,10000]으로 정제
```

실제 입력 값 identity, bitwidth 및 wrapping 의미가 같을 때만 재사용한다. 합류에서
범위를 join하고 값 재정의 시 의존 사실을 무효화한다. 단순 임시변수 liveness 제거로
합의 범위를 잃지 않으며, checker가 보는 기존 numerical state에도 범위를 반영한다.
공통 정의는 DEV_SUMMARY.md §2.7~2.10, transfer는 DEV_TRANSFER_FUNCTION.md §2.6,
caller summary 적용은 DEV_PRUNING.md §7.6을 따른다.

ρ는 현재 도달 상태의 실제 값을 모두 포함하는 범위 정보이며 종료 보장 충분조건
C_X와 구분한다. C_X의 Partial 영역을 전체 실행의 사실로 ρ에 기록하지 않는다.
단일 interval로 표현할 수 없는 signed non-zero 구멍을 별도로 저장하는 확장은
여전히 후속 범위이다.

## 4. Caller에서의 정보 재사용

생성된 summary는 callee의 formal parameter를 caller의 actual argument에 대응시켜
call site에 적용한다. 정상적으로 반환한 흐름에는 normal summary를, 예외가
전파된 흐름에는 exception summary를 사용한다.

앞의 예에서 callee의 정상 종료 조건이 다음과 같이 요약되었다고 하자.

```text
require_positive의 정상 반환 조건: 첫 번째 입력 > 0; coverage=Complete
```

`require_positive(value)` 호출 이후에는 이 조건을 caller의 `value`에 대응시켜
기존 abstract state를 정제할 수 있다. 조건과 모순되는 상태는 제거되므로 이후
분석은 `value > 0`인 실행만 고려한다.

Pointer nullity는 pointer 값 자체의 제약이다. 실제 pointee target을 알 수 없어도
caller의 actual pointer에 non-null을 적용할 수 있다. 정확한 location 해석은 memory
effect 적용의 별도 요건이며, 값의 nullity 적용과 구분한다.

Pointer/reference parameter를 통해 관찰 가능한 메모리가 변경되었다면 종료 시점의
memory summary도 유효한 보장 영역에서 caller 상태에 반영한다. Partial의 바깥에
그 memory 관계를 무조건 적용하지 않는다. 따라서 caller는 단순히 "메모리가 변경될
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
대상으로 한다. 주요 범위는 scalar affine 값 관계, typed 표현식 범위 ρ, parameter 기반 종료 조건, pointer/reference parameter,
parameter-relative memory, normal/exception termination condition 및 Call/Invoke
이후의 pruning이다. Pointer nullity와 증명 가능한 Boolean 생성 분기의 origin 보존, 알려진 비재귀 callee summary의 wrapper 합성도 현재 범위에 포함한다.

`σ/μ`의 선형 값 관계와 종료 조건의 표현 범위를 구분한다. 예를 들어
`n₀ % 2 == 0`은 typed 종료 조건으로 보존할 수 있다. `r=n%2`의 r이 affine
관계로 표현되지 않더라도 SSA 연산 출처를 통해 조건을 복원한다. `%`와 `/`의
signedness, bitwidth 및 정의 가능성은 DEV_SUMMARY.md §2.6을 따른다.

29번은 조건부 대입의 각 guard와 값을 따라 마지막 `aligned>=2` 검사를 DNF에
치환하여 보존한다. 30번은 두 operand가 변수인 `UMul_32(step₀,idx₀)<=9`를 typed
조건으로 그대로 보존한다. 두 검사는 다른 입력 범위에서 함의되더라도 저장·출력하는
summary에서 제거하지 않는다. 같은 atom의 중복 제거만 허용하며, 중복 제거 증명이나
일반 비선형 solver가 있어야 summary를 생성할 수 있다고 요구하지 않는다.
σ의 affine 값 관계 제한과 종료 조건의 표현 범위를 구분하며, 일반적인 ITE 값
상태를 추가하지 않아도 분기별 대입을 치환할 수 있다. 공통 정의와 정확한 조건식은
DEV_SUMMARY.md §5.3·5.4, 복원 규칙은 DEV_TRANSFER_FUNCTION.md §2.5.1·2.5.2,
검증 기준은 DEV_PRUNING.md §14.3을 따른다.

미지원 연산이나 불명확한 origin 때문에 필수 조건을 복원하지 못하면 해당 atom을
true로 바꾸어 종료 보장 범위를 넓히지 않는다. 더 강한 영역을 증명하거나 해당
conjunction의 보장 기여를 생략하여 Partial로 남긴다. 불명확한 memory relation은
TOP/invalidation으로 처리한다. 이는 종료 보장 영역의 축소와 도달 가능 상태의
보수적 확대를 구분하는 정책이다.

Global variable, 정밀한 aggregate/vector relation, return-value summary, 복잡한
alias relation은 후속 확장 대상으로 둔다. IKOS AR statement별 transfer function,
CFG merge와 loop 처리, summary 생성 및 caller 적용 규칙의 구체적인 알고리즘은
별도의 방법론 명세에서 정의한다.

본 연구는 기존 numerical domain과 표현식 범위 상태 ρ를 결합한다. 함수 내부 정보를
입력 기준의 종료 조건과 memory effect로 재구성하고 caller의 continuation에서
재사용하는 구조를 정의한다.

현재 완료 기준은 benchmark 07/09의 nullity warning 제거와, null에서도 반환 가능한
OR guard 및 guard 없는 nullable pointer의 warning 유지이다. 보조 state는 Σ=(σ,μ,ρ)를
사용하며 null/pointer/Boolean의 타입 의미와 표현식 범위를 구분한다. 구체적인 공통 정의와 제한은
DEV_SUMMARY.md §2.5·9, transfer 규칙은 DEV_TRANSFER_FUNCTION.md §2.4·3.2·8.1,
caller 적용은 DEV_PRUNING.md §5~7을 따른다. 22번의 동일 합 재계산도 ρ의 필수 검증에 포함한다.

## 7. 명세와 구현 범위

종료 조건 전용 typed expression, coverage 및 flow별 표현식 범위 ρ를 구현한다.
29번의 분기별 reaching definition과 30번의 변수 곱셈을 조건 AST로 보존하며,
σ/μ의 affine 값 관계와 구분한다. 미지원 조건은 true로 약화하지 않고 해당 보장
경로를 인증 대상에서 제외하여 Partial로 남긴다.

예외 검증에서는 알려진 AR libcpp intrinsic을 필수 지원한다. 정수 예외와 검증된
클래스 생성, 명시적 throw, catch-all, cleanup/resume 및 rethrow를 경로별로 처리하여
normal과 exception 조건을 각각 수집한다. 클래스 생성자는 본문 분석 또는 확인된
표준 라이브러리 모델을 사용한다. 정상 생성 뒤 throw하는 경로와 생성자 자체가
예외를 전파하는 경로가 모두 같은 종료를 보장해야 그 입력을 인증한다. Destructor는
실제 파괴 시점에 검증하며, unknown 생성/정리 또는 typed catch에는 flow별 Partial
fallback을 적용한다. 미지원 경로의 모든 CFG continuation에서 정상 복귀가 불가능함을
증명하면 normal coverage를 유지하고 exception coverage만 Partial로 남길 수 있다.
정상 복귀할 수 있는 catch는 이 예외에 해당하지 않는다. 상세 계약과 library/runtime
모델의 범위는 DEV_TRANSFER_FUNCTION.md §7.1~7.2·8.2.1을 따른다.

Caller 적용에서는 summary 표현 가능성과 domain 정제 가능성을 구분한다.
변수 ± 상수 및 일부 변수끼리의 add/sub, 조건부 상수 곱셈·양의 상수 나눗셈,
정보를 잃지 않는 cast의 역방향 interval projection을 지원한다. 개별 변수를 안전하게
좁힐 수 없는 경우에는 그 정제만 생략하고 summary 조건과 ρ의 식 범위를 보존한다.
정확한 적용 규칙은 DEV_TRANSFER_FUNCTION.md §2.7을 따른다.

종료 보장 인증은 유한 경로 탐색으로 수행한다. 함수당 block-path 처리 1024회,
경로별 같은 block 방문 16회를 한도로 하며, 한도 초과나 미확인 호출·메모리·예외는
Partial fallback을 사용한다. 이는 일반적인 loop 종료성 증명이나 disjunctive 값
domain의 구현을 뜻하지 않는다. 구체적인 구현 경계는 DEV_PRUNING.md §13.2,
필수 검증은 같은 문서 §14를 따른다.
