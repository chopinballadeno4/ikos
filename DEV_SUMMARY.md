# IKOS AR 기반 Function Summary 생성 명세

## 1. 목적

본 문서는 IKOS AR을 기반으로 함수(callee)를 분석하여 caller에게 전달할 `FunctionSummary`를 생성하는 방법을 정의한다.

본 문서의 범위는 다음과 같다.

- symbolic state `Σ = (σ, μ, ρ)`를 이용한 함수 내부 분석
- CFG branch/merge 처리
- loop fixpoint 처리
- normal / exception / program exit 분리
- 정상/예외 종료 조건의 parameter-origin 표현식 환원
- caller-visible memory effect 추출
- FunctionSummary 생성 및 관리 원칙

다음 내용은 본 문서의 범위에서 제외한다.

- caller call site에서 `C_normal`을 실제로 적용하는 방법
- callee parameter와 caller argument 치환의 세부 알고리즘 (합성 시 DEV_PRUNING.md를 참조)
- caller normal-flow pruning
- caller memory state에 `μ_exit`를 반영하는 방법

위 내용은 별도의 Caller Summary Application / Pruning 명세에서 정의한다.

### 1.1 종료 보장 조건의 포함 방향과 안전성

본 명세의 `C_X(f)`는 입력이 조건을 만족하면 flow `X`로 종료함을 보장하는
**충분조건**이다. 단순히 해당 flow가 가능하다는 필요조건으로 해석하지 않는다.
`X`는 normal 또는 exception이다. 입력에는 parameter와 필요한 entry-memory 및
환경 전제가 포함되며, 모든 보장은 명시한 전제와 **정의된 실행의 의미론** 아래에서
성립해야 한다. C_X는 함수가 UB 없이 실행됨을 증명하는 안전성 인증서가 아니다.

```text
DefInputs(f)
    = 정의된 실행이 존재하는 입력 집합 (무한 실행도 실행에 포함)
GuaranteedExitInputs_X(f)
    = DefInputs(f) 중 모든 허용된 정의된 실행이 유한 시간 안에
      flow X에 도달하는 입력 집합
PossibleExitInputs_X(f)
    = 해당 입력에서 flow X에 도달하는 정의된 실행이 존재하는 입력 집합

[[P_memory(f) ∧ C_X(f)]] ∩ DefInputs(f)
    ⊆ GuaranteedExitInputs_X(f) ⊆ PossibleExitInputs_X(f)
```

이 의미론은 no-wrap 산술의 overflow를 일으키는 실행을 종료 보장 대상에서 제외한다.
따라서 no-wrap add/sub/mul을 보았다는 이유로 `n+bias<=INT64_MAX`,
`3*n<=INT64_MAX` 같은 입력 제약을 생성·저장·출력하지 않는다. DefInputs는
caller가 입증해야 하는 추가 Requires atom이 아니라 보장의 의미론적 범위이다.
정의되지 않은 실행의 종료 종류에 대해서는 C_X가 보장하지 않는다. Overflow 등
오류 검사는 기존 IKOS transfer/checker가 계속 수행하며, C_X 또는 Complete를
근거로 이미 실행한 연산의 안전성을 소급 인증하거나 오류 검사를 생략하지 않는다.

결정적인 guard 함수에서 실제 정상 종료 입력이 `1 <= n <= 1000`이라면
`1 <= n <= 500`은 허용하지만 `1 <= n <= 1500`은 허용하지 않는다.
보장할 수 있는 입력을 일부 놓치는 것은 허용하며, 보장하지 못하는 입력을
종료가 보장된 입력으로 포함하는 것은 금지한다. `C_X`가 나타내는 집합은
P_memory와 DefInputs 안에서의 under-approximation이다. 분석기 전체의 도달 가능
상태는 여전히 정의된 실제 실행을 모두 포함하는 over-approximation이어야 하며,
false positive는 허용한다.

각 flow는 조건과 별도로 coverage를 가진다.

```text
Coverage_X ∈ {Partial, Complete}
Complete ⇒ (PossibleExitInputs_X(f) ∩ [[P_memory(f)]]) ⊆ [[C_X(f)]]
```

`Partial`은 충분조건이 유효하지만 해당 flow의 모든 입력을 포괄한다고 증명하지
못한 상태이다. `Complete`는 명시된 P_memory 안에서 반대 포함까지 증명한 상태이며,
그 전제와 DefInputs 안에서 종료 입력과 C_X가 일치한다. 메모리 전제는 없으면
true이며 §3에서 정의한다.
경로를 일부 누락하거나 loop/call의 의미를 분석하지 못하면
자동으로 Complete가 되지 않는다. 분석기 invariant가 over-approximation이라는
사실만으로 충분조건이나 종료성, coverage가 증명되지는 않는다. Nondeterministic
call, 미확인 예외 또는 무한 loop를 무시하여 종료를 보장하지 않는다. No-wrap의
overflow 가능성만으로 coverage를 Partial로 낮추거나 임의의 입력 상한을 붙이지 않는다.

부분 조건의 밖은 **미확인 영역**이다. `¬C_X`를 해당 flow의 불가능성으로
해석하거나 그 영역을 caller 분석에서 삭제해서는 안 된다. Caller에서 전역
pruning하려면 Complete 증명 또는 별도의 유효한 필요조건 증명이 있어야 한다.
본 명세의 기본 적용은 Complete일 때만 조건으로 flow를 pruning하고, Partial이면
call-entry 전체가 C_X에 포함됨을 증명한 경우에만 그 영역의 보장 관계를 사용한다.
그 외에는 기존 IKOS semantics를 유지한다(DEV_PRUNING.md §3·7).

구현에서는 σ/μ와 별도로 typed 조건 AST와 종료 보장 인증을 사용하고,
Complete 조건만 기존 flow 전체의 pruning에 적용한다. 인증하지 못한 경로는
Partial로 남겨 기본 IKOS 분석을 유지한다. 표현식 범위 ρ는 numerical state와
연계하여 동일한 계산 결과의 재사용에 적용한다. 유한 인증 탐색의 한도와
미지원 호출·메모리·예외의 처리 범위는 DEV_PRUNING.md §13.2를 따른다.


## 2. Symbolic State

함수 분석 중 다음 symbolic state를 유지한다.

```text
Σ = (σ, μ, ρ)
```

### 2.1 σ

```text
σ : V_SSA → Expr ∪ Loc ∪ {Null} ∪ BoolExpr ∪ {TOP}
```

`σ`는 AR SSA variable의 값을 함수 entry 시점의 parameter-origin expression 또는 symbolic location으로 표현한다.

예:

```text
σ(%1) = a₀
σ(%2) = L₂
σ(%3) = a₀ + 1
```

### 2.2 μ

```text
μ : Loc → Expr ∪ Loc ∪ {Null} ∪ BoolExpr ∪ {TOP}
```

`μ`는 symbolic memory location에 저장된 값을 나타낸다.

예:

```text
μ($3) = a₀
μ(L₂) = a₀ + 1
```

### 2.3 Pointer State

별도의 points-to symbolic state `π`는 사용하지 않는다.

Pointer/reference 관계는 `σ`와 `μ`를 통해 표현한다.

### 2.4 Symbolic Location

```text
Loc ::= B | Loc(B, δ)
```

구현 및 formal specification에서는 source-level parameter 이름에 의존하지 않는다.

예를 들어 `L_p`, `L_q`와 같은 표기는 설명용 예제에서는 사용할 수 있지만, 실제 명세 및 구현에서는 AR parameter identity 또는 parameter index를 기반으로 한다.

예:

```text
L₀
L₁
L₂
```

또는 이에 대응하는 내부 parameter identity를 사용한다.


### 2.5 Pointer value와 Boolean origin

`Σ=(σ,μ,ρ)`와 별도 `π`를 사용하지 않는 원칙은 유지한다. `Loc`은 memory key로
사용되며, pointer-valued `σ`/`μ` entry에서는 `PtrValue(Loc)`의 저장 표현으로도
사용한다. 두 용도의 의미를 구분한다.

```text
P_i^0 = PtrValue(L_i)    // entry pointer parameter의 값, nullable
PtrExpr ::= Null | PtrValue(Loc)
BoolExpr ::= IsNull(P_i^0) | NonNull(P_i^0)
```

`L_i`의 존재는 non-null, 유효한 allocation 또는 정확한 points-to target의 증거가
아니다. `Null`은 integer zero나 `TOP`과 구분한다. 현재 nullity summary는 offset이
0인 parameter-origin pointer만 지원한다. 일반 pointer comparison, derived pointer의
nullity 및 임의의 Boolean formula를 환원하지 못하면 §6의 보장 실패 규칙을 따른다.

Pointer parameter는 기존 parameter-relative `Loc`으로 초기화하되 pointer value로
해석할 수 있어야 한다. Integer parameter는 기존 `Expr`로 초기화하며, unsigned
1-bit parameter에는 값이 `{0,1}`이라는 정보를 함께 유지한다. `BoolParam(i)`는
이 typed integer entry atom의 논리적 표기이다. Null constant는 `Null`로 평가한다.

`BoolExpr`는 정확한 `{0,1}` encoding의 값 관계이다. `σ(b)=NonNull(P_i^0)`는
현재 경로에서 pointer가 non-null이라고 가정한다는 뜻이 아니라,
`b == 1 ⇔ P_i^0 != null`이라는 값의 유래를 뜻한다. Assignment/copy 및 정확한
Boolean encoding 보존 cast를 통해 전달할 수 있다. Memory에 저장된 값도 확장된
SymbolicValue를 사용할 수 있으나 기존 invalidation 정책을 따른다.

### 2.6 선형 값 관계와 종료 조건 표현의 분리

`Expr`는 `σ/μ`에서 사용하는 parameter-origin affine 값 관계이다. 이 제한이
종료 조건의 표현 범위를 affine comparison으로 제한한다는 뜻은 아니다.
종료 조건의 수치 피연산자에는 별도의 typed `CondExpr`를 사용한다.

```text
CondExpr ::= EntryScalar | EntryMemory | Constant | AffineExpr
           | TypedOp(op, bitwidth, signedness, CondExpr...)
           | Ite(Condition, CondExpr, CondExpr)
Atom ::= Compare(predicate, CondExpr, CondExpr)
       | IsNull(P_i^0) | NonNull(P_i^0)
Condition ::= true | false | Atom | Condition AND Condition | Condition OR Condition
```

초기 필수 범위는 affine 비교, wrapping UAdd 및 0이 아닌 양의 상수 divisor에 대한 typed
`SDiv/UDiv/SRem/URem` 및 그 결과의 비교이다. 29번의 wrapping USub와 acyclic
조건부 대입의 분기별 origin 복원, 30번의 변수끼리의 wrapping UMul도 필수 범위다.
이들은 affine σ에 저장하지 못해도 종료 조건에는 보존한다. 그 외 TypedOp/ITE는 각 연산의
의미와 origin을 정확히 복원할 수 있을 때만 지원한다. 지원하지 않는 연산을
임의의 affine 식으로 바꾸지 않는다. Signed/unsigned, bitwidth, cast 및 wrapping
의미를 유지한다. 특히 signed remainder는 수학적 modulo와 무조건 동일시하지
않으며, 0 divisor 및 signed MIN / -1, MIN % -1 등 정의되지 않는 연산을
보장 영역에 포함하지 않는다.

`σ(r)=TOP`이어도 read-only SSA definition/reaching-definition origin을 따라
`r=n srem 2`와 `σ(n)=n₀`를 확인하면 `r==0`을 `SRem_w(n₀,2)==0`으로 복원한다.
연산 출처는 조건 복원을 위한 근거이며, `σ`의 선형 값 관계로 저장할 필요는 없다.
여러 definition이 있으면 해당 경로의 정의 또는 모든 관련 정의의 동일한 의미를
증명해야 한다. Guarded value는 양쪽 origin과 guard를 확인한 경우 분기별 조건부
치환으로 DNF를 구성한다(§5.3). ITE는 동일한 의미의 내부 표현으로 사용할 수 있으나
일반적인 ITE 값 상태를 σ에 추가할 필요는 없다. 순환 정의, side effect, 모호한
load/alias 또는 미확인 origin은 복원 실패다.
이는 별도의 π나 일반적인 symbolic path-state를 도입하는 것이 아니다.

### 2.7 표현식 범위 상태 ρ

개별 변수의 범위와 별도로, **같은 값을 계산하는 typed 표현식 자체의 범위**를
보존한다. 전체 분석 상태는 `(A, Σ)`이며 `A`는 기존 IKOS state,
`Σ=(σ,μ,ρ)`이다. `σ/μ`의 affine 값 관계 제한은 유지한다.

```text
ρ : ExprKey ⇀ MachineIntInterval
ρ(E) = I  // 현재 flow에서 E의 가능한 값이 I에 포함됨
missing key = typed TOP interval
ρ_entry = ∅

모든 실제 도달 상태 s에서:
    EvalMachine(E, s) ∈ γ(ρ(E))
```

`ρ`는 numerical state와 마찬가지로 실제 값을 모두 포함하는 범위 정보이다.
종료 보장 충분조건 C_X의 under-approximation과 포함 방향이 다르다. ρ의 interval
또는 그 hull을 충분조건 C_X로 재저장하거나, interval 안의 모든 입력이 안전하다고
결론내리지 않는다. 초기화·정의 가능성·오류 검사는 기존 분석과 동일하게 유지한다.
유효한 범위의 교차가 bottom이면 해당 flow는 bottom이다.

### 2.8 ExprKey와 값 동일성

```text
ExprKey ::= ValueId(v) | TypedConstant(c)
          | OpKey(opcode, result/operand types, semantic flags, ExprKey...)
E22 = UAdd_w(ValueId(a), ValueId(b))
```

Key는 출력 문자열이나 source 변수명이 아니라 실제 operand의 정의/도달 정의에
기반한 값 identity를 사용한다. `get_a()`와 `get_b()`의 결과가 모두 σ에서 TOP이어도
서로 다른 ValueId이다. 같은 함수의 별도 call 결과도 서로 다른 값이다. Parameter로
환원되지 않는 caller-local 값도 key의 leaf가 될 수 있다.

Bitwidth, signedness, operand/result type, cast, wrapping 및 no-wrap/exact 등의
의미를 보존한다. Unsigned add는 modulo 2^w 덧셈이며 수학적 affine 식과 혼동하지
않는다. Copy와 증명된 동일 연산만 같은 key로 정규화한다. 동일한 typed unsigned
add의 operand 순서 정규화는 가능하지만, 근거 없는 재결합·분배·cast 제거는 금지한다.
초기 필수 범위는 정수 값 identity/copy와 wrapping UAdd를 포함한 순수 덧셈 및
그 식과 상수의 비교이다. 다른 연산은 정확한 key와 sound interval transfer를
지원할 때만 추가한다. `CondExpr`와 의미를 공유하지만 함수 공통 조건의 leaf는
entry atom이고, 로컬 ExprKey의 leaf는 현재 함수의 값 identity일 수 있다.

Key 생성에는 §2.6과 같은 origin 검증을 사용한다. 여러 reaching definition이나
PHI 대입을 하나로 해석할 수 없으면 동일성을 가정하지 않는다. AR variable이
재대입되거나 loop에서 같은 definition이 재실행되면 새 실행의 값을 이전 값과
동일시하지 않고 관련 key를 무효화한다. 무한한 실행별 key를 생성하지 않는다.

초기 범위의 key는 mutable memory read, volatile/atomic read 및 call 자체를
순수 식으로 취급하지 않는다. 그 결과를 각각 별도의 ValueId leaf로 고정할 수는
있다. 동일 주소를 다시 load했다고 이전 값과 같다고 가정하지 않는다. Memory를
직접 포함한 key의 지원에는 별도 memory-version/alias 증명이 필요하다.

### 2.9 ρ의 갱신·병합과 수명

- 현재 flow 전체에서 성립하는 numerical invariant, branch comparison 또는 적용이
  허용된 summary 조건에서만 범위를 얻는다. C_X가 Partial인 경우에는 기존
  §1.1의 적용 영역 증명 없이 그 조건을 ρ에 넣지 않는다.
- 같은 식에 대한 추가 제약은 interval 교차로 정제한다. `a+b`의 범위를 얻었다고
  a와 b의 개별 범위를 임의로 결정하지 않는다.
- `r=E`를 다시 계산하면 sound interval transfer 결과와 `ρ(E)`를 교차하여 기존
  numerical state의 r을 정제한다. 이 결과는 checker가 읽는 A에 전달해야 한다.
- 결과 임시변수만 liveness에서 제거되어도 정규화된 E의 사실은 유지할 수 있다.
  입력 값 identity가 바뀌거나 재사용되면 그 값에 의존하는 key를 전이적으로
  무효화한다. Key의 의미나 origin을 확인하지 못하면 사실을 버려 TOP으로 둔다.
- CFG 합류에서는 각 incoming flow의 같은 의미의 key를 interval hull/join으로
  병합한다. 한 도달 경로에서 key가 없으면 TOP과 join한다. Branch별 범위를
  교차하거나 한 경로의 사실을 전체에 강제하지 않는다. Unreachable predecessor는
  join에서 제외한다. PHI key 변환은 동일성 증명이 있을 때만 허용한다.
- Key 수와 origin 깊이를 유한하게 제한하며 제한을 넘으면 해당 사실을 생략한다.
  Loop에서는 σ/μ의 flat merge와 별개로 ρ에 interval join/widening을 적용한다.
  Narrowing은 sound한 transfer/재분석으로 증명한 경우에만 적용한다.

`ρ`는 basic block 및 normal/exception flow별 분석 상태이다. Mutable한 함수 공통
cache에 한 경로의 범위를 저장하여 다른 경로나 call context에 재사용하지 않는다.
식의 불변 구조를 intern하는 table과 flow별 range map은 구분한다. 로컬 ValueId를
포함한 ρ 전체를 FunctionSummary에 저장하지 않는다. Entry atom으로 환원되는 사실도
최종 종료 보장 검증의 보조 근거일 뿐, 그 사실만으로 summary 충분조건이 되지 않는다.

ρ는 단일 interval만 저장한다. 예를 들어 unsigned E에 `E!=0`을 적용하면
`[1, UINT_MAX]`를 보존할 수 있지만 signed E의 `[-10,10]`에서 0만 제외하는 구멍은
저장하지 않는다. 앞서 정한 NE 정밀도 수용 및 향후 zero/non-zero 확장은 유지한다.

### 2.10 Benchmark 22의 기준

```text
t1 = UAdd_w(a, b)
guard_pos(t1)                  // C_N: n₀!=0 AND n₀ u<=10000; Complete
ρ(UAdd_w(ValueId(a), ValueId(b))) = [1,10000]

t2 = UAdd_w(a, b)             // a와 b의 값 identity가 유지됨
A(t2) := A(t2) ∩ ρ(UAdd_w(ValueId(a), ValueId(b)))
       ⊆ [1,10000]
100 udiv t2                   // 분모의 0 가능성 없음
```

a와 b 각각의 범위를 알 필요는 없다. 두 덧셈에 wrapping이 있어도 같은 typed
연산과 같은 값 identity이므로 결과는 같다. `σ(a)=σ(b)=TOP` 또는 t1의 liveness
제거 때문에 E의 사실을 잃어서는 안 된다. 다만 a/b의 값 변경, guard를 거치지 않은
incoming flow 또는 연산 의미 차이가 있으면 위 재사용을 보장하지 않는다.


### 2.11 표현식 조건의 domain 적용 가능성

조건을 summary에 표현할 수 있는지와, 그 조건으로 caller 변수의 interval을
정제할 수 있는지는 별개의 판정이다. `a-b`가 summary에 남는다고 a와 b 각각의
범위를 항상 좁힐 수 있는 것은 아니다. 반대로 한쪽 범위가 충분히 알려지면
변수끼리의 연산도 안전한 개별 범위를 유도할 수 있다.

적용 가능한 Complete 조건 또는 적용 영역이 증명된 조건에서 `ρ(E)`를 얻으면,
DEV_TRANSFER_FUNCTION.md §2.7의 연산별 역방향 projection으로 기존 numerical
state를 교차 정제한다. 동일 폭 sign cast와 확장, add/sub, 조건부 상수 곱셈 및
양의 상수 나눗셈을 지원한다. Wrap으로 해가 갈라지면 이를 모두 포함하는 interval을
사용한다. 미지원 연산이나 계산 한도에서는 해당 피연산자의 기존 범위를 유지한다.

이 projection은 실제 가능한 값을 버리지 않는 over-approximation이다. 그 결과를
종료 보장 충분조건 C_X로 재저장하지 않는다. Caller에서 적용 불가한 atom도 원래
summary에서는 삭제하거나 true로 약화하지 않으며, ρ의 동일 표현식 재사용도 유지한다.
σ/μ의 affine 값 관계 정의는 바뀌지 않는다.

## 3. Function Summary

함수 `f`의 summary는 다음과 같이 구성한다.

```text
FunctionSummary(f) = {
    P_memory(f),
    NormalSummary(f),
    ExceptionSummary(f)
}
```

`P_memory`는 함수 진입 시 확인해야 하는 명시적인 메모리 전제의 conjunction이다.
전제가 없으면 true다. `Writable(P_i, T, size, alignment)`는 해당 pointer가 살아
있는 쓰기 가능한 T 객체의 요구 범위를 올바른 정렬로 가리킨다는 뜻이다. NonNull은
이 전제를 대신하지 못한다. 전제를 가진 summary의 C_X와 Complete는 모두
`P_memory`와 정의된 실행 안에서만 성립한다. 실제 종료 보장 영역은
`[[P_memory ∧ C_X]] ∩ DefInputs`이며,
전제 밖에서는 종료 조건도 memory effect도 재사용하지 않는다.

```text
[[P_memory ∧ C_X]] ∩ DefInputs ⊆ GuaranteedExitInputs_X
Coverage_X = Complete ⇒ (PossibleExitInputs_X ∩ [[P_memory]]) ⊆ [[C_X]]
```

현재 필수 지원은 offset 0의 정수 scalar pointer parameter에 대한 non-volatile
store다. 타입은 formal pointee와 일치하고 byte 크기는 양수여야 한다. 경로에서
필요한 전제는 함수 공통 P_memory에 모두 모은다. 경로별로 더 약한 전제를 만드는
최적화는 필수가 아니다. 미지원 외부 load, 불명확한 offset/타입, 일반 alias에
의존하는 조건은 기존 Partial fallback을 유지한다.

Benchmark 37의 계약은 다음과 같다(소스의 fail=true 경로가 throw한다).

```text
P_memory = Writable(divisor₀, int, sizeof(int), alignof(int))
C_normal = (fail₀ == false), μ_normal = { *divisor₀ ↦ 10 }
C_exception = (fail₀ == true), μ_exception = { *divisor₀ ↦ 5 }
Coverage_normal = Coverage_exception = Complete within P_memory
```

Normal summary:

```text
NormalSummary(f) = {
    C_normal(f),
    Coverage_normal(f),
    μ_normal_exit,f
}
```

Exception summary:

```text
ExceptionSummary(f) = {
    C_exception(f),
    Coverage_exception(f),
    μ_exception_exit,f
}
```

각 flow summary는 다음 가용 상태를 갖는다.
아래 Known 표기는 FunctionSummary의 공통 P_memory 안에서 해석한다. 전제가
있는 false/Complete도 전제 밖의 flow 불가능성을 의미하지 않는다.

```text
Summary_X = Known(C_X, Coverage_X, μ_X) | Unavailable
Known(false, Partial, ∅)  // 보장 영역을 얻지 못함; flow 불가능이라는 뜻이 아님
Known(false, Complete, ∅) // 해당 flow가 불가능함을 증명함
Known(true, Complete, μ)  // 모든 허용 입력에서 해당 종료를 보장함
```

정상 종료 CFG가 없고 다른 normal-return 경로도 없음을 증명하면 normal flow를
`Known(false, Complete, ∅)`로 표현한다. 분석하지 못한 flow는 Partial 또는
Unavailable이며, 이를 Complete의 false로 단정하지 않는다.

특히 명시적인 exception-exit이 없거나 함수 내부에 일반 `Call`이 있어 그 call의
외부 전파 예외를 보조 symbolic 분석이 모델링하지 못하면 exception summary는
Partial 또는 Unavailable이다. 인증된 영역이 없으면 `Known(false, Partial, ∅)`를
사용할 수 있고, 독립적으로 증명한 예외 영역이 있으면 그 조건을 Partial로 저장한다.
어느 경우에도 미확인 영역의 기존 IKOS exception state를 유지한다.

Importer가 식별한 예외 intrinsic은 unknown call fallback과 구분하여 필수 지원한다.
지원된 정수/클래스 예외의 할당/초기화, throw, catch-all, cleanup/resume 및 rethrow의
조건 수집 규칙과 AR runtime 모델의 범위는 DEV_TRANSFER_FUNCTION.md §7.1~7.2를 따른다.
이 범위의 명시적 throw를 일반 미지원 호출로 버리고 `false/Partial`로 남긴 결과를
지원 완료로 보지 않는다. Typed catch, 미확인 생성자/cleanup 등 미지원 영역에는
flow별 fallback을 적용한다. 미지원 경로가 정상 복귀할 수 없다는 별도 CFG 증명이
있으면 normal coverage는 유지할 수 있지만 exception coverage는 Partial로 남긴다.
상세 규칙은 DEV_TRANSFER_FUNCTION.md §8.2.1을 따른다. 클래스 예외의 종류만으로 제외하지 않는다. 생성자는
본문의 제한적 문맥 분석 또는 검증된 declaration 모델로 인증하며, normal 생성
후 throw와 생성자 실패의 예외 전파를 모두 확인한다. Nondeterministic 결과의
충분조건은 결과별 보장 영역의 AND이고, 가능한 종료가 다르면 Complete로 내보내지
않는다. Destructor는 escaping throw 이후까지 지연되므로 종료 조건과 cleanup
안전성을 구분한다. Complete exception summary에서만 전체 객체의 정상 cleanup
계약을 다음 caller에 전달하며, 미확인 destructor를 만난 catch 종료는 Partial이다.

Program exit는 caller로 복귀하지 않으므로 caller 적용을 위한 summary를 생성하지 않는다.

현재 연구에서는 return value summary를 생성하지 않는다.


## 4. Exit 종류

함수의 종료를 다음과 같이 구분한다.

```text
normal_exit
exception_exit
program_exit
```

### 4.1 Normal Exit

`ReturnValue` 또는 정상적인 함수 return에 해당한다.

정상 종료 경로에서 다음 정보를 생성한다.

```text
C_normal
μ_normal_exit
```

### 4.2 Exception Exit

외부로 exception이 전파되는 종료이다.

대표적으로 AR의 `Resume`이 이에 해당한다.

다음 정보를 생성한다.

```text
C_exception
μ_exception_exit
```

### 4.3 Program Exit

`abort()` 이후 `unreachable` 등 프로그램 자체가 종료되는 경로이다.

이 경로는 caller로 복귀하지 않으므로 caller용 memory summary를 생성하지 않는다.


## 5. Exit Condition 생성

각 종료 경로의 branch condition, 정의된 연산의 전제 및 callee의 보장 조건을
parameter-origin 조건으로 환원한다. 먼저 `σ`의 정확한 관계를 사용하고, affine
관계로 표현하지 못한 연산은 §2.6의 SSA origin으로 복원한다. 최종 numerical
invariant만을 충분조건의 증거로 사용하지 않으며, liveness로 지워진 제약이 최종
invariant에 남아 있다고 가정하지 않는다.

`σ`의 affine 후보는 operand의 실제 bitwidth/sign과 reaching definition에 대해
값 동치를 확인한 뒤 우선 사용한다. 예를 들어 `s1=3*n`, `s2=2*n`, `s3=s1-s2`,
`s0=n+bias`, `s4=s3+bias-s0`이면 정의된 실행에서 `σ(s3)=n`, `σ(s4)=0`이므로
`leaf_check(s3+s4)`의 actual은 `n`이다. 이미 환원된 값을 원래 SSA 트리로 다시
확장하지 않는다. Cast/truncation 경계는 임의로 지우지 않으며, 동치를 확인하지
못하면 typed origin을 사용한다. Affine 계수의 구현상 표현 한도를 넘으면 TOP으로
내리고 typed origin으로 복원하며, 계수 overflow를 관계로 저장하지 않는다.

`σ=TOP`인 wrapping 연산도 복원된 CondExpr를 단순화할 수 있다. 같은 width/sign의
add/sub와 상수 배 mul을 `Z/(2^w)`에서 계수·상수항으로 모아 상쇄하고, 더 작은
동치 typed 식을 사용한다. 예를 들어 32비트 unsigned의 `(c+4)-4`와 `2*r-r`은
wrap 발생 여부와 관계없이 각각 `c`, `r`이다. 비교를 생성할 때와 callee 조건을
caller actual로 치환한 뒤에도 적용한다. 이 결과는 typed 조건/값 origin에 사용하고,
wrapping 중간값을 수학적 affine σ에 저장하도록 σ의 정의를 넓히지는 않는다.

Cast는 그 operand 내부에서 먼저 단순화하되 cast 경계 너머의 항을 합치지 않는다.
나눗셈·나머지·변수끼리 곱셈 등은 typed opaque atom으로 보존하여 정확히 같은 항만
상쇄한다. `(n%3 + n*k + 4) - n*k - 4`는 `n%3`이지만, wrapping `2*n<8`을
`n<4`로 바꾸지 않는다. 이미 간단한 `idx-1`, `step*idx`는 형태를 유지한다. 이 변환은
동치인 피연산자로의 교체이며, 원래 검사를 근거 없이 생략하거나 비교 항을 이항하는
규칙이 아니다. 상수 비교로 환원되면 기존 상수 평가를 적용한다. 복원 순회와 계수 수집은
각 512 node, 서로 다른 atom은 128개로 제한하며 한도/재구성 실패에서는 원래 식을
유지한다. 44번 check_row/check_col은 각각 `r<6`, `c<8`로 요약해야 한다.

값 관계의 단순화는 정의된 실행의 의미론 안에서 적용한다. No-wrap 연산의 안전성을
무조건 보장하는 입력 영역을 별도로 만들지 않으며, 두 배 width의 overflow 배제
atom도 생성하지 않는다. 43번 mid_chain과 outer_guard는 정상 조건 `0<n<=200`,
예외 조건 `n>200`을 요약한다. 원래 경로의 `n>0 && n>200`은 아래의 scalar 조건
단순화로 `n>200`으로 저장한다. `n+bias<=INT64_MAX`나 `3*n<=INT64_MAX`를 자동으로
붙이지 않는다. 반면 소스에 명시한 `if(n>INT64_MAX/3)` 검사는 실제 flow를 결정하므로
복원하며, 다른 조건과의 논리적 동치가 증명된 경우에만 중복을 제거한다.
Unsigned wrapping은 정의된 연산이므로 modulo 의미를 유지하고, truncation이나
29/30번의 원래 검사 atom도 삭제하지 않는다. 미지원 값 관계/호출/메모리/loop를
정의된 실행 전제라는 이유로 생략하지 않으며, 기존 Partial fallback을 유지한다.

```text
Candidate_X,i = RestoreCondition(PathConditions_X,i, σ_i, SSAOrigins)
C_X,i = CertifyExit(Candidate_X,i, path/call/loop semantics)
C_X   = OR { C_X,i | 종료 보장이 검증된 영역 i }
```

`CertifyExit`는 단순한 구문 치환과 구분한다. 해당 조건의 모든 정의된 실행에서
경로 선택과 값 관계/effect, call의 종료 보장 및 필요한 loop 종료성이 성립해야 한다.
불완전한 분석 결과에 도달 가능한 exit가 있다는 사실만으로 경로 전체를 인증하지
않는다. 해당 영역의 모든 정의된 실행이 flow X로 종료한다는 증거가 없으면 영역을
기여시키지 않는다.

Callee 보장 조건을 정확히 치환할 수 있으면 call 이전의 보장 영역과 논리곱하여
합성한다. Callee가 Partial이면 그 보장 영역 밖을 제거하지 않으며, caller의
coverage는 별도로 증명한다.

```text
C_after_call,X = C_before_call AND θ_c(C_X(callee))
```

이 식은 해당 call을 거쳐 보장되는 영역의 합성이며 caller 전체 조건을 이 영역으로
제한하여 나머지 실행을 삭제한다는 뜻이 아니다. 이후 statement와 최종 exit까지의
보장도 별도로 확인한다. Boolean origin과 nullity는 정확하게 치환·정규화한다.
Unknown을 부정하여 false를 만들지 않는다.

종료 조건은 DNF로 보관한다.

```text
ExitCondition = OR<Conjunction>
Conjunction   = AND<Atom>
empty disjunction = false  // 보장 영역 없음; coverage를 별도로 확인
empty conjunction = true // 모든 입력에서의 종료가 증명된 경우에만 인증 가능
```

검증된 disjunct는 일부만 보관해도 되지만, 그 경우 기본 coverage는 Partial이다.
불완전한 disjunct를 정확한 것으로 합치거나 DNF를 interval hull로 바꾸어 종료
보장 영역을 넓히지 않는다. 동치인 단순화는 허용한다. 더 강한 조건으로 대체하는
경우에도 최종 조건의 종료 보장을 확인하고 coverage를 재평가한다.
단, §5.3·5.4의 29/30번 검사 보존 요구가 우선한다. 해당 조건을 다른 atom으로부터
함의할 수 있더라도 생성·저장·출력하는 summary에서 삭제하지 않는다. 완전히 동일한
atom의 중복 제거는 허용한다. Numerical domain의 적용 정밀도와 조건의 저장 여부는
구분한다.

DNF를 구성하고 합성할 때 동일 typed parameter와 상수 사이의 비교는 논리적으로
중복된 항을 제거한다. Width/sign과 비교 predicate의 signedness가 일치하는 경우에만
유한 정수 범위의 정확한 집합 포함 관계를 사용한다. `!=k`는 k를 제외한 두 구간으로
취급하며 interval hull로 바꾸지 않는다. 예를 들어 `n!=0 && n>=0 && n>300`은
`n>300`, `n!=0 && n<0`은 `n<0`으로 저장한다. `-5<=n<=5 && n!=0`의 내부 구멍은
보존한다. 서로 다른 parameter, cast 및 복합 연산식의 비교에는 이 scalar 규칙을
추측하여 적용하지 않는다.

같은 scalar와 같은 상수 k에 대한 끝점 제외도 동치로 합친다.
`n>=k && n!=k`는 `n>k`, `n<=k && n!=k`는 `n<k`이다. Signed/unsigned predicate와
width/sign이 일치할 때만 적용하며 k±1을 machine integer로 계산하지 않는다.
이 변환은 내부의 구멍을 제거하는 규칙이 아니다. 특히 45번의 정상 조건은
`n!=0 && n>=0 && n<=300`에서 `n>0 && n<=300`으로 저장해야 하며,
interval에 NE를 먼저 적용하여 0 제외를 놓치는 순서 의존성을 피한다.

OR에서도 `A || (!A && B) = A || B`를 적용한다. 일반적으로 `K && c`의 c를
제거하려면 다른 disjunct `!c && R`가 존재하고 K가 R를 함의함을 증명해야 한다.
c는 위 scalar 비교로 제한한다. 따라서 44번 예외 조건은 `r>=6 || c>=8`, 45번 예외
조건은 `n>300 || n<0`으로 저장한다. Residual 조건 R를 무시해서는 안 된다.
논리 단순화는 동치 변환이므로 coverage를 바꾸지 않는다. 한 호출의 후보 검사 예산은
16,384회이며 한도에 도달하면 남은 조건을 유지한다. §5.3·5.4의 parity·분기별 USub·UMul
검사 보존은 계속 우선한다. 조건 저장의 단순화와 numerical state의 interval join은
별개의 처리다.

### 5.1 Benchmark 21의 기준

```text
C_normal =
    (n₀ >= 1 AND n₀ <= 500 AND SRem_w(n₀, 2) != 0)
    OR
    (n₀ >= 1 AND n₀ <= 1000 AND SRem_w(n₀, 2) == 0)
```

위 조건은 결정적인 guard의 실제 정상 종료 입력과 동치임을 증명하면 Complete다.
`1 <= n₀ <= 500`도 종료 보장 자체를 별도로 증명하면 유효한 Partial 조건이다.
반면 `%` 조건을 버려 `1 <= n₀ <= 1000`으로 저장하면 홀수 501 등의 비정상 종료
입력을 보장 영역에 포함하므로 이 명세를 위반한다. 23번처럼 생략할 조건이
나머지 조건으로부터 함의됨을 증명한 경우에는 동치인 제거를 허용한다. 29/30번은
§5.3·5.4에 따라 검사를 그대로 보존한다. 중복 제거 증명을 보존의 전제나 대체로
삼지 않는다.

### 5.2 보장 조건을 구성하는 기본 증명 규칙

우선 지원하는 대상은 동작 의미를 확인한 acyclic guard CFG이다. 아래 `S_X(s,Q)`는
statement s 이후의 보장 Q까지 flow X로 진행함을 보장하는 입력 충분조건이다.
조건의 수치 연산은 typed IR 의미로 해석한다.

```text
X 종류의 return/exception terminal → RequiredInD(terminal)
다른 종료 종류의 terminal         → false
순수 assignment x=e               → RequiredInD(e) AND Q[x←e]
if B then t else f                 → RequiredInD(B) AND
                                      ((B AND S_X(t,Q)) OR
                                       (NOT B AND S_X(f,Q)))
```

이 규칙은 정의된 실행에 대해 해석한다. `RequiredInD`는 그 의미론 외에 proof에서
확인해야 하는 전제이며 no-wrap overflow 배제 입력 제약은 포함하지 않는다. 명시적
분기, 지원 범위의 나눗셈 전제 및 Writable 메모리 전제 등은 기존 규칙대로 처리한다.
미지원 연산이나 call의 종료/효과를 true로 대체하는 규칙이 아니다. 단순 void return처럼
추가 전제가 없는 terminal은 true가 될 수 있다.
Branch의 B는 §2.6으로 복원한다. 복원하지 못한 B를 true로 바꾸지 않으며 각
분기의 보장 기여는 §6에 따라 처리한다. 동작을 모두 모델링한 acyclic CFG에서
이 규칙을 정확히 적용하면 Complete를 증명할 수 있다. 경로별 증명 결과를 일부만
유지하면 Partial이다.

Call은 callee의 인증된 충분조건, 해당 영역의 effect 및 이후 Q를 함께 검증해야
한다. Unknown call을 건너뛰어 반환을 보장하지 않는다. Loop에는 해당 입력 영역의
유한 종료와 effect 보존에 대한 별도 증명이 필요하다. 필요한 증명이 없으면 해당
영역의 기여를 생략한다. Forward numerical invariant는 정의 가능성이나 중복 조건을
증명하는 보조 근거로 사용할 수 있지만, 그 invariant 자체를 보장 영역으로 쓰지 않는다.

### 5.3 Benchmark 29 — 분기별 대입과 마지막 검사 보존

`aligned=(idx%2==0)?idx:idx-1` 뒤의 `if(aligned<2) abort()`는 normal 조건인
`aligned>=2`로 반드시 반영한다. 비교 operand를 각 분기의 실제 대입식으로 치환하고
그 대입을 선택하는 guard를 같은 conjunction에 유지한다.

```text
i = idx₀
w = size_t의 bitwidth
G = (i u>= 2 AND i u< 8)

짝수 경로: (URem_w(i,2) u== 0, aligned = i)
홀수 경로: (URem_w(i,2) u!= 0, aligned = USub_w(i,1))

C_normal(guard_index) =
    (G AND URem_w(i,2) u== 0 AND i u>= 2)
    OR
    (G AND URem_w(i,2) u!= 0 AND USub_w(i,1) u>= 2)
```

마지막 검사가 짝수 경로에서는 `i>=2`, 홀수 경로에서는 `USub_w(i,1)>=2`로
포함되어 있다. 짝수 경로의 `i>=2`는 G의 동일 atom과 중복 제거할 수 있지만,
parity guard와 홀수 경로의 뺄셈 비교는 다른 조건에서 함의된다는 이유로 삭제하지
않는다. 단순히 `G`만 저장하거나 ITE를 출력한 채 분기별 DNF 치환을 지원하지 않는
상태를 이 예제의 완료로 보지 않는다.

이는 원래 검사들을 보존한 정확한 정상 종료 조건이다. 정의된 실행의 전체 경로
포괄성을 확인하면 Complete이며, 중복 조건 제거 증명 없이도 생성할 수 있다.
σ의 flat merge에서 aligned가 TOP이어도 검증된 reaching definition과 guard를
역추적한다. 분기별 대입과 guard의 상관관계를 지운 뒤 값 집합만 나열하거나 잘못된
분기의 대입식을 선택하지 않는다. 이 제한적인 조건 복원은 일반적인 path-sensitive
값 domain을 새로 도입하는 요구가 아니다.

### 5.4 Benchmark 30 — 변수끼리의 곱셈 검사 보존

`prod=step*idx` 뒤의 `if(prod>9) abort()`는 `prod u<=9`로 반영한다.
두 operand가 변수여도 typed multiplication을 CondExpr로 복원한다.

```text
s = step₀; i = idx₀
RestoreOrigin(prod) = UMul_32(s,i)

C_normal(guard_stride_params) =
    s u!= 0 AND s u<= 3 AND i u<= 3 AND UMul_32(s,i) u<= 9
```

UMul_32는 uint32_t의 modulo 2^32 곱셈이며 일반 수학적 곱셈이나 affine 관계로
임의 변환하지 않는다. No-wrap flag 또는 상수 operand를 조건 표현의 필수 전제로
요구하지 않는다. σ(prod)가 TOP이어도 마지막 비교를 true로 바꾸지 않는다.

앞의 범위가 곱의 [0,9]를 보장하더라도 저장·출력하는 summary에는 곱셈 비교를
그대로 유지한다. 이를 위해 일반 비선형 solver나 중복 제거 증명을 요구하지 않는다.
전체 원래 조건을 보존한 종료 보장을 확인하면 Complete로 저장한다. Numerical
domain이 곱셈 atom을 직접 적용하지 못하더라도 symbolic summary의 atom은 유지한다.

29/30번의 필수 복원이 실패하면 §6의 안전한 fallback을 따르되, 이를 해당 benchmark의
구현 완료로 간주하지 않는다. 조건 보존 요구는 기존 soundness/coverage 검증과 함께
충족해야 한다.


## 6. 환원 실패와 TOP 처리

`σ(x)=TOP`은 affine 값 관계를 모른다는 뜻이며 조건을 무조건 버리는 근거가 아니다.
SSA origin으로 정확한 CondExpr 복원을 먼저 시도한다. 그래도 조건을 환원하지
못하면 다음 순서를 따른다.

1. 지원하는 표현으로 원래 조건을 정확하게 복원한다.
2. 복원하지 못한 조건을 함의하는 더 강한 입력 조건을 증명할 수 있으면 그
   영역만 인증한다. 나머지 경로/call/loop 의미도 검증한다.
3. 대체 증명도 없으면 해당 **전체 conjunction의 기여를 생략**하고 Partial로
   표시한다. 이는 보장 영역을 얻지 못했다는 뜻이며 경로 불가능의 증거가 아니다.
4. 신뢰할 수 있는 결과를 구성할 수 없으면 Unavailable로 저장한다.

위 Partial 처리는 해당 flow의 입력 coverage를 놓쳤을 수 있기 때문이다. 미지원
경로가 정상 복귀할 수 없다는 별도 증명이 있으면 C_normal에 새 disjunct를 추가하지
않고 normal coverage를 유지할 수 있다(DEV_TRANSFER_FUNCTION.md §8.2.1).
복원 실패만으로 그 증명을 대신하거나 exception coverage를 Complete로 올리지 않는다.

예를 들어 다음 조건에서 x의 origin을 복원할 수 없고 다른 증명도 없다면:

```text
Candidate = (a₀ > 0) AND (x >= 10)
σ(x) = TOP
```

`x>=10`을 true로 바꾸어 `C_X=a₀>0`으로 저장하지 않는다. 해당 disjunct를
인증하지 않으며, 다른 인증 영역도 없다면 `Known(false, Partial, ∅)` 또는
Unavailable이다. 실제로 해당 flow가 없다는 별도 증명이 있어야만
`Known(false, Complete, ∅)`로 만들 수 있다.

`C_X=true`는 정보가 없다는 뜻이 아니라 모든 허용 입력에서 종료를 보장한다는
주장이다. 미지원 atom의 삭제로 true를 만들지 않는다. Unknown의 부정과 numerical
TOP은 unknown으로 유지한다. Caller의 미확인 영역은 기존 분석으로 계속 검사한다.


## 7. Exit Memory Summary

함수 종료 시점의 전체 `μ_exit`를 그대로 caller에게 전달하지 않는다.

callee 내부의 local Allocate location은 caller에서 관찰할 수 없으므로 제거한다.

```text
ExternalLoc =
    { L ∈ dom(μ_exit) |
      L is caller-visible }
```

따라서:

```text
μ_normal_exit =
    μ_exit | ExternalLoc
```

```text
μ_exception_exit =
    μ_exit | ExternalLoc
```

예:

```text
μ_exit = {
    $3 ↦ a₀,
    $4 ↦ L₁,
    L₁ ↦ a₀ + 1
}
```

이면:

```text
μ_normal_exit = {
    L₁ ↦ a₀ + 1
}
```

`μ_X`의 각 relation은 최소한 `C_X` 안의 모든 flow X 실행에서 성립해야 한다.
Partial의 memory effect를 미확인 영역에 무조건 적용하지 않는다. DNF 경로마다
값이 다르면 공통으로 보장되는 relation만 유지하거나 TOP으로 처리한다.

Local 여부는 `$`와 같은 변수 이름 문자열로 판단하지 않는다.

반드시 해당 location이 현재 callee의 `Allocate`에서 생성된 location인지 origin을 기준으로 판단한다.


## 8. CFG Branch 처리

IKOS AR에서는 branch condition이 별도의 branch statement로 표현되지 않고 successor basic block의 `Comparison`으로 나타날 수 있다.

예:

```text
#1 successors={#2, #3}

#2 {
    %8 sigt 0
    ...
}

#3 {
    %8 sile 0
    ...
}
```

IKOS analyzer는 successor별로 서로 다른 abstract state를 유지한다.

Symbolic state 역시 CFG를 따라 basic block별 상태를 유지한다.


## 9. CFG Merge

여러 predecessor가 하나의 basic block으로 합류하면 component별로 병합한다.
σ/μ는 기존 flat symbolic merge, ρ는 같은 key에 대한 interval join을 사용한다.

```text
Σ_merge = Σ₁ ⊔ₛ Σ₂
```

`ρ_merge(E)=ρ₁(E) ⊔_I ρ₂(E)`이며 missing key는 TOP이다(§2.9).
`σ`와 `μ`의 각 entry에는 다음 규칙을 적용한다.

```text
MergeVal(v₁, v₂) =
    v₁     if v₁ = v₂
    TOP    otherwise (아래 Boolean-origin 정규화 이후의 기본 규칙)
```

즉:

```text
동일한 symbolic value
→ 그대로 유지

서로 다른 symbolic value
→ TOP
```

예:

```text
Pred₁:
σ(x) = a₀ + 1

Pred₂:
σ(x) = a₀ + 1
```

이면:

```text
σ_merge(x) = a₀ + 1
```

반면:

```text
Pred₁:
σ(x) = a₀ + 1

Pred₂:
σ(x) = b₀ + 2
```

이면:

```text
σ_merge(x) = TOP
```

일반 relation에는 이 기본 정책을 적용한다. 다만 Boolean을 생성하는 완전한
분기 구조는 pointwise merge 전에 정규화한다.

```text
G 경로:  b=1       ¬G 경로: b=0
→ 각 assignment를 b=BooleanValue(G)로 정규화
→ merge에서 동일한 BoolExpr 유지
```

현재 지원하는 G는 offset 0인 pointer parameter의 null 비교이다. 모든 definition을
검사하여 각 assignment가 같은 pointer operand의 PEQ/PNE 조건 아래 G의 올바른
0/1 encoding을 대입함을 증명한다. 각 definition block은 해당 comparison과
assignment로만 구성되어야 한다. 두 branch의 기본 diamond뿐 아니라, 여러 Boolean
인자 평가로 복제된 branch도 전체 definition이 같은 G를 표현하면 지원한다.
Pointer operand는 동일한 AR value이며, formal parameter는 entry 이후 재대입이
없어야 하고 그 외 value는 최대 하나의 definition만 가져야 한다.
Definition block이 cycle에 포함되면 현재 인식 범위에서 제외한다. 사용 위치에서
origin을 역으로 조회할 때에는 entry에서 use에 이르는 모든 경로가 검증된 assignment
중 하나를 반드시 통과해야 한다. 단일 merge의 dominance는 이 조건의 특수한 경우이다.
누락된/추가된 미지원 definition, TOP 또는 초기화되지 않은 경로가 있으면 기본 규칙을
사용한다. 미지원 definition을 무시한 채 일부 branch만으로 관계를 만들지 않는다.

이 규칙은 임의의 경로 전체를 보존하는 disjunctive state가 아니라, 증명한 Boolean
값의 유래를 기존 σ에 저장하는 제한적인 확장이다. 일반 memory merge와 loop
fixpoint 규칙은 유지한다.


## 10. LLVM PHI와 IKOS AR

LLVM PHI는 IKOS AR에서 명시적인 PHI statement로 남지 않을 수 있다.

실제 AR에서는 각 predecessor에서 동일한 AR variable에 assignment하는 형태로 나타난다.

예:

```text
#2:
    %.0 = %4

#3:
    %.0 = %6

#4:
    ... %.0 ...
```

따라서 symbolic analysis에서는 CFG merge 시 동일한 AR variable `%.0`에 대한 predecessor별 symbolic value를 merge해야 한다. 단, Section 9의 완전한 Boolean 생성 분기는 assignment를 동일한 BoolExpr로 정규화한 뒤 merge한다.

예:

```text
#2:
σ(%.0) = a₀ + 1

#3:
σ(%.0) = b₀ + 2
```

이면 #4 진입 시:

```text
σ(%.0) = TOP
```


## 11. Loop 표현

IKOS는 WTO(Weak Topological Ordering)를 이용하여 loop를 식별하고 fixpoint를 계산한다.

예:

```text
#1 → #2
     ↓
     #3
     ↓
     #2

#2 → #4
```

여기서:

```text
#2 = loop head
#1 → #2 = loop-entry edge
#3 → #2 = back-edge
#2 → #4 = loop-exit edge
```


## 12. 최초 Loop-Head State

IKOS의 `WtoIterator::visit(WtoCycleT&)`는 최초 loop-head state를 계산할 때 entry edge만 사용한다.

개념적으로:

```text
Σ_H^0 =
    ⊔ₛ {
        Σ_OUT(P) |
        P → H is a loop-entry edge
    }
```

최초 상태에서는 back-edge를 반영하지 않는다.

예:

```text
#1 → #2
#3 → #2
```

이면 최초 상태는:

```text
Σ_#2^0 = Σ_OUT(#1)
```

이다.


## 13. Loop Back-Edge Merge

Loop body를 분석한 후 entry edge와 back-edge에서 들어오는 symbolic state를 다시 계산한다.

```text
Σ_candidate^(k+1) =
    Σ_entry ⊔ₛ Σ_back^k
```

σ/μ의 symbolic merge는 CFG merge와 동일한 MergeVal을 사용한다.
ρ는 §2.9의 interval join을 사용하며 back-edge의 값 재정의에 따른 무효화를 먼저
반영한다. 이전 iteration의 expression fact를 새 값에 그대로 붙이지 않는다.

예:

```text
entry:
σ(%.0) = n₀

back-edge:
σ(%.0) = n₀ + 1
```

이면:

```text
σ_candidate(%.0)
    = MergeVal(n₀, n₀ + 1)
    = TOP
```


## 14. Symbolic Loop Fixpoint

Loop의 symbolic state는 IKOS의 기존 WTO iteration lifecycle과 함께 반복적으로 계산한다.

명세 수준에서 다음과 같이 정의한다.

```text
Σ_H^0 =
    merge of loop-entry states
```

반복:

```text
Σ_candidate^(k+1) =
    Σ_entry ⊔ₛ Σ_back^k
```

```text
(σ_H^(k+1), μ_H^(k+1)) =
    (σ_H^k, μ_H^k) ⊔ₛ (σ_candidate^(k+1), μ_candidate^(k+1))
ρ_H^(k+1) = WTOIntervalUpdate(ρ_H^k, ρ_candidate^(k+1))
```

WTOIntervalUpdate는 기존 WTO의 ascending/widening 정책과 동기화하며 유한 key
집합의 interval 수렴을 보장한다. σ/μ가 먼저 안정되어도 ρ의 수렴 검사를 생략하지
않는다. 다음 조건을 만족하면 보조 state 전체가 안정된 것으로 본다.

```text
Σ_H^(k+1) = Σ_H^k
```


## 15. Symbolic Widening

σ/μ의 flat symbolic 관계에는 별도의 widening을 정의하지 않는다.

현재 merge 정책에서는 서로 다른 symbolic value가 즉시 `TOP`으로 상승한다.

예:

```text
n₀
↓
MergeVal(n₀, n₀ + 1)
↓
TOP
↓
MergeVal(n₀, TOP)
↓
TOP
```

따라서 σ/μ의 loop-carried symbolic value는 빠르게 안정된다. ρ의 interval 수렴은 별도다.

IKOS의 기존 numerical AbstractDomain은 기존 방식대로 join/widening을 수행하지만, `σ/μ`는 자체 `MergeVal` 규칙으로 수렴한다.

```text
IKOS AbstractDomain:
    join → widening → fixpoint → narrowing

Symbolic σ/μ:
    MergeVal → fixpoint

Expression ranges ρ:
    interval join → widening → fixpoint → sound narrowing (선택)
```

σ/μ의 별도 symbolic widening은 사용하지 않는다. ρ에는 interval widening을 적용한다.


## 16. Symbolic Narrowing

σ/μ의 symbolic 관계에는 별도의 narrowing을 적용하지 않는다.
ρ의 interval narrowing은 sound한 재분석으로 근거를 확인한 경우 허용한다.

한번:

```text
σ(x) = TOP
```

으로 추상화된 값은 loop narrowing 단계에서 다시 구체적인 symbolic expression으로 복원하지 않는다.

```text
TOP → TOP
```

현재 분석의 목표는 sound한 parameter relation summary 생성이며, loop invariant의 정밀한 symbolic 복원은 향후 확장으로 남긴다.


## 17. Loop Exit

WTO loop fixpoint가 종료된 후 loop exit successor는 수렴된 loop state를 기반으로 분석한다.

Loop exit의 `Comparison`은 일반 exit constraint와 동일하게 `SubstConstraint`를 적용한다.

예:

```text
%.0 sige 10
```

이고:

```text
σ(%.0) = TOP
```

이면 해당 constraint는 parameter-origin expression으로 환원할 수 없다.

따라서:

```text
SubstConstraint(%.0 ≥ 10, σ)
→ Unrepresentable
```

SSA origin으로도 복원하지 못하면 이 경로의 종료 보장 기여를 생략하고 Partial로
표시한다. Loop의 수렴된 invariant만으로 종료성을 증명하지 않는다.

Loop exit의 memory effect 역시 일반 exit와 동일하게 처리한다.

예:

```text
store %2, %.0
```

에서:

```text
σ(%2) = L₁
σ(%.0) = TOP
```

이면:

```text
μ(L₁) = TOP
```

이다.


## 18. Branch와 Loop의 통합 CFG 정책

증명 가능한 acyclic Boolean 생성 assignment를 정규화한 뒤에는 branch와 loop에 동일한 기본 symbolic merge 원칙을 사용한다.

```text
CFG Merge:

동일한 symbolic value
    → 유지

서로 다른 symbolic value
    → TOP
```

위 same/different symbolic value 규칙은 σ/μ에 대한 것이다. ρ는 branch와 loop
모두 같은 key의 interval join을 사용하고 loop head에서는 추가 widening을 수행한다.
차이는 merge 수행 방식에 있다.

Branch:

```text
predecessor states
        ↓
      merge
        ↓
successor state
```

Loop:

```text
entry state + back-edge state
        ↓
      merge
        ↓
loop-head state
        ↓
   body analysis
        ↓
     back-edge
        ↓
     반복하여 fixpoint
```

따라서 σ/μ의 값 분석에는 별도의 path-sensitive branch/loop symbolic domain을
사용하지 않는다. 다만 C_X를 인증하는 별도 유한 탐색은 경로별 조건과 reaching
definition을 유지한다. 이 탐색의 한도를 넘으면 Partial이며, σ/μ의 merge 규칙을
변경하거나 loop 불변식을 추측하지 않는다(DEV_PRUNING.md §13.2).


## 19. IKOS Loop Fixpoint와의 통합

σ/μ/ρ의 symbolic state 분석은 IKOS의 기존 WTO fixpoint lifecycle과 함께 수행한다.
종료 보장 인증용 유한 경로 탐색은 이 값 fixpoint와 구분하며, 탐색 실패를
종료 보장으로 사용하지 않는다.

IKOS의 loop 처리 순서는 개념적으로 다음과 같다.

```text
1. loop-entry state로 최초 head state 생성
2. loop head 분석
3. loop body 분석
4. back-edge state 계산
5. entry/back-edge merge
6. IKOS AbstractDomain extrapolate
7. fixpoint 확인
8. 필요하면 반복
9. IKOS narrowing/refinement
10. loop exit 분석
```

Symbolic state는 동일한 basic block 방문 및 iteration 순서를 사용한다.

현재 구현 검토에서 활용 가능한 주요 hook은 다음과 같다.

```text
analyze_node()
analyze_edge()
extrapolate()
refine()
notify_enter_cycle()
notify_cycle_iteration()
notify_leave_cycle()
```

실제 구현에서는 symbolic `pre/post` state를 basic block별로 유지하고 IKOS의 WTO iteration 순서에 맞춰 갱신해야 한다.

단순히 `analyze_node()` 내부에 transfer function만 추가하는 것으로는 충분하지 않으며, predecessor별 symbolic state의 merge와 loop-head symbolic state 관리가 함께 필요하다.


## 20. IKOS 본 분석과 Summary 생명주기

`SummaryStore`는 `ikos-import`의 임시 결과가 아니라 IKOS 본 분석의
`Context`가 소유한다. 분석 context가 생성될 때 함께 생성되고 본 분석이
종료될 때까지 유지된다.

Summary 생성과 저장은 별도 옵션으로 활성화하거나 비활성화하지 않는다.
옵션은 개발용 출력과 trace 여부에만 영향을 준다.

IKOS의 기존 interprocedural analysis는 별도의 전체 call graph를 먼저 생성하는 방식이 아니라 call을 만나면 callee를 분석하는 top-down 방식으로 동작한다.

예:

```text
main
 ↓
 f
 ↓
 g
```

분석 과정에서 결과 summary는 자연스럽게 다음 순서로 완성될 수 있다.

```text
Summary(g)
    ↓
Summary(f)
    ↓
Summary(main)
```

각 함수의 fixpoint가 완료되면 해당 함수의 `FunctionSummary`를 생성하여
`Context::function_summaries`에 저장한다. 따라서 현재 설계에서는 별도의 call
graph 또는 SCC 기반 summary 생성 framework를 추가하지 않는다.

Intraprocedural 분석에서도 함수 fixpoint가 완료되는 동일한 시점에 summary를
저장한다.


## 21. Summary 생성 단계의 비간섭 원칙

본 단계는 함수 summary를 생성·합성하여 저장한다.

```text
Function fixpoint → FunctionSummary 생성 → SummaryStore 저장
```

이 생성 단계는 기존 IKOS abstract invariant나 call execution semantics를 직접
변경하지 않는다. 다만 현재 함수의 summary를 생성하기 위해 이미 알려진 callee
summary를 조회하고 parameter-origin 문맥으로 합성할 수 있다. 보조 symbolic
memory/condition의 합성과 기존 IKOS caller state의 refinement는 구분한다.
후자는 DEV_PRUNING.md에서 정의한다. ρ를 이용한 실제 numerical state 정제도
transfer/application 단계의 동작이며, read-only summary 생성 검증과 구분한다.

함수당 재사용하는 summary는 함수 entry의 모든 입력에 대해 sound해야 한다.
특정 caller context에서만 성립하는 제약을 함수 공통 summary로 저장하지 않는다.


## 22. Recursion과 저장

함수 분석 순서와 recursion 처리는 IKOS의 기존 동작을 그대로 사용한다.
Summary 생성 단계는 recursion 판단이나 call semantics를 변경하지 않는다.

한 함수가 여러 call context에서 분석되더라도 현재 summary는 formal
parameter 기준의 함수 단위 결과이므로 `SummaryStore`에는 함수당 하나만
저장한다. 별도의 recursive FunctionSummary fixpoint 또는 SCC 분석은 현재
범위에서 구현하지 않는다.


## 23. Call Target과 Summary 생성의 분리

Unresolved call 및 multiple possible callees의 summary 적용 정책은 pruning
명세에서 정의한다. Summary 생성 자체는 runtime call target을 선택하거나 변경하지
않는다. 보조 symbolic 분석에서는 직접 식별된 singleton callee의 알려진 summary를
조회해 합성하며, unresolved/multiple target에는 보수적 fallback을 적용한다.


## 24. Caller-Visible Summary 원칙

최종 FunctionSummary에는 caller가 관찰할 수 있는 정보만 포함한다.

포함:

```text
normal termination condition
exception termination condition
parameter-relative external memory effect
```

제외:

```text
callee local Allocate location
callee 내부에서만 사용되는 SSA relation
program-exit memory state
return-value relation
```


## 25. 향후 정밀도 확장

σ/μ의 CFG merge는 증명 가능한 Boolean 생성 패턴을 먼저 정규화하고 그 외 서로
다른 symbolic value는 TOP으로 병합한다. ρ는 §2.9의 interval join/widening을 따른다.

향후 다음 방식으로 확장할 수 있다.

```text
1. branch별 symbolic state 유지
2. symbolic value의 finite disjunction
3. path-sensitive symbolic state
4. path condition과 σ/μ 사이의 correlation 유지
5. loop에 대한 보다 정밀한 symbolic invariant 추론
```

현재 범위에는 Section 9의 nullity Boolean-origin 보존과 §5.3의 acyclic 조건부 대입을
종료 조건으로 복원하는 기능이 포함된다. 일반적인 path-sensitive σ/μ와 임의의
guarded/disjunctive 값 상태는 후속 확장이다.

별도로, summary의 NE(`!=`) 조건이 기존 numerical domain 적용 과정에서 손실되는
정밀도는 현재 수용한다. Summary에 표현 가능한 NE atom은 유지하되, 단일 interval로
표현할 수 없는 중간 값 제외 정보는 별도 보존하지 않는다. 향후 interval과 결합하는
zero/non-zero 보조 상태 및 DBZ checker 연계를 도입할 수 있으며, 그 범위와 갱신·병합
요건은 DEV_PRUNING.md §13.4에서 정의한다. ν는 개정된 Σ의 ρ와 달리 후속 확장이며 필수 구현 요건이 아니다. ρ는 표현식의
단일 interval을 저장하므로 중간의 0 제외 정보를 별도 표현하지 않는다.


## 26. 핵심 Invariant

분석 구현은 다음 invariant를 만족해야 한다.

```text
1. 보조 state는 Σ=(σ,μ,ρ)이다. σ/μ는 scalar affine 및 pointer/Boolean 값 관계,
   ρ는 typed 표현식의 범위다.

2. 별도의 π pointer state는 사용하지 않는다.

3. Formal symbolic location은 source-level parameter 이름에 의존하지 않는다.

4. CFG merge에서 동일한 symbolic value는 유지한다.

5. 증명 가능한 nullity Boolean 생성 패턴을 정규화한 뒤, 서로 다른 symbolic value는 TOP으로 병합한다.

6. Loop는 IKOS의 기존 WTO fixpoint lifecycle을 사용한다.

7. σ/μ에는 별도 widening을 사용하지 않고 ρ에는 interval widening을 사용한다.

8. σ/μ에는 narrowing을 사용하지 않고 ρ에는 증명된 interval narrowing만 허용한다.

9. exit constraint는 affine σ 또는 SSA origin에서 정확히 복원한다. 실패 시
   해당 conjunction을 인증하지 않으며, atom을 true로 바꾸어 영역을 넓히지 않는다.

10. 보장 영역이 없으면 false/Partial 또는 Unavailable이다.
    false/Complete만 flow 불가능을 뜻하며 true는 전체 입력의 종료 보장을 뜻한다.

11. 여러 normal exit의 조건은 disjunction으로 결합한다.

12. 여러 exception exit의 조건은 disjunction으로 결합한다.

13. μ_normal_exit와 μ_exception_exit에는 caller-visible location만 포함한다.

14. callee-local Allocate location은 exit summary에서 제거한다.

15. local/external location 구분은 이름 문자열이 아니라 location origin으로 판단한다.

16. program_exit는 caller용 summary를 생성하지 않는다.

17. return value summary는 현재 생성하지 않는다.

18. SummaryStore는 IKOS 본 분석 Context가 소유하고 분석 종료까지 유지한다.

19. 함수 fixpoint가 완료되면 summary를 생성하여 함수당 하나를 저장한다.

20. Summary 생성 단계에서는 caller state 또는 기존 call semantics를 변경하지 않는다.

21. 미분석 exception 영역은 Partial 또는 Unavailable이며 불가능으로 취급하지 않는다.

22. [[P_memory ∧ C_X]] ∩ DefInputs ⊆ GuaranteedExitInputs_X를 유지하고, P_memory 안의 정의된 실행에 대한 coverage를 독립적으로 증명한다.

23. Partial 영역 밖의 실제 실행은 caller에서 보존한다.

24. ρ(E)는 현재 flow의 모든 실제 E 값을 포함하고, C_X의 충분조건과 구분한다.

25. ρ는 값 identity·연산 타입이 같은 key에서만 재사용하고 checker가 보는 A에 반영한다.

26. 합류의 ρ는 interval join이며 한 incoming에서 없는 key는 TOP이다.

27. 입력 값 재정의 시 의존 key를 무효화하며 단순 liveness 제거와 구분한다.

28. 29번은 마지막 aligned 검사를 분기별 대입으로 치환한 DNF에 보존한다.

29. 30번은 변수끼리의 typed 곱셈 비교를 summary에 그대로 보존한다.
```


## 27. 본 문서와 Transfer Function 명세의 관계

본 문서는 FunctionSummary를 생성하기 위한 전체 분석 구조와 CFG/exit 처리 정책을 정의한다.

각 AR statement에 대한 구체적인 symbolic transfer function은 별도의 transfer-function 명세에서 정의한다.

```text
Transfer Function 명세
    ↓
각 AR statement에서 σ/μ/ρ 갱신 및 A와의 범위 정제
    ↓
본 문서의 CFG/loop 처리
    ↓
Exit condition / memory filtering
    ↓
FunctionSummary 생성
```

Transfer-function 명세는 본 문서에서 확정된 다음 정책과 일관되어야 한다.

```text
Σ = (σ, μ, ρ)

π 사용 안 함

CFG merge:
    same → same
    proved Boolean construction → 동일 BoolExpr로 정규화
    different → TOP (그 외 기본 규칙)

ρ merge:
    same typed value key → interval join; missing → TOP

Loop:
    IKOS WTO lifecycle + σ/μ flat merge + ρ interval widening

Exit constraint:
    exact restoration + exit proof → 인증된 영역 유지
    29 → guard와 분기별 대입을 치환한 마지막 비교를 DNF에 보존
    30 → UMul_32의 마지막 비교 보존; 중복을 이유로 삭제하지 않음
    unrepresentable → 더 강한 영역을 증명하거나 conjunction 기여 생략/Partial
    unknown → true로 변환 금지

Caller pruning:
    Complete → 해당 flow의 필요조건으로도 사용 가능
    Partial → 미확인 영역 보존

Exit μ:
    caller-visible location만 유지
```


## 28. 별도 Pruning 명세와의 관계

본 문서에서 생성된 결과:

```text
FunctionSummary(f)

NormalSummary(f) = {
    C_normal(f),
    Coverage_normal(f),
    μ_normal_exit,f
}

ExceptionSummary(f) = {
    C_exception(f),
    Coverage_exception(f),
    μ_exception_exit,f
}
```

를 caller에서 실제로 사용하는 과정은 별도 문서에서 정의한다.

해당 문서에서는 다음 내용을 다룬다.

```text
callee summary lookup
parameter ↔ argument mapping
C_normal caller substitution
normal-flow pruning
μ_normal_exit caller memory 반영
Call / Invoke의 normal/exception successor 처리
```

따라서 본 문서는 FunctionSummary의 생성까지를 책임지고, caller-side pruning 및 summary application은 책임지지 않는다.

## 29. Nullity guard 완료 기준

```text
07: C_normal(guard_non_null) = NonNull(P_0^0)
09: σ(cond) = BooleanValue(NonNull(P_0^0))
    C_normal(guard_check) = cond_0 == 1
    C_normal(check_ptr) = NonNull(P_0^0)
```

Caller에서는 pointee target을 모르는 actual pointer에도 nullity 조건을 적용하여
07/09의 역참조 nullity warning을 제거해야 한다. Guard 없는 nullable pointer 및
`guard_either(ptr != null, flag)`는 warning을 유지해야 한다. 지원하지 않는
Boolean 생성/cast/derived-pointer relation은 값 관계에서는 TOP이며, 종료 조건의
인증은 §6을 따른다. 07/09의 전역 pruning에는 각 guard의 Complete 증명이 필요하다.
Return-value summary나 일반적인 path partitioning은 이 완료 기준의 전제가 아니다.
