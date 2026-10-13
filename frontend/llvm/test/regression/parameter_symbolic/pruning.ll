; Interprocedural memory-summary propagation test.
;
; write_fixed writes 42 to *p; its summary is used when analyzing test_write
; (bottom-up order guaranteed by post-order DFS in analyze_bundle).
; After summary application the load in test_write yields 42 (not TOP).
;
; RUN: ikos-import --format=no --param-symbolic %s | FileCheck %s
;
; Analysis order (post-order DFS from unordered set, may vary by hash):
; write_add -> test_add -> write_fixed -> test_write
; We verify results for each function in that order.

; CHECK-LABEL: Function: write_add
; CHECK: [normal_exit]
; CHECK: mu_normal_exit (caller-visible):
; CHECK-NEXT:     L[p](ext) -> a0 + 1

; CHECK-LABEL: Function: test_add
; CHECK: [normal_exit]
; CHECK: return value = x0 + 1

; CHECK-LABEL: Function: write_fixed
; CHECK: [normal_exit]
; CHECK: mu_normal_exit (caller-visible):
; CHECK-NEXT:     L[p](ext) -> 42

; CHECK-LABEL: Function: test_write
; CHECK: [normal_exit]
; CHECK: return value = 42

define void @write_fixed(i32* %p) {
entry:
  store i32 42, i32* %p
  ret void
}

define i32 @test_write(i32* %p) {
entry:
  call void @write_fixed(i32* %p)
  %v = load i32, i32* %p
  ret i32 %v
}

define void @write_add(i32* %p, i32 %a) {
entry:
  %sum = add i32 %a, 1
  store i32 %sum, i32* %p
  ret void
}

define i32 @test_add(i32* %q, i32 %x) {
entry:
  call void @write_add(i32* %q, i32 %x)
  %v = load i32, i32* %q
  ret i32 %v
}
