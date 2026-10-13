target datalayout = "e-m:o-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-apple-macosx10.14.0"

declare void @may_throw()
declare i32 @__gxx_personality_v0(...)

define i32 @summary_paths(i32 %x) personality i32 (...)* @__gxx_personality_v0 {
entry:
  %is_two = icmp eq i32 %x, 2
  br i1 %is_two, label %two, label %not_two

two:
  invoke void @may_throw()
          to label %normal_two unwind label %exception_two

not_two:
  %is_three = icmp eq i32 %x, 3
  br i1 %is_three, label %three, label %normal_other

three:
  invoke void @may_throw()
          to label %normal_three unwind label %exception_three

normal_two:
  br label %normal_exit

normal_three:
  br label %normal_exit

normal_other:
  br label %normal_exit

normal_exit:
  %result = phi i32 [ 2, %normal_two ],
                    [ 3, %normal_three ],
                    [ 0, %normal_other ]
  ret i32 %result

exception_two:
  %lp_two = landingpad {i8*, i32}
              cleanup
  resume {i8*, i32} %lp_two

exception_three:
  %lp_three = landingpad {i8*, i32}
                cleanup
  resume {i8*, i32} %lp_three
}

; CHECK-LABEL: Function: summary_paths
; CHECK: C_normal (parameter constraints):
; CHECK-NEXT: (x0 == 2) ||
; CHECK-NEXT: (x0 != 2 && x0 != 3) ||
; CHECK-NEXT: (x0 != 2 && x0 == 3)
; CHECK: C_exception (parameter constraints):
; CHECK-NEXT: (x0 == 2) ||
; CHECK-NEXT: (x0 != 2 && x0 == 3)
