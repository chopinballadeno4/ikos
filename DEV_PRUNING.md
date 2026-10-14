# Caller Summary Pruning 명세

## 1. 문서 목적과 범위

이 문서는 callee에서 생성한 Parameter-Origin Symbolic Exit Summary를 caller의
`Call` 또는 `Invoke`에 적용하는 방법을 정의한다. 기존 IKOS call semantics가
계산한 abstract state를 기준으로 callee의 정상 종료 조건, 예외 종료 조건 및
caller-visible memory effect를 재사용하여 각 continuation을 정제하는 것이
목적이다.

Summary는 기존 IKOS 분석을 대체하지 않는다. Callee resolution, 함수 본문 분석,
return 처리, exception 처리 및 memory 처리는 기존 IKOS semantics를 먼저
수행하며, caller summary pruning은 그 결과에 추가되는 sound한 refinement로만
동작한다.

현재 범위는 다음과 같다.

- 단일 callee로 확정되는 direct 또는 resolved indirect call
- Normal flow와 exception flow의 독립적인 pruning
- Scalar affine condition의 caller 문맥 치환
- Pointer/reference parameter를 통한 caller symbolic memory effect
- `Call`과 `Invoke`의 서로 다른 exception routing
- 정보가 부족하거나 표현할 수 없을 때의 conservative fallback

Global variable summary, 정밀한 aggregate/vector relation, return-value summary,
복수 callee summary join 및 recursive summary는 현재 범위에 포함하지 않는다.

---

## 2. 기호와 용어

이 절에서 문서 전체에 사용되는 기호를 정의한다. 이후 절에서는 같은 기호의
의미를 반복해서 설명하지 않는다.

### 2.1 프로그램 요소

| 기호 | 의미 |
| --- | --- |
| `f` | Call site를 포함하는 caller 함수 |
| `g` | Call site에서 호출되는 callee 함수 |
| `c` | Caller `f` 내부의 `Call` 또는 `Invoke` call site |
| `p_i` | Callee `g`의 `i`번째 formal parameter |
| `arg_i` | Call site `c`의 `i`번째 actual argument |
| `X` | 종료 flow의 종류 |
| `N` | Normal-return flow |
| `E` | Caller로 전파되는 exception flow |

`X`는 문맥에 따라 `N` 또는 `E` 중 하나를 나타낸다.

### 2.2 기존 IKOS abstract state

| 기호 | 의미 |
| --- | --- |
| `A` | 기존 IKOS abstract state |
| `A_c^pre` | Call instruction을 실행하기 직전의 abstract state |
| `A_{c,X}^base` | 기존 IKOS call semantics가 생성한 flow `X`의 state |
| `A_{c,X}^pruned` | 종료 조건을 적용한 뒤의 flow `X` state |
| `A_{c,X}^post` | Memory summary까지 적용한 최종 flow `X` state |
| `⊤` | 해당 domain에서 아무 제약도 없는 상태 또는 값 |
| `⊥` | 도달 불가능한 abstract state |
| `γ(A)` | Abstract state `A`가 나타내는 concrete state 집합 |
| `⊑` | Abstract-domain precision order |
| `⊔` | Abstract-domain join |

이 문서에서 `A_`가 붙은 기호는 기존 IKOS abstract analysis에서 사용하는 상태나
그 상태에 적용할 표현을 의미한다. 특히 위 첨자 `pre`, `base`, `pruned`, `post`는
call site 처리 과정의 서로 다른 시점을 나타낸다.

### 2.3 보조 symbolic state

보조 symbolic state는 다음과 같이 구성한다.

```text
Σ = (σ, μ)
```

| 기호 | 의미 |
| --- | --- |
| `Σ` | Parameter-origin relation을 보존하는 보조 symbolic state |
| `σ` | Scalar value를 symbolic expression에 연결하는 환경 |
| `μ` | Caller-visible location을 symbolic value에 연결하는 memory |
| `Σ_c^pre` | Call 직전의 symbolic state snapshot |
| `Σ_{c,X}^base` | 기존 call effect를 보수적으로 적용한 flow `X`의 symbolic state |
| `Σ_{c,X}^post` | Memory summary까지 적용한 flow `X`의 symbolic state |
| `Expr` | 지원되는 parameter-origin symbolic expression |
| `Loc` | 지원되는 symbolic memory location |
| `TOP` | 정확한 symbolic relation을 표현할 수 없음 |

`Σ`에는 별도의 symbolic path-condition component를 두지 않는다. 치환된 종료
조건은 IKOS state pruning 또는 caller summary 구성에 사용되지만 `σ`나 `μ`에
저장하지 않는다.

### 2.4 Callee entry atom과 summary

| 기호 | 의미 |
| --- | --- |
| `p_i^0` | Callee 진입 시점의 `i`번째 scalar parameter 값 |
| `L_i` | Pointer/reference parameter `p_i`가 가리키는 entry location |
| `L + δ` | `L`로부터 정확한 offset `δ`에 있는 derived location |
| `v^0(L)` | Callee 진입 시점 location `L`의 scalar content |
| `ℓ^0(L)` | Callee 진입 시점 location `L`의 pointer-valued content |
| `C_X(g)` | Callee `g`가 flow `X`로 종료할 수 있는 입력 조건 |
| `μ_X(g)` | Flow `X` 종료 시의 caller-visible memory summary |

Flow별 FunctionSummary는 다음 형식이다.

```text
NormalSummary(g)    = (C_N(g), μ_N(g))
ExceptionSummary(g) = (C_E(g), μ_E(g))
```

각 flow summary의 가용 상태는 다음 중 하나이다.

```text
Summary_X(g) = Known(C_X(g), μ_X(g))
             | Unavailable
```

`Known(false, ∅)`은 해당 flow가 불가능하다고 증명되었음을 뜻한다.
`Known(true, μ_X(g))`은 유용한 종료 조건은 없지만 memory summary는 적용할 수
있음을 뜻한다. `Unavailable`은 sound하게 사용할 summary가 없음을 뜻한다.

### 2.5 Caller 문맥 mapping과 치환 결과

| 기호 | 의미 |
| --- | --- |
| `κ_c` | Callee entry atom을 현재 caller의 IKOS 표현으로 변환하는 mapping |
| `θ_c` | Callee entry atom을 caller의 parameter-origin 표현으로 변환하는 mapping |
| `C_{c,X}^A` | `κ_c`로 치환하여 즉시 IKOS pruning에 사용하는 조건 |
| `C_{c,X}^Σ` | `θ_c`로 치환하여 caller summary 구성에 사용하는 조건 |
| `Effects_{c,X}^A` | IKOS memory refinement에 사용할 caller 문맥의 memory effect |
| `Effects_{c,X}^Σ` | Symbolic memory에 사용할 caller 문맥의 memory effect |

`κ_c`와 `θ_c`는 모두 변경되지 않는 동일한 call-entry snapshot을 기준으로
계산한다.

### 2.6 주요 연산

| 연산 | 의미 |
| --- | --- |
| `ResolveCallees(c, A)` | Call site에서 가능한 callee 집합 계산 |
| `Normalize(v, T)` | 값 `v`를 타입 `T`의 bitwidth와 signedness에 맞게 정규화 |
| `Eval(v, Σ)` | Caller value를 parameter-origin expression으로 평가 |
| `ResolveLoc(v, A)` | Pointer/reference 값의 정확한 caller location 계산 |
| `EvalLoc(v, Σ)` | Pointer/reference 값을 symbolic location으로 평가 |
| `SubstCondition(C, m)` | Mapping `m`을 이용한 조건 치환 |
| `Assume(A, a)` | Atomic constraint `a`로 abstract state `A` 정제 |
| `PruneFlow(A, C)` | Boolean condition `C`를 이용한 flow pruning |
| `RefineMemory(A, E)` | Memory effect `E`로 기존 IKOS memory state 정제 |
| `ApplySymbolicMemory(Σ, E)` | Symbolic memory effect `E` 적용 |

### 2.7 적용 상태

```text
ApplicationStatus_{c,X}
    ∈ {Applied, PartiallyApplied, Pruned, Skipped(reason)}
```

| 상태 | 의미 |
| --- | --- |
| `Applied` | Flow summary가 정상적으로 적용됨 |
| `PartiallyApplied` | 일부 정보가 약화 또는 생략되고 나머지가 적용됨 |
| `Pruned` | 종료 조건에 의해 flow가 `⊥`이 됨 |
| `Skipped(reason)` | Summary 전체를 적용하지 않고 기존 semantics를 유지함 |

이 상태는 diagnostic 정보이며 분석 의미를 변경하지 않는다.

---

## 3. 단계 1 — Pruning의 의미

### 3.1 기존 분석에 대한 refinement

Caller summary pruning은 기존 IKOS call semantics가 계산한 flow별 base state에
callee의 종료 조건을 추가하는 연산이다.

```text
A_{c,N}^pruned = PruneFlow(A_{c,N}^base, C_{c,N}^A)
A_{c,E}^pruned = PruneFlow(A_{c,E}^base, C_{c,E}^A)
```

Pruning 결과는 기존 state보다 같거나 더 정밀해야 한다.

```text
A_{c,X}^pruned ⊑ A_{c,X}^base
```

Summary는 기존 abstract state를 새 상태로 교체하지 않는다. 기존 IKOS가 계산한
numerical constraint, pointer 정보, exception 정보 및 call effect를 보존한 상태에서
summary가 보장하는 조건만 추가한다.

### 3.2 Flow별 적용

Normal condition은 normal-return flow에만 적용한다.

```text
C_N(g) → A_{c,N}^base
```

Exception condition은 exception flow에만 적용한다.

```text
C_E(g) → A_{c,E}^base
```

한 flow의 조건을 다른 flow에 적용하거나 두 조건을 합쳐 하나의 continuation에
적용하지 않는다.

### 3.3 Program exit

프로그램 자체를 종료하는 실행은 caller의 normal continuation이나 exception
continuation으로 전달하지 않는다. 따라서 program-exit condition은 caller의
후속 state를 생성하지 않는다.

Normal flow와 exception flow가 모두 불가능하고 program exit만 가능하면 call site
이후에는 successor state가 없다.

### 3.4 조건의 soundness

Flow `X`로 실제 종료하는 모든 callee 입력은 해당 종료 조건을 만족해야 한다.

```text
ConcreteExitInputs_X(g) ⊆ [[C_X(g)]]
```

따라서 sound한 종료 조건을 기존 base state에 적용해도 실제 실행을 제거하지
않는다.

```text
Reach_{c,X} ⊆ γ(A_{c,X}^pruned)
```

Memory summary 적용은 condition pruning 이후의 별도 단계로 정의한다.

---

## 4. 단계 2 — Summary 적용 가능 여부

### 4.1 Callee resolution

```text
Targets(c) = ResolveCallees(c, A_c^pre)
```

현재 명세에서 summary 적용은 다음 조건을 모두 만족할 때만 가능하다.

```text
Applicable(c, g, A_c^pre) ⇔
    Targets(c) = {g}
    ∧ BodyAvailable(g)
    ∧ TypeCompatible(c, g)
    ∧ ¬RecursiveApplication(c, g)
```

가능한 callee가 없거나 여러 개이면 summary를 적용하지 않는다. External function,
본문을 사용할 수 없는 함수, 호출 타입이 호환되지 않는 함수 및 현재 분석 chain의
recursive call에도 summary를 적용하지 않는다.

이 경우 기존 IKOS call semantics가 계산한 보수적인 결과를 그대로 사용한다.

### 4.2 Flow summary의 독립적인 가용성

Normal summary와 exception summary의 가용성은 서로 독립적으로 판단한다.

```text
Summary_N(g) = Known(C_N(g), μ_N(g)) | Unavailable
Summary_E(g) = Known(C_E(g), μ_E(g)) | Unavailable
```

한 flow의 summary가 `Unavailable`이어도 다른 flow의 `Known` summary는 적용할 수
있다.

### 4.3 Summary 상태별 처리

`Known(false, ∅)`이면 해당 flow를 제거한다.

```text
Summary_X(g) = Known(false, ∅)
⇒ A_{c,X}^post = ⊥
```

`Known(true, μ_X(g))`이면 condition으로 추가 pruning을 수행하지 않지만, flow가
살아 있는 경우 memory summary는 적용한다.

`Unavailable`이면 flow가 불가능하다고 판단하지 않는다.

```text
Summary_X(g) = Unavailable
⇒ A_{c,X}^post = A_{c,X}^base
```

---

## 5. 단계 3 — Parameter–Argument Mapping

### 5.1 두 종류의 mapping

```text
κ_c : CalleeAtom → CallerARValue | CallerSnapshot | unrepresentable
θ_c : CalleeAtom → Expr | Loc | TOP
```

`κ_c`는 현재 call site의 IKOS state를 즉시 pruning하거나 refinement하기 위한
표현을 생성한다. `θ_c`는 callee summary를 caller의 parameter-origin 표현으로
합성하기 위한 표현을 생성한다.

모든 mapping은 다음 snapshot을 기준으로 계산한다.

```text
(A_c^pre, Σ_c^pre)
```

Memory summary의 앞선 update가 뒤의 target 또는 value 계산에 영향을 주어서는 안
된다.

### 5.2 Scalar parameter

```text
κ_c(p_i^0) = Normalize(arg_i, Type(p_i))
θ_c(p_i^0) = Eval(arg_i, Σ_c^pre)
```

Actual argument의 bitwidth, signedness 및 cast 의미를 callee parameter 타입에 맞게
보존할 수 있어야 한다. 안전한 정규화가 불가능하면 각각 `unrepresentable` 또는
`TOP`으로 처리한다.

### 5.3 Pointer/reference parameter

```text
κ_c(L_i) = ResolveLoc(arg_i, A_c^pre)
θ_c(L_i) = EvalLoc(arg_i, Σ_c^pre)
```

정확한 base location을 확인할 수 있을 때 derived location의 offset을 그대로
보존한다.

```text
κ_c(L_i + δ) = κ_c(L_i) + NormalizeOffset(δ)
θ_c(L_i + δ) = θ_c(L_i) + NormalizeOffset(δ)
```

Offset의 단위, pointee type 및 memory range가 호환되지 않으면 정확한 location으로
취급하지 않는다.

### 5.4 Entry-memory atom

```text
κ_c(v^0(L)) = ReadSnapshot(A_c^pre, κ_c(L))
θ_c(v^0(L)) = ReadSymbolicSnapshot(Σ_c^pre, θ_c(L))
```

```text
κ_c(ℓ^0(L)) = ReadPointerSnapshot(A_c^pre, κ_c(L))
θ_c(ℓ^0(L)) = ReadSymbolicPointerSnapshot(Σ_c^pre, θ_c(L))
```

Entry-memory atom은 call이 실행된 뒤의 memory가 아니라 call 직전 snapshot의
content를 나타낸다.

필요한 entry-memory relation이 기존 `Σ_c^pre`에 명시되어 있지 않다면 다음 조건을
모두 만족하는 경우에만 caller parameter 기준의 relation을 lazy하게 생성할 수
있다.

- 정확한 caller parameter-relative location이다.
- Call 이전에 해당 relation을 변경하는 write가 없다.
- 해당 relation이 invalidation되지 않았다.
- 지원되는 scalar 또는 pointer type이다.
- Uninitialized, unknown 또는 multiple-target location이 아니다.

그 외의 경우에는 임의의 entry relation을 생성하지 않는다.

### 5.5 Nested pointer

Pointer-valued entry-memory atom은 같은 규칙을 재귀적으로 적용한다.

```text
θ_c(ℓ^0(L_i)) = L_j 또는 TOP
θ_c(v^0(ℓ^0(L_i) + δ)) = ReadSymbolicSnapshot(Σ_c^pre, L_j + δ)
```

각 dereference 단계에서 정확한 single target과 유효한 memory range가 확인되어야
한다. 어느 한 단계라도 불확실하면 이후 relation을 `TOP`으로 처리한다.

### 5.6 Mapping 실패

Mapping 실패는 flow가 불가능하다는 의미가 아니다.

```text
κ_c(atom) = unrepresentable
θ_c(atom) = TOP
```

실패한 atom은 이후 condition 또는 memory 적용 단계의 conservative fallback
규칙에 따라 처리한다.

---

## 6. 단계 4 — 종료 조건 치환

### 6.1 Caller 조건 생성

```text
C_{c,X}^A = SubstCondition(C_X(g), κ_c)
C_{c,X}^Σ = SubstCondition(C_X(g), θ_c)
```

`C_{c,X}^A`는 현재 IKOS abstract state의 즉시 pruning에 사용한다.
`C_{c,X}^Σ`는 살아남은 flow의 caller exit-condition을 구성할 때 사용한다.

### 6.2 조건 형식

종료 조건은 DNF로 정규화한다.

```text
C_X(g) = D_1 ∨ ... ∨ D_m
D_i    = c_i1 ∧ ... ∧ c_in
```

Affine expression 내부의 모든 callee entry atom을 재귀적으로 치환하며, 다음
정보를 보존한다.

- Integer와 pointer type
- Bitwidth
- Signedness
- Comparison predicate
- Exact location과 offset

### 6.3 표현 가능한 atom

Atomic constraint의 모든 operand가 안전하게 치환되면 같은 predicate를 유지한
caller constraint를 생성한다.

```text
SubstCondition(e_1 op e_2, m)
    = SubstExpr(e_1, m) op SubstExpr(e_2, m)
```

### 6.4 표현 불가능한 atom

Atomic constraint의 일부를 안전하게 치환할 수 없으면 해당 atom만 `true`로
약화한다.

```text
SubstCondition(c_ij, m) = true
```

이를 `false`로 바꾸거나 해당 atom이 포함된 전체 disjunct를 제거하지 않는다.
Mapping 실패는 불가능성의 증거가 아니기 때문이다.

### 6.5 Boolean 단순화

치환 후 다음 규칙으로 조건을 단순화한다.

```text
D ∧ true  = D
D ∧ false = false
empty conjunction = true
```

```text
C ∨ false = C
C ∨ true  = true
empty disjunction = false
```

Disjunction 구조는 유지한다. 여기의 논리 disjunction `∨`는 abstract-domain join
`⊔`과 구분한다.

원래 조건이 `true` 또는 `false`이면 치환 뒤에도 그대로 유지한다.

---

## 7. 단계 5 — Flow별 Condition 적용 및 Pruning

### 7.1 Conjunction 적용

```text
PruneFlow(A, c_1 ∧ ... ∧ c_n)
    = Assume(...Assume(Assume(A, c_1), c_2)..., c_n)
```

```text
PruneFlow(A, true)  = A
PruneFlow(A, false) = ⊥
PruneFlow(⊥, C)     = ⊥
```

각 atomic constraint는 같은 conjunction state에 순차적으로 적용한다. 중간 결과가
`⊥`이면 나머지 atom을 처리하지 않는다.

### 7.2 Disjunction 적용

```text
PruneFlow(A, D_1 ∨ ... ∨ D_m)
    = ⊔ { PruneFlow(A, D_i) | PruneFlow(A, D_i) ≠ ⊥ }
```

모든 disjunct는 동일한 입력 state `A`에서 독립적으로 시작한다. 하나의 state에
disjunct를 순차 적용하면 conjunction 의미가 되므로 허용하지 않는다.

살아남은 disjunct가 없으면 결과는 `⊥`이다. 여러 disjunct가 살아남으면 기존 IKOS
abstract domain의 join으로 결합한다. Join 과정에서 disjunctive precision이 일부
손실될 수 있지만 soundness는 유지한다.

### 7.3 Flow별 pruning

```text
A_{c,N}^pruned = PruneFlow(A_{c,N}^base, C_{c,N}^A)
A_{c,E}^pruned = PruneFlow(A_{c,E}^base, C_{c,E}^A)
```

Summary를 적용할 수 없는 flow는 base state를 유지한다.

IKOS domain이 치환된 atom을 표현하지 못하면 그 atom으로 state를 `⊥`으로 만들지
않고 현재 state를 보존한다. 반면 sound하게 표현된 constraint와 기존 state의
교집합이 실제로 `⊥`이면 유효한 pruning 결과로 인정한다.

### 7.4 Symbolic condition의 사용

`C_{c,X}^Σ`는 `Σ`에 직접 `Assume`하지 않는다. `Σ`에는 path-condition component가
없으므로 살아남은 flow의 caller exit-condition을 생성하거나 상위 함수 summary를
합성하는 입력으로 전달한다.

Memory summary는 `A_{c,X}^pruned`가 `⊥`이 아닌 경우에만 적용한다.

---

## 8. 단계 6 — Exit Memory Summary 적용

### 8.1 Flow별 적용

Normal memory summary는 살아남은 normal flow에만 적용하고 exception memory
summary는 살아남은 exception flow에만 적용한다.

```text
A_{c,X}^pruned = ⊥ ⇒ Memory summary를 적용하지 않음
```

`Summary_X(g)`가 `Unavailable`인 경우에도 memory summary를 적용하지 않는다.

### 8.2 Memory entry 변환

```text
Effects_{c,X}^A = {
    κ_c(L) ↦ κ_c(e)
    | L ↦ e ∈ μ_X(g)
}
```

```text
Effects_{c,X}^Σ = {
    θ_c(L) ↦ θ_c(e)
    | L ↦ e ∈ μ_X(g)
}
```

모든 target과 value를 동일한 call-entry snapshot에서 먼저 계산한 뒤 update를
수행한다. 한 entry의 update 결과를 다른 entry의 mapping 입력으로 사용하지 않는다.

### 8.3 기존 IKOS memory refinement

```text
A_{c,X}^post = RefineMemory(A_{c,X}^pruned, Effects_{c,X}^A)
```

다음 조건을 모두 만족하는 relation만 추가 refinement로 적용한다.

- Caller target이 정확히 하나의 유효한 location으로 결정된다.
- Value를 IKOS domain에서 sound하게 표현할 수 있다.
- Target과 value의 type, bitwidth 및 memory range가 호환된다.
- Alias 또는 overlapping write 충돌이 없다.
- 해당 relation이 flow `X`의 모든 실행에 대해 성립한다.

IKOS의 interprocedural 분석은 callee를 caller state에서 직접 분석하므로
`A_{c,X}^base`에 callee의 memory effect가 이미 포함된다. 현재 구현은 같은 write를
summary로 다시 실행하지 않고 이 기존 결과를 보존한다.

```text
A_{c,X}^post = A_{c,X}^pruned
```

즉 현재 `μ_X(g)`는 caller의 보조 symbolic state와 상위 FunctionSummary 합성에
사용한다. IKOS memory domain에 대한 별도 `RefineMemory` 구현은 후속 범위이며,
구현되더라도 Summary memory로 기존 IKOS memory state 전체를 교체해서는 안 된다.

### 8.4 Symbolic memory 적용

```text
Σ_{c,X}^post
    = ApplySymbolicMemory(Σ_{c,X}^base, Effects_{c,X}^Σ)
```

정확한 relation은 flow별 base symbolic state에 기록한다. Pointer-valued effect의
value는 `Loc`으로 저장한다.

### 8.5 `TOP` value

Value가 `TOP`이면 기존 IKOS state에는 추가 refinement를 수행하지 않는다. 기존
IKOS 결과가 해당 call context에서 더 정확할 수 있으므로 이를 `TOP`으로
덮어쓰지 않는다.

Symbolic state에서는 해당 target의 정확한 relation을 유지할 수 없으므로 `TOP`을
기록한다.

```text
θ_c(e) = TOP ⇒ μ_{c,X}^post(θ_c(L)) = TOP
```

### 8.6 Target mapping 실패

Target을 정확히 결정할 수 없으면 임의의 strong update를 수행하지 않는다.

- 기존 IKOS memory state는 그대로 유지한다.
- Symbolic state에는 기존 call semantics의 conservative invalidation을 유지한다.
- 기존 invalidation이 충분하지 않으면 가능한 may-alias location을 invalidation한다.

### 8.7 동일 target과 overlapping range

여러 callee location이 동일한 caller location으로 변환되는 경우 다음과 같이
처리한다.

- 모든 value가 동일하면 하나의 update로 결합한다.
- Value가 충돌하면 IKOS에는 추가 refinement를 적용하지 않는다.
- Value가 충돌하면 symbolic target은 `TOP`으로 처리한다.

Memory range가 일부 겹치는 경우 exact byte-level combination을 sound하게 구성할 수
있을 때만 결합한다. 그 외에는 충돌한 range에 같은 fallback을 적용한다.

### 8.8 누락된 entry

Memory summary에 entry가 없다는 사실은 해당 location이 변경되지 않았다는 뜻이
아니다.

```text
L ∉ dom(μ_X(g)) ⇏ μ_{c,X}^post(L) = μ_c^pre(L)
```

빈 memory summary도 모든 memory가 보존됨을 의미하지 않는다. 이는 추가로 적용할
수 있는 memory relation이 없다는 뜻이며, 해당 location은 기존 IKOS call
semantics와 conservative symbolic invalidation 결과를 따른다.

---

## 9. 단계 7 — Call과 Invoke의 Flow 처리

### 9.1 Exception state 구성요소

기존 exception abstract state의 흐름은 개념적으로 다음 요소로 구분한다.

```text
normal
caught_exceptions
propagated_exceptions
```

Summary의 normal flow는 `normal`에 대응한다. `Invoke`의 exception flow는
`caught_exceptions`에 대응하며, 일반 `Call`의 exception flow는 최종적으로
`propagated_exceptions`에 대응한다.

### 9.2 공통 CallBase 처리 위치

공통 처리 순서는 다음과 같다.

```text
기존 callee 분석
→ callee exit invariant 획득
→ normal/exception base flow 분리
→ summary condition pruning
→ summary memory effect 적용
→ Call/Invoke별 exception routing
```

Summary는 callee exit state와 flow별 base state가 준비된 뒤, 일반 `Call`의
caught-to-propagated 변환 또는 `Invoke` successor edge 선택이 이루어지기 전에
적용한다.

### 9.3 일반 Call

`A_{c,N}^post`는 call 다음의 normal statement로 전달한다.
`A_{c,E}^post`는 propagated exception flow로 전달한다.

```text
A_{c,N}^post = ⊥ ⇒ normal continuation 없음
A_{c,E}^post = ⊥ ⇒ propagated exception continuation 없음
```

두 flow는 독립적이므로 하나가 제거되어도 다른 하나는 계속 전달될 수 있다.

### 9.4 Invoke

공통 call 처리 직후 normal 결과는 `normal` component에, exception 결과는
`caught_exceptions` component에 둔다.

Normal successor edge에서는 normal component만 현재 state로 선택하고 caught
exception component를 버린다. Exception successor edge에서는 caught exception
component를 현재 state로 선택하고 normal component를 버린다.

따라서 `Invoke`에는 다음 네 결과가 가능하다.

- Normal edge와 exception edge가 모두 살아 있다.
- Normal edge만 살아 있다.
- Exception edge만 살아 있다.
- 두 edge가 모두 제거된다.

### 9.5 Program exit와 return value

Program-exit flow는 어떤 exception component에도 넣지 않으며 caller successor를
생성하지 않는다.

Return-value summary는 현재 범위에 포함하지 않는다. Normal flow의 기존 IKOS
return 처리는 유지한다. Symbolic call result는 normal flow에서 `TOP`으로
처리하며, exception flow와 program-exit flow에는 return result를 생성하지 않는다.

### 9.6 기존 semantics 보존

Summary가 적용되지 않은 flow는 기존 IKOS flow를 그대로 유지한다.
`Known(false, ∅)`인 flow만 명시적으로 제거한다.

현재 명세는 singleton callee만 지원하므로 callee별 summary 결과의 join은 수행하지
않는다.

---

## 10. 단계 8 — Conservative Fallback 정책

### 10.1 기본 원칙

Summary 적용은 기존 IKOS call semantics의 결과에 대한 추가 refinement이다.

```text
A_{c,X}^post ⊑ A_{c,X}^base
```

Summary를 안전하게 적용할 수 없으면 기존 base state를 유지한다.

```text
Fallback_A(A_{c,X}^base) = A_{c,X}^base
```

반면 해당 flow의 불가능성이 명시적으로 증명되면 flow를 제거한다.

```text
Summary_X(g) = Known(false, ∅)
⇒ A_{c,X}^post = ⊥
```

```text
Unavailable       ⇏ ⊥
Unrepresentable   ⇏ ⊥
TOP               ⇏ ⊥
Known(false, ∅)    ⇒ ⊥
```

### 10.2 Summary 유효성 검증

Summary를 적용하기 전에 다음 조건을 확인한다.

- Summary 형식이 올바르다.
- Condition과 memory entry의 타입 정보가 일관된다.
- Parameter-origin atom이 callee signature와 일치한다.
- Location과 offset이 유효하다.
- Summary가 지원되는 분석 규칙으로 생성되었다.

검증에 실패한 summary는 `Known`으로 취급하지 않는다.

```text
Invalid(Summary_X(g)) ⇒ Summary_X(g) = Unavailable
```

잘못된 summary를 근거로 flow를 `⊥`으로 만들지 않는다.

### 10.3 Summary 전체를 적용할 수 없는 경우

다음 경우에는 summary 전체를 적용하지 않는다.

- Callee가 resolve되지 않았다.
- 가능한 callee가 여러 개다.
- Callee body를 사용할 수 없다.
- External call이다.
- Recursive application이다.
- Call signature가 호환되지 않는다.
- Summary가 `Unavailable`이거나 유효성 검증에 실패했다.

Abstract state에는 기존 base state를 유지한다. Symbolic state에는 기존 call
semantics가 요구하는 conservative invalidation을 적용한다.

### 10.4 Condition의 국소 fallback

하나의 atomic constraint를 변환할 수 없으면 해당 atom만 `true`로 약화하고
변환 가능한 나머지 조건을 계속 적용한다.

```text
Unrepresentable(c_ij) ⇒ SubstCondition(c_ij, m) = true
```

Caller 조건으로 변환되었지만 IKOS domain에서 표현할 수 없는 atom도 현재 state를
유지하는 방식으로 처리한다.

```text
UnsupportedByDomain(a) ⇒ Assume(A, a) = A
```

Sound하게 표현된 atom을 적용한 결과가 `⊥`인 경우에는 유효한 pruning으로
인정한다.

### 10.5 Memory entry의 국소 fallback

Memory entry의 적용 실패는 가능한 한 해당 entry에 국한한다.

Target을 정확히 결정할 수 없으면 IKOS에 임의의 strong update를 수행하지 않고,
symbolic state에서는 영향을 받을 수 있는 location을 conservative하게
invalidation한다.

Value를 표현할 수 없으면 IKOS에 추가 refinement를 수행하지 않고 symbolic
target을 `TOP`으로 처리한다.

Alias 또는 overlap 충돌을 sound하게 결합할 수 없으면 충돌한 entry에만 다음
정책을 적용한다.

- IKOS state: 추가 refinement 없음
- Symbolic state: 영향받는 location을 `TOP`으로 처리
- 충돌하지 않는 entry: 계속 적용

### 10.6 Flow 독립성

```text
Failure_N ⇏ Failure_E
Failure_E ⇏ Failure_N
```

한 flow의 summary 적용 실패는 다른 flow의 유효한 summary 적용을 취소하지 않는다.
한 flow 내부에서도 특정 atom 또는 memory entry의 실패 때문에 적용 가능한 나머지
정보를 버리지 않는다.

### 10.7 Fallback 처리 순서

각 flow에 대해 다음 순서로 처리한다.

1. Summary의 유효성과 적용 가능 여부를 검사한다.
2. `Unavailable`이거나 invalid하면 기존 base state를 유지한다.
3. `Known(false, ∅)`이면 해당 flow를 `⊥`으로 만든다.
4. Condition을 caller 문맥으로 변환한다.
5. 변환 불가능한 condition atom만 `true`로 약화한다.
6. 표현 가능한 condition으로 flow를 pruning한다.
7. 살아남은 flow에 적용 가능한 memory entry만 반영한다.
8. 불확실한 symbolic memory relation에는 `TOP` 또는 invalidation을 적용한다.
9. 기존 IKOS 결과보다 근거 없이 더 강한 state를 생성하지 않는다.

### 10.8 핵심 보수성 규칙

```text
Unknown         ⇏ Impossible
Unrepresentable ⇏ Impossible
Missing         ⇏ Unchanged
ProvenImpossible ⇒ ⊥
```

Summary 적용 실패는 기존 IKOS 결과를 보존하거나 symbolic 정보를 약화시키는
방향으로 처리한다. 명시적으로 증명된 불가능성만 flow pruning에 사용한다.

---

## 11. 단계 9 — End-to-End Caller Summary Pruning 알고리즘

### 11.1 입력

```text
Input(c) = (A_c^pre, Σ_c^pre, Targets(c), Summary(g))
```

처리를 시작할 때 `A_c^pre`와 `Σ_c^pre`를 변경되지 않는 call-entry snapshot으로
보존한다.

### 11.2 Base flow 생성

먼저 기존 IKOS call semantics와 conservative symbolic call semantics를 수행한다.

```text
(A_{c,N}^base, A_{c,E}^base)
    = ExistingCallSemantics(c, A_c^pre)
```

```text
(Σ_{c,N}^base, Σ_{c,E}^base)
    = ConservativeSymbolicCall(c, Σ_c^pre)
```

### 11.3 적용 가능성 검사와 mapping 생성

`Applicable(c, g, A_c^pre)`가 성립하지 않으면 base flow를 그대로 routing한다.

적용 가능하면 동일한 call-entry snapshot에서 mapping을 생성한다.

```text
κ_c = BuildAbstractMapping(c, A_c^pre)
θ_c = BuildSymbolicMapping(c, Σ_c^pre)
```

### 11.4 Flow별 적용

각 `X ∈ {N, E}`에 대해 다음 절차를 독립적으로 수행한다.

1. `Summary_X(g)`의 가용성과 유효성을 확인한다.
2. `Unavailable`이면 해당 flow의 base state를 유지한다.
3. `Known`이면 `C_{c,X}^A`와 `C_{c,X}^Σ`를 생성한다.
4. `C_{c,X}^A`를 `A_{c,X}^base`에 적용한다.
5. 결과가 `⊥`이면 해당 flow를 제거하고 memory summary를 적용하지 않는다.
6. Flow가 살아남으면 모든 memory target과 value를 call-entry snapshot에서
   변환한다.
7. 표현 가능한 memory relation으로 IKOS state를 refinement한다.
8. Symbolic memory에 정확한 relation을 적용하고 불확실한 relation에는 `TOP` 또는
   invalidation을 적용한다.
9. 살아남은 `C_{c,X}^Σ`를 caller exit-condition 생성 과정에 전달한다.

### 11.5 통합 의사코드

```text
ApplyCallerSummary(c, A_pre, Sigma_pre):

    entry_A     := snapshot(A_pre)
    entry_Sigma := snapshot(Sigma_pre)

    (base_A_N, base_A_E) := ExistingCallSemantics(c, entry_A)
    (base_S_N, base_S_E) := ConservativeSymbolicCall(c, entry_Sigma)

    if not Applicable(c, g, entry_A):
        return RouteUsingExistingSemantics(
            base_A_N, base_S_N,
            base_A_E, base_S_E)

    kappa := BuildAbstractMapping(c, entry_A)
    theta := BuildSymbolicMapping(c, entry_Sigma)

    for X in {Normal, Exception}:
        if Summary_X(g) is Unavailable or Invalid:
            post_A_X := base_A_X
            post_S_X := base_S_X
            status_X := Skipped(reason)
            continue

        (C_X, M_X) := Summary_X(g)

        C_A_X := SubstCondition(C_X, kappa)
        C_S_X := SubstCondition(C_X, theta)

        pruned_A_X := PruneFlow(base_A_X, C_A_X)

        if pruned_A_X is bottom:
            post_A_X := bottom
            discard symbolic successor for X
            status_X := Pruned
            continue

        effects_A_X := TranslateMemory(M_X, kappa, entry_A)
        effects_S_X := TranslateMemory(M_X, theta, entry_Sigma)

        post_A_X := RefineMemory(pruned_A_X, effects_A_X)
        post_S_X := ApplySymbolicMemory(base_S_X, effects_S_X)

        forward C_S_X to caller exit-condition construction
        status_X := Applied or PartiallyApplied

    return RouteCallOrInvoke(post states and statuses)
```

### 11.6 Flow routing

일반 `Call`은 살아남은 normal state를 다음 statement로 전달하고 exception state를
propagated exception으로 전달한다.

`Invoke`는 살아남은 normal state를 normal successor edge로, exception state를
exception successor edge로 전달한다.

`⊥`인 flow에는 successor를 생성하지 않는다. Program-exit flow에도 successor를
생성하지 않는다.

### 11.7 결과

```text
Result(c) = (
    A_{c,N}^post,
    Σ_{c,N}^post,
    A_{c,E}^post,
    Σ_{c,E}^post,
    ApplicationStatus_{c,N},
    ApplicationStatus_{c,E}
)
```

`⊥`으로 제거된 flow의 symbolic state는 successor로 전달하지 않는다.

---

## 12. 핵심 불변조건

### 12.1 Refinement

```text
A_{c,X}^post ⊑ A_{c,X}^base
```

Summary 적용은 기존 IKOS 결과에 concrete behavior를 추가하지 않는다.

### 12.2 Soundness

```text
Reach_{c,X} ⊆ γ(A_{c,X}^post)
```

Summary 적용 후에도 실제로 flow `X`에 도달할 수 있는 모든 concrete state가
결과에 포함되어야 한다.

### 12.3 Flow separation

```text
Summary_N(g) → normal flow only
Summary_E(g) → exception flow only
```

Normal flow와 exception flow의 condition, memory effect, 실패 및 pruning 결과를
서로 섞지 않는다.

### 12.4 Entry-snapshot consistency

Parameter mapping, condition substitution 및 memory effect translation은 모두 같은
`(A_c^pre, Σ_c^pre)`를 기준으로 수행한다. Memory update의 적용 순서가 다른
entry의 target 또는 value 계산 결과를 바꾸지 않는다.

### 12.5 불가능성과 불확실성의 구분

```text
Known(false, ∅) ⇒ A_{c,X}^post = ⊥
Unavailable ∨ Unrepresentable ∨ TOP ⇏ A_{c,X}^post = ⊥
```

### 12.6 Memory 적용 조건

```text
A_{c,X}^pruned = ⊥ ⇒ ¬ApplyMemory_X
```

살아남지 않은 flow에는 memory summary를 적용하지 않는다.

### 12.7 기존 Call/Invoke semantics 유지

Callee resolution, callee execution, return-value 처리, exception routing,
local-memory deallocation 및 `Invoke` edge selection은 기존 IKOS semantics를
유지한다. Caller summary pruning은 callee exit state가 생성된 이후부터
`Call`/`Invoke` flow가 최종 routing되기 전까지의 refinement 단계로만 동작한다.

---

## 13. 구현 위치와 현재 확장 경계

### 13.1 구현 위치

Summary 적용 지점은 공통 `CallBase` 처리에서 다음 두 시점 사이이다.

```text
callee exit state와 exception flow가 준비된 시점
                         ↓
              caller summary pruning
                         ↓
Call의 caught-to-propagated 변환 또는 Invoke edge 선택
```

이 위치에서 normal flow와 exception flow가 모두 존재하므로 동일한 공통 로직으로
각 flow를 독립적으로 refinement할 수 있다.

### 13.2 현재 구현 범위

현재 IKOS 본 분석 구현은 interprocedural dynamic inlining에서 callee가 정확히
하나로 결정되고 callee 분석과 summary 저장이 완료된 경우에만 적용한다.

`SummaryStore`에는 현재 분석 프로세스가 직접 생성한 summary만 들어간다. 별도
파일이나 외부 입력에서 summary를 역직렬화하지 않으며, 잘못된 parameter index나
표현 불가능한 operand는 적용 단계에서 해당 atom 또는 entry만 보수적으로 생략한다.

- Normal summary는 `normal` component에만 적용한다.
- Exception summary는 propagated exception을 `caught_exceptions`로 모은 뒤 해당
  component에만 적용한다.
- 명시적인 exception-exit을 분석하지 못했거나 함수 안에 보조 symbolic 분석이
  예외를 모델링하지 않는 일반 `Call`이 있는 경우 exception summary는
  `Unavailable`이다. exception-exit 부재만으로 일반 `Call`이 외부로 전파할 수 있는
  예외 flow를 `Known(false)`로 간주하지 않는다.
- `Call`의 caught-to-propagated 변환 및 `Invoke`의 edge selection은 기존 IKOS
  처리를 그대로 사용한다.
- DNF의 각 conjunction은 같은 base state에서 시작하고, 결과를 IKOS join으로
  결합한다.
- IKOS machine-integer API로 직접 표현할 수 있는 constant, 단일 변수와 상수,
  또는 offset 없는 두 변수 비교만 적용한다. 그 외 atom은 `true`로 약화한다.
- Unsigned affine comparison은 유한 bitwidth에서 항 이항이 동치가 아닐 수 있으므로
  direct parameter/constant 형태만 적용한다.
- Intraprocedural의 context-insensitive call semantics에는 callee별 normal/exception
  base flow가 없으므로 현재 caller pruning을 적용하지 않는다.
- 개발용 적용 로그는 기존 `--trace-ar-stmts` 옵션으로만 출력하며, 옵션 유무는
  pruning 수행 여부에 영향을 주지 않는다.

보조 symbolic 분석에서는 pointer argument가 가리키는 symbolic memory를 먼저
`TOP`으로 invalidation한 다음, 알려진 flow별 `μ_X(g)`만 다시 적용한다. 여러
summary entry가 같은 caller symbolic location으로 치환되면서 값이 충돌하면 해당
location을 `TOP`으로 처리한다.

Wrapping 가능성이 있는 integer add/sub/mul/shift는 affine relation으로 보존하지
않는다. AR의 `no-wrap` 정보가 있는 연산만 affine symbolic expression으로 유지한다.

### 13.3 후속 확장 대상

다음 항목은 현재 명세의 보수적인 동작을 유지한 상태에서 후속 연구로 확장한다.

- 복수 callee의 flow별 summary join
- Recursive SCC의 fixpoint summary
- Return-value symbolic summary
- Global variable과 heap object summary
- IKOS memory domain에 대한 별도 summary 기반 `RefineMemory`
- Modifies-set을 이용한 unchanged memory 보존
- Aggregate, vector 및 byte-range memory relation
- Disjunctive abstract domain 또는 path partitioning
- 복잡한 alias relation과 weak update 정밀화
