; A low-bit test does not imply that the original input equals one.
; In particular, input 3 must remain a possible normal-return input.
; RUN: ikos-import --format=no --param-symbolic %s | FileCheck %s

declare void @abort() noreturn

define void @require_one(i32 %condition) {
entry:
  %valid = icmp eq i32 %condition, 1
  br i1 %valid, label %normal, label %terminate
normal:
  ret void
terminate:
  call void @abort()
  unreachable
}

define void @low_bit_guard(i32 %input) {
entry:
  %bit = trunc i32 %input to i1
  %encoded = zext i1 %bit to i32
  call void @require_one(i32 %encoded)
  ret void
}

; CHECK-LABEL: Function: low_bit_guard
; CHECK: C_normal (parameter constraints):
; CHECK-NEXT: true
