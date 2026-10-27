# IKOS AR 기반 Function Summary 생성 명세

## 1. 목적

본 문서는 IKOS AR을 기반으로 함수(callee)를 분석하여 caller에게 전달할 `FunctionSummary`를 생성하는 방법을 정의한다.

본 문서의 범위는 다음과 같다.

- symbolic state `Σ = (σ, μ)`를 이용한 함수 내부 분석
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


## 2. Symbolic State

함수 분석 중 다음 symbolic state를 유지한다.

```text
Σ = (σ, μ)
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

`Σ=(σ,μ)`와 별도 `π`를 사용하지 않는 원칙은 유지한다. `Loc`은 memory key로
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
nullity 및 임의의 Boolean formula는 표현 불가 시 보수적으로 약화한다.

Pointer parameter는 기존 parameter-relative `Loc`으로 초기화하되 pointer value로
해석할 수 있어야 한다. Integer parameter는 기존 `Expr`로 초기화하며, unsigned
1-bit parameter에는 값이 `{0,1}`이라는 정보를 함께 유지한다. `BoolParam(i)`는
이 typed integer entry atom의 논리적 표기이다. Null constant는 `Null`로 평가한다.

`BoolExpr`는 정확한 `{0,1}` encoding의 값 관계이다. `σ(b)=NonNull(P_i^0)`는
현재 경로에서 pointer가 non-null이라고 가정한다는 뜻이 아니라,
`b == 1 ⇔ P_i^0 != null`이라는 값의 유래를 뜻한다. Assignment/copy 및 정확한
Boolean encoding 보존 cast를 통해 전달할 수 있다. Memory에 저장된 값도 확장된
SymbolicValue를 사용할 수 있으나 기존 invalidation 정책을 따른다.


## 3. Function Summary

함수 `f`의 summary는 다음과 같이 구성한다.

```text
FunctionSummary(f) = {
    NormalSummary(f),
    ExceptionSummary(f)
}
```

Normal summary:

```text
NormalSummary(f) = {
    C_normal(f),
    μ_normal_exit,f
}
```

Exception summary:

```text
ExceptionSummary(f) = {
    C_exception(f),
    μ_exception_exit,f
}
```

각 flow summary는 `Known` 또는 `Unavailable` 상태를 갖는다. 정상 종료 CFG가
없으면 normal flow는 `Known(false, ∅)`로 표현할 수 있다. 반면 현재 보조 symbolic
분석이 모든 예외 전파를 포착하지 못하는 경우 exception flow는 `false`로 단정하지
않고 `Unavailable`로 저장한다.

특히 명시적인 exception-exit이 없거나 함수 내부에 일반 `Call`이 있어 그 call의
외부 전파 예외를 보조 symbolic 분석이 모델링하지 못하면 exception summary는
`Unavailable`이다. `Unavailable`은 caller pruning에서 기존 IKOS exception state를
그대로 유지한다.

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

각 exit 경로의 조건을 symbolic state `σ`로 parameter-origin condition으로
환원한다. 조건의 출처에는 AR `Comparison`의 integer/null 비교와, 이미 알려진
callee의 flow별 summary를 현재 함수 문맥으로 치환한 조건이 포함된다. 기존 IKOS
invariant에서 추가 제약을 추출할 경우에도 해당 제약의 타입과 추출 시점을
명시해야 하며, liveness가 변수를 제거한 뒤 사라진 제약이 남아 있다고 가정하지 않는다.

```text
C_after_call,X = C_before_call ∧ θ_c(C_X(callee))
Atom ::= IntegerComparison | IsNull(P_i^0) | NonNull(P_i^0)
```

Boolean argument의 정확한 origin이 있으면 `BooleanValue(G)==1`은 `G`,
`BooleanValue(G)==0`은 `¬G`로 환원한다. Nullity 부정은 IsNull/NonNull 사이에서
전환한다. 먼저 정확하게 치환·정규화한 뒤 표현 불가능한 atom만 true로 약화한다.
Unknown의 부정을 false로 해석하지 않는다. 이 조건은 실제 exit 입력을 모두
포함하는 필요조건(over-approximation)이며, 일반적으로 정상 반환의 충분조건은 아니다.

경로 `i`에 대해:

```text
C_X,i =
    SubstConstraint(
        AnalyzerConstraint_X,i,
        σ_i
    )
```

여기서 `X`는 `normal` 또는 `exception`이다.

여러 exit 경로가 존재하면 disjunction으로 결합한다.

```text
C_X = ⋁ᵢ C_X,i
```

따라서:

```text
C_normal(f) = ⋁ᵢ C_normal,i
```

```text
C_exception(f) = ⋁ᵢ C_exception,i
```

구현에서는 exit condition을 DNF(Disjunctive Normal Form)로 보관한다.

```text
ExitCondition = OR<Conjunction>
Conjunction   = AND<SymbolicConstraint>
```

즉 내부 `Conjunction`의 constraint는 논리곱이고, 여러 `Conjunction`은
논리합이다. 경로가 하나도 없으면 `false`, constraint가 하나도 없는
도달 가능한 경로가 있으면 `true`이다.


## 6. SubstConstraint의 TOP 처리

Exit constraint의 모든 조건이 항상 parameter-origin expression으로 환원되는 것은 아니다.

예를 들어:

```text
C_exit =
    (a > 0) ∧ (x ≥ 10)

σ(a) = a₀
σ(x) = TOP
```

이면 첫 번째 조건은 환원 가능하지만 두 번째 조건은 환원할 수 없다.

따라서 `SubstConstraint`는 다음 규칙을 사용한다.

```text
각 constraint c에 대해:

parameter-origin expression으로 완전히 환원 가능
    → 환원된 constraint 유지

TOP 또는 표현 불가능한 symbolic value 포함
    → 해당 constraint를 true로 추상화하여 제거
```

위 예에서는:

```text
a > 0
→ a₀ > 0
```

```text
x ≥ 10
→ unrepresentable
→ true
```

따라서:

```text
C_normal = a₀ > 0
```

모든 constraint가 제거되면:

```text
C_normal = true
```

로 한다.

`true`는 정상 종료 조건에 대해 caller에게 제공할 수 있는 유효한 parameter constraint가 없음을 의미한다.

이 방식은 실제 정상 종료 조건보다 약한 조건을 생성하므로 caller pruning 관점에서 보수적인 over-approximation이다.

단, 도달 가능한 exit 경로 자체가 하나도 없는 경우는 `true`가 아니라
`false`이다. 따라서 "도달 가능한 경로이나 표현 가능한 constraint가 없음"과
"도달 불가능한 exit"를 구분한다.


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

여러 predecessor가 하나의 basic block으로 합류하면 symbolic state를 pointwise merge한다.

```text
Σ_merge = Σ₁ ⊔ₛ Σ₂
```

`σ`와 `μ`의 각 entry에 대해 다음 규칙을 적용한다.

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

현재 symbolic merge는 CFG merge와 동일한 `MergeVal`을 사용한다.

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
Σ_H^(k+1) =
    Σ_H^k ⊔ₛ Σ_candidate^(k+1)
```

다음 조건을 만족하면 symbolic state가 안정된 것으로 본다.

```text
Σ_H^(k+1) = Σ_H^k
```


## 15. Symbolic Widening

현재 symbolic domain에는 별도의 widening을 정의하지 않는다.

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

따라서 loop-carried symbolic value는 빠르게 안정된다.

IKOS의 기존 numerical AbstractDomain은 기존 방식대로 join/widening을 수행하지만, `σ/μ`는 자체 `MergeVal` 규칙으로 수렴한다.

```text
IKOS AbstractDomain:
    join → widening → fixpoint → narrowing

Symbolic σ/μ:
    MergeVal → fixpoint
```

별도의 symbolic widening은 사용하지 않는다.


## 16. Symbolic Narrowing

현재 symbolic state에는 별도의 narrowing을 적용하지 않는다.

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
→ true
```

로 추상화하여 제거한다.

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

따라서 별도의 path-sensitive branch/loop symbolic domain은 현재 버전에서 사용하지 않는다.


## 19. IKOS Loop Fixpoint와의 통합

Symbolic analysis는 별도의 loop iteration engine을 구현하지 않고 IKOS의 기존 WTO fixpoint lifecycle과 함께 수행한다.

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
후자는 DEV_PRUNING.md에서 정의한다.

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

현재 CFG merge는 증명 가능한 Boolean 생성 패턴을 먼저 정규화하고, 그 외 서로 다른 symbolic value는 `TOP`으로 병합한다.

향후 다음 방식으로 확장할 수 있다.

```text
1. branch별 symbolic state 유지
2. symbolic value의 finite disjunction
3. path-sensitive symbolic state
4. path condition과 σ/μ 사이의 correlation 유지
5. loop에 대한 보다 정밀한 symbolic invariant 추론
```

현재 범위에는 Section 9의 제한적인 nullity Boolean-origin 보존만 포함한다. 위 목록의 일반적인 path sensitivity와 임의의 guarded/disjunctive value는 후속 확장이다.


## 26. 핵심 Invariant

분석 구현은 다음 invariant를 만족해야 한다.

```text
1. Symbolic state는 Σ=(σ,μ)만 사용한다.

2. 별도의 π pointer state는 사용하지 않는다.

3. Formal symbolic location은 source-level parameter 이름에 의존하지 않는다.

4. CFG merge에서 동일한 symbolic value는 유지한다.

5. 증명 가능한 nullity Boolean 생성 패턴을 정규화한 뒤, 서로 다른 symbolic value는 TOP으로 병합한다.

6. Loop는 IKOS의 기존 WTO fixpoint lifecycle을 사용한다.

7. 별도의 symbolic widening은 사용하지 않는다.

8. 별도의 symbolic narrowing은 사용하지 않는다.

9. parameter-origin expression으로 환원할 수 없는 exit constraint는
   true로 추상화하여 제거한다.

10. 모든 exit constraint가 제거되면 해당 exit condition은 true이다.

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

21. 분석하지 못한 exception flow는 false가 아니라 Unavailable로 보관한다.
```


## 27. 본 문서와 Transfer Function 명세의 관계

본 문서는 FunctionSummary를 생성하기 위한 전체 분석 구조와 CFG/exit 처리 정책을 정의한다.

각 AR statement에 대한 구체적인 symbolic transfer function은 별도의 transfer-function 명세에서 정의한다.

```text
Transfer Function 명세
    ↓
각 AR statement에서 σ/μ 갱신
    ↓
본 문서의 CFG/loop 처리
    ↓
Exit condition / memory filtering
    ↓
FunctionSummary 생성
```

Transfer-function 명세는 본 문서에서 확정된 다음 정책과 일관되어야 한다.

```text
Σ = (σ, μ)

π 사용 안 함

CFG merge:
    same → same
    proved Boolean construction → 동일 BoolExpr로 정규화
    different → TOP (그 외 기본 규칙)

Loop:
    IKOS WTO lifecycle + symbolic merge

Exit constraint:
    representable → 유지
    unrepresentable → true

Exit μ:
    caller-visible location만 유지
```


## 28. 별도 Pruning 명세와의 관계

본 문서에서 생성된 결과:

```text
FunctionSummary(f)

NormalSummary(f) = {
    C_normal(f),
    μ_normal_exit,f
}

ExceptionSummary(f) = {
    C_exception(f),
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
Boolean 생성/cast/derived-pointer relation은 TOP 또는 true로 약화한다.
Return-value summary나 일반적인 path partitioning은 이 완료 기준의 전제가 아니다.
