# fmt 감소한 3개 warning: 호출 경로와 실제 원인

2026-11-02 후속 확인. 최초 비교의 `fmt/api-017`, 즉 `fmt::detail::print(FILE*, string_view)` entry를 기준으로 확인했다. 이 실행의 세 감소 항목은 다음과 같다.

| 검사 위치 | 검사 종류 | original → dev |
|---|---|---|
| core.h:2573:23 `index * 4` | signed overflow | warning → safe |
| 같은 연산 | signed underflow | warning → safe |
| core.h:2575:45 `desc_ >> shift` | shift count | warning → safe |

**이 감소는 종료 summary의 직접 효과로 분류하면 안 된다.** 조건식의 unsigned cast를 원래 signed 변수로 역전파하는 expression-range 개선으로 같은 차이를 함수 내부 분석만으로도 재현했다. `get(id)`의 범위 밖 분기는 abort/throw가 아니라 정상 return이다. 함수 전체의 C_normal을 `0<=id<15`로 제한하는 상황이 아니다.

## 1. 실제 호출 경로

원본 DB의 call_contexts를 부모 방향으로 추적했다. 경고였던 두 문맥 중 context 136은 다음과 같다. 각 위치는 caller가 다음 함수를 부르는 지점이다.

```text
detail::print                                      format-inl.h:1760
  → detail::fwrite_all                             format-inl.h:127
  → system_error<>                                 format.h:4593
  → vsystem_error                                  format-inl.h:187
  → vformat                                        format-inl.h:1462
  → detail::vformat_to                             format-inl.h:1473
  → parse_format_string                            core.h:1651
  → parse_replacement_field                        core.h:1636
  → format_handler::on_format_specs                format.h:4068
  → context::arg                                   core.h:2665
  → basic_format_args::get                         core.h:2616
  → basic_format_args::type                        core.h:2573 / 2575
```

다른 경고 문맥151은 `parse_replacement_field`의 core.h:1626에서 `on_replacement_field`를 부르고, format.h:4063에서 `context::arg`로 들어온다. 나머지 상위 경로는 같다. context117은 core.h:1608의 자동 argument-id 경로이며 원래도 safe였다.

`fwrite_all`은 파일 쓰기 실패 시 `system_error(...,"cannot write to file")`를 만들고, 이 메시지를 fmt formatter로 처리한다. 실제 문자열은 placeholder 없는 상수다. 그럼에도 분석의 일반화된 parser 경로에 검사 기록이 생겼다. 실제 실행에서 이 메시지가 특정 큰 인덱스를 가진다는 뜻은 아니다.

## 2. 첫 번째 warning: signed overflow

[get(id): core.h:2609](/Users/baesungmin/master/experiment/realworld/fmt/include/fmt/core.h:2609)의 packed 분기는 다음과 같다.

```cpp
if (unsigned(id) >= detail::max_packed_args) return arg;
arg.type_ = type(id);
```

[상수 정의: core.h:2293](/Users/baesungmin/master/experiment/realworld/fmt/include/fmt/core.h:2293)는 `packed_arg_bits=4`, `max_packed_args=62/4=15`다. 따라서 type에 도달하는 branch는 `unsigned(id)<15`. 32-bit int/unsigned에서 음수 id는 큰 unsigned 값이 되므로 이 branch의 실제 signed id는 `[0,14]`다.

[type(index): core.h:2572](/Users/baesungmin/master/experiment/realworld/fmt/include/fmt/core.h:2572)는 다음을 계산한다.

```cpp
int shift = index * detail::packed_arg_bits;  // index * 4
```

최초 all-checker DB에서 context136/151의 original은 index를 signed int 전체 범위 `[-2147483648,2147483647]`로 보았다. 곱셈의 수학적 범위는 `[-8589934592,8589934588]`이라 overflow/underflow warning이었다. dev에서는 해당 검사들이 safe로 바뀌었다.

같은 bitcode/entry/interval/inter 설정에서 sio,shc checker를 켜고 `--display-inv=all --display-checks=all`로 재확인했다. 곱셈 직전 dev invariant는 다음과 같다.

| 문맥 | original index | dev index |
|---|---|---|
| 117: 자동 id 경로 | `[0,0]` | `[0,0]` |
| 136: format-spec 경로 | signed int 전체 | `[0,14]` |
| 151: replacement 경로 | signed int 전체 | `[0,14]` |

따라서 dev의 `index*4`는 `[0,56]`이고 signed overflow가 없다. 같은 이유로 두 번째 underflow도 없으며, 세 번째 shift count도 64-bit 값에 대한 `[0,56]`이라 안전하다. 원본 DB에서도 해당 검사 지점은 dev에서 unreachable이 아니라 safe다.

## 3. 원래 signed 변수로 조건이 전달되는 지점

수정판의 [PolymorphicDomain::int_apply:1820](/Users/baesungmin/master/experiment/ikos/core/include/ikos/core/domain/memory/polymorphic_domain.hpp:1820)는 cast의 expression key를 보존한다. 비교 조건을 반영하는 [int_add:1859](/Users/baesungmin/master/experiment/ikos/core/include/ikos/core/domain/memory/polymorphic_domain.hpp:1859)는 capture_range를 호출한다.

[reduce_expression:1512](/Users/baesungmin/master/experiment/ikos/core/include/ikos/core/domain/memory/polymorphic_domain.hpp:1512)는 같은 bit width의 cast 또는 확장을 역으로 정제한다. 이 사례에서는 unsigned 임시값 `[0,14]`를 signed id `[0,14]`로 반영한다. 잘리는 truncation을 무조건 역산하는 처리는 아니다.

그 뒤 type의 actual id를 formal index로 전달하므로 index도 `[0,14]`다. 이 세 검사는 callee 종료 이후 caller의 C_normal을 적용해서 해결되는 구조가 아니다.

## 4. 함수 간 summary를 배제한 대조 실행

별도의 진단 예제에 실제 코드의 cast/비교/곱셈/shift만 남겼다.

```cpp
#include <ikos/analyzer/intrinsic.h>
extern "C" unsigned long probe() {
  int id = __ikos_nondet_int();
  if (static_cast<unsigned>(id) >= 15) return 0;
  int shift = id * 4;
  return static_cast<unsigned long>(__ikos_nondet_uint()) >> shift;
}
```

Clang14/O0로 컴파일한 같은 bitcode를 `--proc=intra --domain=interval -a sio,shc`로 분석했다. 일반 사용자 helper 호출은 없고 nondet intrinsic만 있다.

| raw 검사 | original | dev |
|---|---|---|
| signed overflow | warning | safe |
| signed underflow | warning | safe |
| shift count | warning | safe |

raw DB에는 양쪽 모두 같은 세 kind가 있다. 요약 출력은 original warning3 / dev warning0이며 dev safe2다. 이는 두 safe overflow/underflow가 집계에서 같은 statement의 safe로 합쳐지기 때문이며 검사 삭제가 아니다.

원본 3건 감소는 유효하지만 연구의 exit-summary 효과와 expression-range/cast 역전파 효과를 나누어 보고해야 한다.

## 근거

- `fmt-warning-review.json`: 최초 all-checker DB의 해당 raw 검사, 문맥별 전체 호출 경로, focused 재실행 명령과 변수 범위, intra 대조 실행 명령과 raw check kind/status.
- 최초 DB: `/private/tmp/ikos-realworld-20261101-pou0ms1d/fmt/api-017/{original,dev}.db`.
- invariant 및 대조 실행: `/private/tmp/ikos-fmt-first-warning-7h56ezrl/`. 전체 invariant log는 gzip으로 보존했다. 임시 경로는 정리될 수 있으며 작은 근거 JSON은 이 보고서와 함께 보존한다.
- 분석기와 fmt 소스는 변경하지 않았다.
