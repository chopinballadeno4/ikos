; Interprocedural memory-summary propagation test.
;
; write_fixed writes 42 to *p; its summary is used when analyzing test_write
; (bottom-up order guaranteed by post-order DFS in analyze_bundle).
; After summary application the load in test_write yields 42 (not TOP).
;
; RUN: ikos-import --format=no --param-symbolic %s | FileCheck %s
;
; Analysis uses a bottom-up post-order so each caller can consume its callee's
; summary. The checks below also verify condition and memory composition.

; CHECK-LABEL: Function: require_positive
; CHECK: C_normal (parameter constraints):
; CHECK-NEXT:     x0 > 0

; CHECK-LABEL: Function: test_condition
; CHECK: C_normal (parameter constraints):
; CHECK-NEXT:     x0 > 0

; ANALYZER: Applying caller summary for function 'require_positive'
; ANALYZER-LABEL: Function: test_condition
; ANALYZER: C_normal (parameter constraints):
; ANALYZER-NEXT:     x0 > 0

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

declare void @abort() noreturn

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
  %sum = add nsw i32 %a, 1
  store i32 %sum, i32* %p
  ret void
}

define i32 @test_add(i32* %q, i32 %x) {
entry:
  call void @write_add(i32* %q, i32 %x)
  %v = load i32, i32* %q
  ret i32 %v
}

define void @require_positive(i32 %x) {
entry:
  %positive = icmp sgt i32 %x, 0
  br i1 %positive, label %normal, label %terminate

normal:
  ret void

terminate:
  call void @abort()
  unreachable
}

define i32 @test_condition(i32 %x) {
entry:
  call void @require_positive(i32 %x)
  ret i32 %x
}
