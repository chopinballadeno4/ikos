# Benchmark 01~50 실행 결과 — 2026-11-01

`run_modified.sh`와 `run_original.sh`를 모두 01~50 범위로 바꾸고 실제 실행했다. 각 파일의 `analysis:` 선언을 우선 사용하며, 선언이 없는 01~10은 nullity, 06은 boa로 선택한다. 18/20의 `dbz,boa`, 36/49의 `nullity`도 포함한다. 08/09는 10진수로 처리한다.

| 분석기 | safe | definite unsafe | warnings | 실행 실패 |
|---|---:|---:|---:|---:|
| 수정판 | 141 | 0 | 17 | 0 |
| 기존판 | 100 | 0 | 58 | 0 |

양쪽 모두 50개 파일을 실행했고 각 예제에서 검사 결과가 생성됐다. 선택한 checker의 총 검사 수는 양쪽 모두 158개로, warning 41개가 safe로 바뀌었다. 수정판에서는 35개 파일이 warning 없이 끝났다. 이 수치가 전체 C++ UB, 모든 checker 또는 summary의 soundness 전체를 인증하는 것은 아니다.

결과 디렉터리:

- 수정판: `/private/tmp/ikos-benchmark-1-50-modified/`
- 기존판: `/private/tmp/ikos-benchmark-1-50-original/`
- UBSan 반례: `/private/tmp/ikos-benchmark-1-50-witnesses/`

디렉터리마다 summary.tsv와 파일별 log/DB를 저장한다. 재실행 시 `IKOS_BENCHMARK_OUTPUT`으로 새 결과 디렉터리를 지정할 수 있다. 지정하지 않으면 새 임시 디렉터리를 만든다. 기존 output.db를 덮어쓰지 않는다.

## 1. 17개 경고의 분류

| 구분 | 번호 | 경고 수 |
|---|---|---:|
| 실제 오류가 가능한 코드 | 06, 10, 12, 16, 20, 26, 33, 35, 39, 46 | 11 |
| signed NE의 허용한 interval 정밀도 한계 | 11, 13, 19, 27 | 5 |
| unknown 호출로 충분조건/Complete를 얻지 못하는 오탐 | 05 | 1 |

### Safe 기대 주석과 달리 실제 오류가 있는 예제

| 번호 | 구체적인 입력 | guard 이후 실제 동작 | UBSan 확인 |
|---|---|---|---|
| 06 | n=101 | n>0만 검사; arr[n-1]=arr[100], 배열 크기는 100 | index 100 out of bounds |
| 26 | x=0, y=SIZE_MAX | e0=1, e1=0; guard는 r=0만 거부하므로 통과; arr[0][SIZE_MAX] | index 18446744073709551615 out of bounds |
| 33 | n=2 | n>1이면 throw; catch의 100/(n-2)는 100/0 | division by zero |
| 46 | step=1, bidx=31 | step*bidx=31<32로 통과하지만 실제 index는 step*(bidx+1)=32 | index 32 out of bounds |

이 네 경우를 분석기 오탐으로 고쳐서는 안 된다. 06에는 upper bound가 없고, 26에는 c>=1 조건이 없고, 33의 분모는 현재 n-2이며, 46의 검사식은 실제 접근식과 다르다. 46은 추가로 guard의 32비트 wrapping 곱과 접근의 size_t 곱이 달라 큰 입력도 문제가 된다.

### Safe가 맞는데 warning 기대 주석이 남은 예제

- **38번**: 실제 조건은 `if (n > 0) throw 1;`이다. C_normal은 `n<=0`, C_exception은 `n>0`이며 catch의 `100/n`은 안전하다. 현재 safe는 오류 누락이 아니다. 원래 의도한 “0이 catch로 들어오는 오류”를 테스트하려면 코드가 바뀌어야 한다.
- **43번**: outer_guard의 normal 조건이 `0<n<=200`으로 Complete 생성·적용된다. 과거 complex 식 때문에 경고한다는 주석이 오래됐다.
- **44번**: cell_guard의 normal 조건이 `r<6 && c<8`로 Complete다. 산술식 단순화와 조건 적용이 동작한다.
- **47번**: param_modify_guard의 normal 조건이 `0<n<=1000`으로 Complete다. parameter 재대입과 local-variable 치환이 해결된 결과다.

### 남은 오탐

- **05번**: guard_check의 C_normal/C_exception은 모두 false/Partial이다. loop 안의 `do_work(i)`가 unknown call이라 정상/예외/비종료 효과를 인증하지 못한다. 현재 충분조건/coverage 계약에서는 Partial을 전역 pruning에 쓸 수 없어 pointer 경고가 남는다. 단순히 loop가 있다는 이유만은 아니다. 순수한 고정 반복문을 가진 25번은 Complete로 처리돼 safe다. 정상 복귀에 필요한 조건만 별도로 추출하는 확장은 검토할 수 있지만 이번에는 명세/구현을 변경하지 않았다.
- **11·13·19번**: summary는 `n!=0` 또는 `x!=0 && y!=0`을 Complete로 정확히 보존한다. signed interval 중간의 0을 제외할 수 없어 오탐이 남는다.
- **27번**: summary는 `(n<0)||(n>0)`으로 Complete다. 두 interval을 join하면 0도 포함한다. 기존에 수용한 NE/disjunction 정밀도 한계다.

### 실제 경고를 유지한 음성 대조군

10번의 test_unsafe, 12/16/20번의 guard 없는 접근, 35번의 내부 catch 후 복귀, 39번의 unknown exception origin은 실제 오류가 가능하며 경고를 유지했다. 특히 39번에 explicit throw 조건만 적용하여 n=0의 예외 경로를 삭제하는 일은 이번 결과에서 발생하지 않았다.

## 2. checker 결과와는 별도로 잘못된 주석

- 21번: sum의 전체 정상 범위는 단순 [1,500]이 아니다. 짝수는 1000까지 통과한다. 0은 제외돼 DBZ safe는 맞다.
- 24번: callee는 홀수도 100 이하이면 허용한다. C_N을 무조건 n%2==0이라고 쓴 주석은 틀리다. 이 caller의 d=2*k는 정의된 실행에서 짝수다.
- 26번: 현재 guard는 원점만 거부하는 것이 아니라 r==0 전체를 거부한다. c==0은 허용하며 두 index가 모두 1 이상이라는 주석이 틀리다.
- 29번: 실제 upper guard는 idx<10이며 배열도 10개다. idx<8/[2,7]이라는 주석이 오래됐다. 접근 자체는 safe다.
- 30번: 실제 idx upper bound는 4다. step=3,idx=4의 곱 12를 마지막 prod<=9 검사가 거부한다. 마지막 검사가 항상 참이라는 주석은 틀리다. 현재 warning 0은 곱셈 결과의 범위 재사용도 동작함을 보여준다.
- 31/34/35/36/38/39번의 비어 있거나 뒤집힌 C_normal/C_exception 주석은 생성된 summary와 코드의 의미를 대신하지 못한다. 특히 33·38·39는 실제 predicate를 기준으로 판단해야 한다.
- 41번은 실제 v<=0에서 abort하며, 상단의 “v<0에서 throw” 설명은 현재 코드와 다르다.

이번 작업에서는 벤치마크 동작이나 기대 주석을 자동으로 바꾸지 않았다. 사용자가 변경한 조건을 보존하고 차이를 이 문서에 기록했다.

## 3. 50개 전체 결과

| 번호 | checker | 수정판 safe / warning | 기존판 safe / warning | 판정 |
|---|---|---:|---:|---|
| [01](01_guard_basic.cpp) | nullity | 5 / 0 | 4 / 1 | 선택한 checker에서 기대한 safe |
| [02](02_guard_two_params.cpp) | nullity | 8 / 0 | 6 / 2 | 선택한 checker에서 기대한 safe |
| [03](03_guard_log_abort.cpp) | nullity | 6 / 0 | 5 / 1 | 선택한 checker에서 기대한 safe |
| [04](04_guard_nested_if.cpp) | nullity | 5 / 0 | 4 / 1 | 선택한 checker에서 기대한 safe |
| [05](05_guard_loop_callee.cpp) | nullity | 5 / 1 | 5 / 1 | 안전 코드지만 unknown do_work 호출로 normal summary Partial; 오탐 |
| [06](06_guard_integer.cpp) | boa | 0 / 1 | 0 / 1 | 실제 OOB; safe 기대 주석이 틀림 |
| [07](07_guard_null_form.cpp) | nullity | 5 / 0 | 4 / 1 | 선택한 checker에서 기대한 safe |
| [08](08_guard_multiple_abort.cpp) | nullity | 7 / 0 | 6 / 1 | 선택한 checker에서 기대한 safe |
| [09](09_guard_wrapper.cpp) | nullity | 6 / 0 | 5 / 1 | 선택한 checker에서 기대한 safe |
| [10](10_guard_no_guard_mix.cpp) | nullity | 7 / 1 | 6 / 2 | guard 없는 test_unsafe의 의도한 경고 |
| [11](11_dbz_basic.cpp) | dbz | 0 / 1 | 0 / 1 | signed NE 내부 구멍: 허용한 interval 오탐 |
| [12](12_dbz_no_guard.cpp) | dbz | 0 / 1 | 0 / 1 | 0 분모 가능: 의도한 경고 |
| [13](13_dbz_two_params.cpp) | dbz | 0 / 2 | 0 / 2 | 두 분모의 signed NE: 허용한 interval 오탐 2개 |
| [14](14_dbz_positive_guard.cpp) | dbz | 1 / 0 | 0 / 1 | 선택한 checker에서 기대한 safe |
| [15](15_boa_basic.cpp) | boa | 2 / 0 | 1 / 1 | 선택한 checker에서 기대한 safe |
| [16](16_boa_no_guard.cpp) | boa | 1 / 1 | 1 / 1 | 범위 밖 index 가능: 의도한 경고 |
| [17](17_boa_size_guard.cpp) | boa | 2 / 0 | 1 / 1 | 선택한 checker에서 기대한 safe |
| [18](18_mixed_dbz_boa.cpp) | dbz,boa | 3 / 0 | 1 / 2 | 선택한 checker에서 기대한 safe |
| [19](19_dbz_log_abort.cpp) | dbz | 0 / 1 | 0 / 1 | summary Complete지만 signed NE 정밀도 한계 |
| [20](20_mixed_no_guard.cpp) | dbz,boa | 1 / 2 | 1 / 2 | 배열 범위/0 나눗셈 모두 실제 가능 |
| [21](21_dbz_named_sum.cpp) | dbz | 2 / 0 | 1 / 1 | 선택한 checker에서 기대한 safe |
| [22](22_dbz_inline_sum.cpp) | dbz | 1 / 0 | 0 / 1 | 선택한 checker에서 기대한 safe |
| [23](23_dbz_named_offset.cpp) | dbz | 2 / 0 | 1 / 1 | 선택한 checker에서 기대한 safe |
| [24](24_dbz_scaled_named.cpp) | dbz | 2 / 0 | 1 / 1 | 선택한 checker에서 기대한 safe |
| [25](25_dbz_named_diff.cpp) | dbz | 1 / 0 | 0 / 1 | 선택한 checker에서 기대한 safe |
| [26](26_boa_two_expr_args.cpp) | boa | 1 / 1 | 1 / 1 | c=0 허용, c-1 underflow: 실제 OOB |
| [27](27_dbz_ne_named_diff.cpp) | dbz | 0 / 1 | 0 / 1 | 양·음수 구간 join의 0 포함: 의도한 정밀도 경고 |
| [28](28_dbz_wrong_base.cpp) | dbz | 1 / 0 | 0 / 1 | 선택한 checker에서 기대한 safe |
| [29](29_boa_named_index_expr.cpp) | boa | 2 / 0 | 1 / 1 | 선택한 checker에서 기대한 safe |
| [30](30_boa_stride_expr.cpp) | boa | 2 / 0 | 1 / 1 | 선택한 checker에서 기대한 safe |
| [31](31_exception_normal_positive.cpp) | dbz | 1 / 0 | 0 / 1 | 선택한 checker에서 기대한 safe |
| [32](32_exception_normal_bounds.cpp) | boa | 3 / 0 | 2 / 1 | 선택한 checker에서 기대한 safe |
| [33](33_exception_catch_positive.cpp) | dbz | 0 / 1 | 0 / 1 | n=2에서 catch의 n-2=0: 실제 오류 |
| [34](34_exception_catch_bounds.cpp) | boa | 3 / 0 | 2 / 1 | 선택한 checker에서 기대한 safe |
| [35](35_exception_swallowed_guard.cpp) | dbz | 0 / 1 | 0 / 1 | n=0의 예외를 삼키고 복귀: 실제 오류 |
| [36](36_exception_rethrow_pointer.cpp) | nullity | 13 / 0 | 12 / 1 | 선택한 checker에서 기대한 safe |
| [37](37_exception_memory_effect.cpp) | dbz | 2 / 0 | 2 / 0 | 선택한 checker에서 기대한 safe |
| [38](38_exception_catch_zero_possible.cpp) | dbz | 1 / 0 | 0 / 1 | 현재 throw 조건은 n>0: catch 나눗셈 safe가 맞음 |
| [39](39_exception_unknown_call.cpp) | dbz | 0 / 1 | 0 / 1 | may_throw가 n=0에서도 throw 가능: 실제 오류 |
| [40](40_exception_abort_separation.cpp) | dbz | 2 / 0 | 0 / 2 | 선택한 checker에서 기대한 safe |
| [41](41_mixed_dbz_ladder_guard.cpp) | dbz | 1 / 0 | 0 / 1 | 선택한 checker에서 기대한 safe |
| [42](42_mixed_boa_index_chain.cpp) | boa | 4 / 0 | 3 / 1 | 선택한 checker에서 기대한 safe |
| [43](43_mixed_dbz_three_level.cpp) | dbz | 1 / 0 | 0 / 1 | 산술 단순화/합성이 해결됨; warning 기대 주석이 오래됨 |
| [44](44_mixed_boa_two_sequential_guards.cpp) | boa | 3 / 0 | 2 / 1 | modular 식 단순화 해결; warning 기대 주석이 오래됨 |
| [45](45_mixed_dbz_catch_rethrow.cpp) | dbz | 1 / 0 | 0 / 1 | 선택한 checker에서 기대한 safe |
| [46](46_mixed_boa_stride_expr_arg.cpp) | boa | 3 / 1 | 3 / 1 | 검사식과 실제 index가 다름: 실제 OOB |
| [47](47_mixed_dbz_param_modified.cpp) | dbz | 1 / 0 | 0 / 1 | parameter 재대입 후 치환 해결; warning 기대 주석이 오래됨 |
| [48](48_mixed_boa_region_span.cpp) | boa | 5 / 0 | 3 / 2 | normal/exception의 서로 다른 index 범위 모두 safe |
| [49](49_mixed_nullity_buffer_guard.cpp) | nullity | 14 / 0 | 12 / 2 | normal/exception의 서로 다른 pointer nullity 모두 safe |
| [50](50_mixed_boa_triple_guard.cpp) | boa | 5 / 0 | 3 / 2 | normal/exception의 서로 다른 3D 좌표 범위 모두 safe |

## 4. 해석 한도

이번 01~50의 선택된 checker 결과에서는 새로 확인된 실제 오류 누락은 없었다. 38번의 safe도 코드 의미와 일치한다. 하지만 이 50개에 exception destructor의 외부 메모리 수정/termination 반례가 포함돼 있지는 않다. 앞서 research_audit에서 확인한 native exec_end_catch 누락은 여전히 별개로 남아 있으며, 이번 실행이 그 문제를 해결하거나 부정하지 않는다. 자세한 내용은 [연구 감사 보고서](research_audit/FINDINGS.md)를 참고한다.

우선순위는 (1) 기존 감사에서 확인한 destructor soundness 문제, (2) safe/warning 기대가 코드와 다른 06/26/33/38/43/44/46/47 주석·테스트 의도 정리, (3) 05의 필요조건 활용 및 NE 정밀도 확장 여부 결정이다.
