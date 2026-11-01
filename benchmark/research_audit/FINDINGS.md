# 연구 감사 결과 — 2026-11-01

## 결과 요약

현재 checkout의 설치된 IKOS 3.5 실행 파일과 interval domain으로 확인했다.
27개 기본 entry와 31개 오류 주입 변형, 합계 **58회** 분석했다.
결과 원본: `/private/tmp/ikos-research-audit-complete/{report.md,results.json}`.
다른 환경에서는 README의 도구로 재실행하여 새 결과를 생성한다.

| 구분 | 결과 |
|---|---|
| source-line별 기대 결과 일치 | 114개 |
| 기대 결과 불일치 | 4개: 소멸자 관련 2개, PHI 포인터 Boolean-origin 2개 |
| 알려진 signed NE interval 한계 | 1개 |
| float/double DBZ 검사 미지원 관측 | 2개 |
| 오류를 주입한 31개 변형 | 모두 해당 흐름에서 검출; 반대 흐름의 safe 조건도 유지 |
| Complete normal/exception 조건 의미 검증 | 46개 component, 표본 내 불일치 0 |

여기서 component 46개는 서로 다른 46개 함수를 뜻하지 않는다. 여러 entry에서
검증한 함수별 normal/exception 조건의 합계다. 총 검사 수는 내부 UVA 등 보조
검사까지 포함하면 더 많으며, 위 숫자는 의도한 source-line/checker 목표 단위다.

`audit_packet`은 여섯 parameter, 두 단계 call, unsigned wrapping identity,
세 종류의 종료, 두 pointer의 반대 nullity, output pointer의 서로 다른 store,
64비트 합, 16비트 row를 함께 사용한다. Normal과 exception의 7개 지점씩 총
14개 목표가 모두 safe였다. Writable(out)을 만족하는 stack storage에서
normal은 out=7, exception은 out=9인 사실까지 assertion으로 확인했다.

## 1. 우선 수정 필요: 예외 소멸자의 메모리 효과를 놓침

최소 재현: [destructor_effect.cpp](destructor_effect.cpp).

```cpp
int divisor = 1;
try {
  throw ClearingError(&divisor);
} catch (...) {
} // ~ClearingError()가 divisor=0으로 변경
return 100 / divisor;
```

- 실제 C++ 실행: UBSan이 마지막 줄의 `division by zero`를 검출했다.
- 현재 `--analyses=dbz`: safe 1, warnings 0. **실제 오류 누락이다.**
- `audit_destructor_invalidation`의 표식 `destructor_effect`에서도 DBZ가 safe로
  기록된다. 모듈 전체의 다른 경고 유무가 이 나눗셈의 잘못된 판정을 대신하지 않는다.

원인은 `analyzer/include/ikos/analyzer/analysis/execution_engine/numerical.hpp`의
`exec_end_catch()`가 빈 함수인 것이다. Throw 시 전달한 destructor를 catch 종료의
실제 메모리 효과로 실행하지 않아 divisor=1이라는 오래된 정보를 유지한다.

Symbolic proof의 `runtime_call(LibcppEndCatch)`는 미확인 cleanup을 Unsupported로
처리하지만, Partial로 낮춘 뒤 보존하는 **기존 native state 자체가 보수적이지
않다.** 따라서 false/Partial을 유지하는 것만으로 이 오류를 막을 수 없다.

기존 `/Users/baesungmin/Projects/ikos` checkout의 같은 native 함수도 빈 구현임을
소스로 확인했다. 기존 바이너리는 이 예제의 LLVM 상수를 import하지 못해 실행
결과를 직접 비교하지는 못했다. 이 문제를 새 summary pruning이 만들어 낸 회귀로
단정하지 않으며, 기존 모델에서 물려받은 누락으로 판단한다. 어느 경우든 현재
연구의 전체 분석기 안전성 요구를 충족하지 못하는 반례다.

필요 작업:

1. Native exception state가 throw의 object/destructor identity와 catch/rethrow
   소유권을 추적하고, 실제 마지막 catch 종료에서 cleanup을 실행해야 한다.
2. 모델링 가능한 destructor의 memory effect를 native state에 적용하고 관련
   `σ/μ/ρ` 사실을 무효화해야 한다.
3. 미확인 cleanup은 외부로 도달 가능한 수정 대상을 안전하게 havoc해야 한다.
   그 대상조차 모르면 기존 메모리 사실을 그대로 유지해서는 안 된다.
4. Rethrow 도중에는 destructor를 조기에 실행하지 않도록 별도 음성 대조군이 필요하다.

이번 감사에서는 분석기 구현과 DEV 명세를 수정하지 않았다.

## 2. 같은 누락의 반대 방향: 소멸자 program exit 이후에도 검사함

재현: `audit_destructor_program_exit(int n)`.

`TerminatingError::~TerminatingError()`는 abort한다. 따라서 catch 다음의 `100/n`은
도달 불가능해야 하지만 DBZ가 `divisor might be zero`를 보고한다.

이 함수의 symbolic 요약은 이미 다음과 같이 올바르게 생성된다.

```text
C_normal = false [Complete]
C_exception = false [Complete]
```

즉, summary 생성기의 cleanup/종료 판정과 native executor의 처리가 일치하지 않는다.
고칠 위치는 실제 cleanup 실행 시점의 flow 전이이다. 함수 전체의 false/Complete를
근거로 그 이전의 오류 검사까지 지우는 방식은 잘못이다.

## 3. 연구 확장 지점: PHI 포인터를 Boolean으로 전달하면 양쪽 nullity를 잃음

재현: `audit_bool_origin_phi`, 표식 `bool_phi_n`, `bool_phi_e`.

```cpp
int* pointer = flag ? &value : nullptr;
try {
  split_null_bool(pointer == nullptr); // null이면 throw, 그 외 return
  __ikos_assert(pointer != nullptr);
} catch (...) {
  __ikos_assert(pointer == nullptr);
}
```

두 assertion은 참이지만 둘 다 warning이다. `split_null_bool`의 normal/exception
Boolean 요약은 Complete다. Caller의 Boolean→pointer 역치환이 실패한다.

`find_nullity_bool_origin()`은 pointer의 AR definition이 둘 이상이면 거부한다.
이 조건식은 PHI가 두 assignment로 내려가므로 그 제한에 걸린다. **DEV_SUMMARY.md
§9도 formal 이외 pointer에 최대 한 definition만 허용하므로, 현재 명세와 구현이
불일치하는 버그는 아니다.** 넓은 연구 의도에서는 보완할 정밀도 범위다.

동일 시험에서 `pointer=fresh_pointer()`처럼 한 definition이면 normal과 exception의
assertion이 모두 safe였다. 둘 중 하나를 뒤집은 오류도 각각 검출했다.

확장하려면 function 전체 definition 개수 대신 Boolean 생성 시의 value identity와
생성→사용 구간의 재정의 부재를 증명해야 한다. Loop나 실제 재대입이 있는 경우까지
단순히 제한을 풀면 오래된 Boolean으로 다른 pointer 값을 정제할 위험이 있다.

## 4. 확인된 현재 한계

- Signed `n!=0`의 내부 구멍: DBZ warning을 재현했다. 기존에 수용한 interval
  한계이며 단독으로 구현 누락으로 분류하지 않는다.
- Typed catch/rethrow wrapper: normal은 Complete, exception은 false/Partial이었다.
  이 시험의 native checker 결과는 safe였지만 **wrapper의 exception summary가
  적용되어서 safe였다고 해석해서는 안 된다.** 내부 callee와 기존 native 분석이
  유지한 정보로도 safe가 될 수 있다.
- float/double: summary는 양쪽 Partial, float 나눗셈의 DBZ check는 0개였다.
  `DivisionByZeroChecker`는 정수 SDiv/UDiv/SRem/URem만 검사한다. IEEE 0 나눗셈의
  의미도 정수와 다르므로 이를 곧바로 정수 DBZ false negative라고 부르지 않는다.
  float/double까지 안전성을 주장하려면 별도 명세와 checker 지원이 필요하다.
- Program exit의 조건부 `C_program` 출력/상위 합성은 현재 명세 범위에 없다.
  Local CFG의 존재와 normal/exception에서의 제외를 확인했다.
- 재귀, 일반 dynamic dispatch, 모든 loop, 임의의 alias, 전체 C++ ABI에 대한
  누락 없음은 이 유한 시험으로 증명하지 못했다.

## 다음 우선순위

1. 소멸자의 외부 메모리 효과/termination을 native execution과 summary proof가
   동일하게 처리하도록 보완한다. 실제 오류 누락이 있어 가장 먼저 해결해야 한다.
2. PHI pointer의 Boolean-origin 지원 범위를 확장할지 명세에서 결정한다.
3. Typed catch의 exception coverage, float 계열의 연구 범위를 별도로 결정한다.
