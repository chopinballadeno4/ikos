# ikos_ar_parameter_relation_symbolic_transfer_spec.md

## 1. 목적

본 문서는 IKOS AR 기반 Parameter-Relation Symbolic Analysis에서 사용하는 symbolic state와 transfer/merge 규칙을 정의한다.

분석의 목적은 함수 내부의 AR statement와 CFG를 따라 symbolic state를 계산하고, 함수 종료 시 original parameter 기반의 종료 조건과 caller-visible memory state를 얻는 것이다.

caller에서 summary를 실제로 적용하여 normal flow를 pruning하거나 caller memory를 갱신하는 과정은 본 문서의 범위에 포함하지 않는다.


## 2. Symbolic State

분석 상태는 다음과 같다.

```text
Σ = (σ, μ)
```

별도의 pointer state `π`는 사용하지 않는다.

### 2.1 σ

```text
σ : V_SSA → Expr ∪ Loc ∪ {Null} ∪ BoolExpr ∪ {TOP}
```

`σ`는 AR SSA variable을 함수 entry 시점의 parameter-origin symbolic expression 또는 symbolic location에 대응시킨다.

예:

```text
σ(%1) = a₀
σ(%2) = L₁
σ(%3) = a₀ + 1
```

### 2.2 μ

```text
μ : Loc → Expr ∪ Loc ∪ {Null} ∪ BoolExpr ∪ {TOP}
```

`μ`는 symbolic memory location에 저장된 symbolic value를 나타낸다.

예:

```text
μ($3) = a₀
μ(L₁) = a₀ + 1
```

### 2.3 Symbolic location

```text
Loc ::= B | Loc(B, δ)
```

`δ`는 byte 단위의 parameter-origin affine symbolic expression이다.

```text
Loc(B, 0) = B
```

`PointerShift`가 다음과 같고:

```text
result = base + Σ(factorᵢ × operandᵢ)
```

`σ(base) = Loc(B, δ_base)`이며 모든 operand가 symbolic expression으로
환원 가능하면 다음과 같이 offset을 보존한다.

```text
δ_shift = Σ(factorᵢ × σ(operandᵢ))
σ(result) = Loc(B, δ_base + δ_shift)
```

하나의 operand라도 `TOP`이거나 affine symbolic expression으로 표현할 수 없으면:

```text
σ(result) = TOP
```

서로 다른 offset의 location은 서로 다른 memory location으로 취급한다.

```text
Loc(B, δ₁) ≠ Loc(B, δ₂)  if δ₁ ≠ δ₂
```

formal specification과 구현에서는 source-level parameter 이름에 의존하지 않는다.

따라서 `L_p`, `L_q`, `L_r`와 같은 표기는 설명용 예제에서만 사용할 수 있으며, 실제 구현에서는 AR parameter identity 또는 parameter index를 기준으로 symbolic location을 생성한다.

예:

```text
L₀
L₁
L₂
Loc(L₁, δ)
```


### 2.4 Typed pointer/null/Boolean value

Pointer value와 memory location은 DEV_SUMMARY.md §2.5와 동일하게 구분한다.
`P_i^0=PtrValue(L_i)`는 nullable entry pointer 값이며, 기존 Loc 표현을 재사용한다.
Null constant는 별도의 `Null` 값이다. `BoolExpr`는 offset 0인 pointer parameter의
IsNull/NonNull predicate에 대한 정확한 `{0,1}` 값 관계이다. Integer entry atom과
unsigned 1-bit parameter의 `{0,1}` range 정보도 보존한다.

Assignment/copy는 확장된 SymbolicValue를 전달한다. ZExt와 Boolean encoding을
보존하는 same-width bitcast는 origin을 유지한다. Truncation은 operand가 정확히
`{0,1}`이고 결과가 unsigned 1-bit일 때만 보존한다. SExt가 1을 -1로 바꾸는 경우나
range를 보장할 수 없는 truncation은 BoolExpr/Boolean origin을 TOP으로 만든다.
단순히 coefficient가 1인 parameter expression이라는 이유로 range를 가정하지 않는다.

## 3. CFG Symbolic Merge

여러 CFG predecessor의 symbolic state가 하나의 basic block에서 합류하면 pointwise merge를 수행한다.

```text
Σ_merge = Σ₁ ⊔ₛ Σ₂
```

`σ`와 `μ`의 각 symbolic value에 대해 다음 규칙을 사용한다.

```text
MergeVal(v₁, v₂) =
    v₁     if v₁ = v₂
    TOP    otherwise
```

즉:

```text
동일한 symbolic value
→ 유지

서로 다른 symbolic value
→ TOP
```

예:

```text
σ₁(x) = a₀ + 1
σ₂(x) = a₀ + 1

→ σ_merge(x) = a₀ + 1
```

반면:

```text
σ₁(x) = a₀ + 1
σ₂(x) = b₀ + 2

→ σ_merge(x) = TOP
```

`μ`에도 동일한 규칙을 적용한다.

### 3.1 Missing entry

구현에서 `σ`와 `μ`는 sparse map으로 표현할 수 있다. 도달 가능한 state의 map에
key가 존재하지 않는 경우 해당 값은 `TOP`으로 해석한다.

```text
Lookup(M, k) =
    M(k)    if k ∈ dom(M)
    TOP     otherwise
```

따라서 pointwise merge는 두 map의 key 합집합에 대해 다음과 같이 계산한다.

```text
(M₁ ⊔ₛ M₂)(k)
= MergeVal(Lookup(M₁, k), Lookup(M₂, k))
```

예를 들어:

```text
M₁(k) = a₀
k ∉ dom(M₂)

→ MergeVal(a₀, TOP)
→ TOP
```

이는 도달 불가능한 state인 `BOTTOM`과 구분한다. `BOTTOM` predecessor는 merge에
기여하지 않으므로 다음 관계가 성립한다.

```text
BOTTOM ⊔ₛ Σ = Σ
```

이 기본 정책 전에 Section 3.2의 Boolean 생성 정규화를 수행한다. 그 외에는
path correlation을 보존하지 않으며 서로 다른 symbolic relation은 TOP으로 병합한다.

### 3.2 증명 가능한 Boolean 생성 정규화

```text
branch G:   b=1
branch ¬G:  b=0
→ 두 assignment에서 σ(b)=BooleanValue(G)
→ 합류점에서 동일한 BoolExpr 유지
```

현재 G는 동일 pointer value와 null의 PEQ/PNE이다. DEV_SUMMARY.md §9의 전체 definition 검사, comparison/assignment-only block,
acyclic 구성, pointer 재정의 부재 및 모든 entry-to-use 경로의 definition 통과 조건을
모두 충족할 때만 적용한다. 복제된 branch도 모든 definition이 같은 G를 표현해야 한다. 반대 encoding은 BooleanValue(¬G)이다.
메모리 merge나 일반적인 loop-dependent predicate에는 이 예외를 확대하지 않는다.
조건이 불충분하면 상수 assignment와 기존 merge를 그대로 사용한다.


## 4. Branch 처리

IKOS AR에서는 branch condition이 명시적인 branch statement로 표현되지 않고 successor basic block의 `Comparison`으로 나타날 수 있다.

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

IKOS analyzer는 각 successor에 해당 comparison constraint를 반영한 별도의 abstract state를 유지한다.

Symbolic analysis는 각 basic block별 `σ`, `μ` 상태를 유지하고 CFG 합류점에서 Section 3의 symbolic merge를 수행한다.


## 5. LLVM PHI의 IKOS AR 표현

LLVM PHI는 IKOS AR에서 별도의 PHI statement로 유지되지 않을 수 있다.

실제 AR에서는 각 predecessor에서 동일한 AR variable에 assignment하는 형태로 나타난다.

예:

```text
#2 {
    ...
    si32 %.0 = %4
}

#3 {
    ...
    si32 %.0 = %6
}

#4 {
    ... %.0 ...
}
```

따라서 predecessor별 상태가 다음과 같다면:

```text
#2:
σ(%.0) = a₀ + 1

#3:
σ(%.0) = b₀ + 2
```

#4 진입 시 CFG merge를 적용한다.

```text
σ(%.0)
= MergeVal(a₀ + 1, b₀ + 2)
= TOP
```

일반 PHI는 CFG merge로 처리한다. Boolean 생성 PHI는 Section 3.2의 증명된 관계로 assignment를 정규화한 뒤 CFG merge를 적용한다.


## 6. Loop 처리

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


### 6.1 최초 loop-head state

최초 loop-head symbolic state는 loop-entry edge의 상태만 사용한다.

```text
Σ_H^0 =
    ⊔ₛ {
        Σ_OUT(P) |
        P → H is a loop-entry edge
    }
```

최초 상태 계산에서는 아직 back-edge를 반영하지 않는다.

예:

```text
#1 → #2
#3 → #2
```

이면:

```text
Σ_#2^0 = Σ_OUT(#1)
```


### 6.2 Back-edge 반영

loop body를 분석한 후 entry edge와 back-edge의 symbolic state를 merge한다.

```text
Σ_candidate^(k+1)
    = Σ_entry ⊔ₛ Σ_back^k
```

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


### 6.3 Symbolic loop fixpoint

symbolic loop state는 IKOS의 기존 WTO iteration lifecycle과 동일한 basic block 방문 순서에서 반복적으로 갱신한다.

명세 수준에서는 다음과 같이 정의한다.

```text
Σ_H^0 =
    merge of loop-entry states
```

반복:

```text
Σ_candidate^(k+1)
    = Σ_entry ⊔ₛ Σ_back^k
```

```text
Σ_H^(k+1)
    = Σ_H^k ⊔ₛ Σ_candidate^(k+1)
```

다음 조건에서 symbolic state가 안정되었다고 본다.

```text
Σ_H^(k+1) = Σ_H^k
```


### 6.4 Symbolic widening

현재 symbolic domain에는 별도의 widening을 정의하지 않는다.

현재 merge 규칙에서는 서로 다른 symbolic value가 즉시 `TOP`으로 상승한다.

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

따라서 loop-carried symbolic relation은 빠르게 안정된다.

IKOS의 기존 numerical AbstractDomain은 기존 방식대로 join/widening을 수행하고, `σ`, `μ`는 자체 symbolic merge 규칙으로 수렴한다.

```text
IKOS AbstractDomain:
    join → widening → fixpoint → narrowing

Symbolic σ/μ:
    MergeVal → fixpoint
```

따라서 별도의 symbolic widening은 사용하지 않는다.


### 6.5 Symbolic narrowing

현재 symbolic state에는 별도의 narrowing을 적용하지 않는다.

한 번:

```text
σ(x) = TOP
```

으로 추상화된 symbolic relation은 IKOS의 narrowing 단계에서 다시 구체적인 expression으로 복원하지 않는다.

```text
TOP → TOP
```

보다 정밀한 loop symbolic invariant 복원은 향후 확장으로 둔다.


### 6.6 Loop exit

loop fixpoint 이후 loop-exit successor의 `Comparison`은 일반 exit constraint와 동일한 방식으로 처리한다.

예:

```text
%.0 sige 10
```

이고:

```text
σ(%.0) = TOP
```

이면 parameter-origin condition으로 환원할 수 없으므로 Section 8의 `SubstConstraint` 규칙에 따라 해당 constraint를 `true`로 추상화한다.

loop exit의 memory update도 기존 `σ`, `μ` transfer 규칙을 그대로 적용한다.

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


## 7. Exit 분리

함수 종료는 다음 세 종류로 구분한다.

```text
normal_exit
exception_exit
program_exit
```

### Normal exit

정상적인 함수 return이다.

정상 종료 경로에서:

```text
C_normal
μ_normal_exit
```

을 생성한다.

### Exception exit

exception이 caller 방향으로 전파되는 종료이다.

예:

```text
Resume
```

exception exit에서는:

```text
C_exception
μ_exception_exit
```

을 생성한다.

### Program exit

`abort()` 이후 `unreachable` 등 프로그램 자체가 종료되는 경로이다.

program exit는 caller로 복귀하지 않으므로 caller-visible exit summary를 생성하지 않는다.


## 8. Exit Constraint 환원

각 exit 경로의 AR integer/null Comparison 및 합성된 callee exit condition을 σ로
parameter-origin condition으로 환원한다. 조건의 출처와 실제 추출 시점을 명시하며,
이미 liveness로 제거된 값을 최종 invariant에서 다시 얻을 수 있다고 가정하지 않는다.

경로 `i`에 대해:

```text
C_X,i =
    SubstConstraint(
        AnalyzerConstraint_X,i,
        σ_i
    )
```

여기서:

```text
X ∈ {normal, exception}
```


### 8.1 SubstConstraint

지원 atom과 환원 규칙은 다음과 같다.

```text
Atom ::= IntegerComparison | IsNull(P_i^0) | NonNull(P_i^0)
PEQ(pointer, null) 또는 PEQ(null, pointer) → IsNull(EvalPointer(pointer))
PNE(pointer, null) 또는 PNE(null, pointer) → NonNull(EvalPointer(pointer))
BooleanValue(G) == 1 또는 != 0 → G
BooleanValue(G) == 0 또는 != 1 → ¬G
¬IsNull(P) → NonNull(P)
¬NonNull(P) → IsNull(P)
```

Pointer origin은 외부 parameter base이며 offset이 0일 때 지원한다. Null과 Null의
비교는 true/false로 계산한다. 그 외 pointer comparison은 임의의 수치 제약으로
변환하지 않는다. BoolExpr는 exact relation일 때만 부정할 수 있다. TOP의 부정은
여전히 unknown이며 false로 바꾸지 않는다. Normalization 후 지원하지 않는 atom만
true로 약화한다. 이 규칙은 DEV_PRUNING.md의 callee-condition 치환에도 동일하게 적용한다.

각 constraint `c`에 대해:

```text
c에 등장하는 symbolic value가
parameter-origin expression으로 완전히 환원 가능
→ 환원된 constraint 유지
```

반면:

```text
TOP 또는 표현 불가능한 symbolic value 포함
→ 해당 constraint를 true로 추상화하여 제거
```

예:

```text
C_exit =
    (a > 0)
    ∧ (x ≥ 10)

σ(a) = a₀
σ(x) = TOP
```

이면:

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
C_X = a₀ > 0
```

모든 constraint가 표현 불가능하여 제거되면:

```text
C_X = true
```

로 한다.

`true`는 해당 종료에 대해 parameter 기반으로 유효한 추가 제약을 제공하지 못한다는 의미이다.

표현 불가능한 constraint를 제거하는 것은 실제 종료 조건보다 약한 조건을 생성하는 over-approximation이며, 이후 caller에서 pruning 조건으로 사용할 때 정상 종료 가능한 상태를 잘못 제거하지 않는다.


### 8.2 Multiple exits

동일한 종류의 exit가 여러 개 존재하면 각 exit의 환원된 조건을 disjunction으로 결합한다.

```text
C_X = ⋁ᵢ C_X,i
```

따라서:

```text
C_normal(g) = ⋁ᵢ C_normal,i
```

```text
C_exception(g) = ⋁ᵢ C_exception,i
```


## 9. Function Summary

callee `g`의 summary는 다음과 같이 구성한다.

```text
FunctionSummary(g) = {
    NormalSummary(g),
    ExceptionSummary(g)
}
```

현재 return value summary는 생성하지 않는다.


## 9.1 Normal Summary

callee `g`의 normal summary는 최소한 다음으로 구성한다.

```text
NormalSummary(g) =
    (C_normal(g), μ_normal_exit,g)
```

`C_normal(g)`은 callee가 정상적으로 caller에 복귀할 수 있는 parameter-based 조건이다.

`μ_normal_exit,g`은 정상 종료 시 caller-visible external memory의 symbolic 상태를 나타낸다.

현재 globals를 제외하므로 주 대상은 pointer/reference parameter로부터 도달 가능한 parameter-relative location이다.


### 9.2 Exception Summary

exception summary는 다음과 같이 구성한다.

```text
ExceptionSummary(g) =
    (C_exception(g), μ_exception_exit,g)
```

`C_exception(g)`은 exception exit에 도달할 수 있는 parameter-based 조건이다.

`μ_exception_exit,g`은 exception exit 시점의 caller-visible external memory symbolic state이다.


## 9.3 Exit Memory Filtering

exit 시점의 전체 symbolic memory state `μ_exit`에는 callee 내부 `Allocate`로 생성된 local memory location과 caller-visible external location이 함께 존재할 수 있다.

callee summary에는 caller에서 관찰 가능한 location만 포함한다.

```text
ExternalLoc
= {
    L ∈ dom(μ_exit) |
    L is caller-visible
  }
```

따라서 normal exit에서는:

```text
μ_normal_exit
= μ_exit | ExternalLoc
```

exception exit에서는:

```text
μ_exception_exit
= μ_exit | ExternalLoc
```

여기서:

```text
μ_exit | ExternalLoc
```

은 `μ_exit`을 `ExternalLoc`에 제한한 mapping이다.


### 9.4 Callee-local Allocate filtering

callee 내부 `Allocate`로 생성된 local location은 함수 종료 후 caller에서 관찰할 수 없으므로 summary에서 제거한다.

```text
L generated by current callee Allocate
→ L ∉ dom(μ_normal_exit)
→ L ∉ dom(μ_exception_exit)
```

반면 pointer/reference parameter로부터 도달 가능한 parameter-relative location은 caller-visible이면 유지한다.

formal notation에서는 parameter index/identity 기반 location을 사용한다.

예:

```text
L₀
L₁
Loc(L₀, δ)
Loc(L₁, δ)
```

caller-visible이면 exit memory summary에 유지한다.


### 9.5 Exit memory filtering 예제

예를 들어:

```text
μ_exit = {
    $3 ↦ a₀,
    $4 ↦ L₁,
    L₁ ↦ a₀ + 1
}
```

이고 `$3`, `$4`가 현재 callee의 `Allocate`에서 생성된 local location이면:

```text
μ_normal_exit = {
    L₁ ↦ a₀ + 1
}
```

이 된다.

구현에서는 `$` 문자 자체를 기준으로 local location을 판정하지 않는다.

다음과 같이 location의 origin을 기준으로 판단한다.

```text
origin(L) = current-callee Allocate
→ callee-local
```

즉 AR variable/location 이름의 문자열 형식은 filtering의 의미론적 기준이 아니다.


## 10. CFG Merge와 Summary 정밀도

현재 CFG merge는 다음 정책을 사용한다.

```text
same symbolic value
→ preserve

proved nullity Boolean construction
→ normalize assignments to the same BoolExpr

different symbolic value (otherwise)
→ TOP
```

따라서 branch 또는 loop에서 서로 다른 symbolic relation이 합류하면 이후 exit summary의 일부 값이 `TOP`이 될 수 있다.

예:

```text
branch 1:
μ(L₁) = a₀ + 1

branch 2:
μ(L₁) = a₀ + 2
```

이면 merge 후:

```text
μ(L₁) = TOP
```

이다.

이 경우 exit memory summary에서도:

```text
μ_normal_exit(L₁) = TOP
```

으로 유지한다.

반면 exit constraint가 `TOP`에 의존하여 parameter-origin condition으로 환원 불가능한 경우에는 Section 8의 규칙에 따라 해당 constraint를 제거하여 `true`로 추상화한다.


## 11. 향후 CFG 정밀도 확장

현재 정책은 soundness를 유지하면서 구현 복잡도를 제한하기 위해 branch/loop merge 시 서로 다른 symbolic value를 `TOP`으로 추상화한다.

향후 다음 방식으로 확장할 수 있다.

```text
1. branch별 symbolic state 유지
2. symbolic value의 finite disjunction
3. path-sensitive symbolic state
4. path condition과 σ/μ 사이의 correlation 보존
5. loop에 대한 보다 정밀한 symbolic invariant 추론
```

현재 범위에는 Section 3.2의 제한적인 nullity Boolean origin만 포함한다. 일반적인 path partitioning과 임의의 disjunctive value는 후속 확장이다.


## 12. 추가 불변조건

```text
Invariant 20.
μ_normal_exit과 μ_exception_exit에는 caller-visible memory location만
포함한다.

Invariant 21.
callee 내부 Allocate로 생성된 local location은 exit의 전체 μ에는 존재할
수 있지만 caller에 전달되는 exit memory summary에서는 제거한다.

Invariant 22.
exit memory filtering은 AR 이름의 '$' 문자 여부가 아니라 location의
origin이 현재 callee의 Allocate인지 여부를 기준으로 수행한다.

Invariant 23.
CFG merge에서 동일한 symbolic value는 그대로 유지한다.

Invariant 24.
Boolean 생성 정규화 이후 CFG merge에서 서로 다른 symbolic value는 TOP으로 병합한다.

Invariant 25.
LLVM PHI에 의해 predecessor별로 동일한 AR variable에 서로 다른 값이
대입된 경우 CFG merge 규칙으로 처리한다.

Invariant 26.
loop symbolic state는 IKOS의 기존 WTO fixpoint lifecycle과 함께
반복적으로 갱신한다.

Invariant 27.
현재 symbolic σ/μ에는 별도의 widening을 적용하지 않는다.

Invariant 28.
현재 symbolic σ/μ에는 별도의 narrowing을 적용하지 않는다.

Invariant 29.
exit constraint가 parameter-origin expression으로 환원 불가능한 경우
해당 constraint를 true로 추상화하여 제거한다.

Invariant 30.
하나의 exit에서 모든 constraint가 제거되면 해당 exit condition은
true이다.

Invariant 31.
여러 normal exit의 parameter-based condition은 disjunction으로 결합한다.

Invariant 32.
여러 exception exit의 parameter-based condition은 disjunction으로 결합한다.

Invariant 33.
formal symbolic location은 source-level parameter 이름에 의존하지 않고
AR parameter identity 또는 parameter index를 기준으로 표현한다.

Invariant 34.
PointerShift의 affine byte offset은 Loc(B, δ)에 누적하며, 서로 다른 δ의
location은 서로 다른 memory location으로 취급한다.

Invariant 35.
도달 가능한 symbolic state에서 map에 존재하지 않는 entry는 TOP으로 해석하고,
CFG merge 시 다른 predecessor의 값과 MergeVal을 적용한다.
```


## 13. 본 문서의 범위

본 문서는 callee 내부에서 symbolic state를 계산하고 exit summary를 생성하기 위해 필요한 transfer/CFG 규칙을 정의한다.

최종 산출물은 다음과 같다.

```text
NormalSummary(g) =
    (C_normal(g), μ_normal_exit,g)

ExceptionSummary(g) =ㅌ
    (C_exception(g), μ_exception_exit,g)
```

이 summary를 caller call site에서 argument에 맞게 치환하고 normal-flow pruning 및 caller memory update에 적용하는 과정은 별도의 pruning/application 명세에서 정의한다.
