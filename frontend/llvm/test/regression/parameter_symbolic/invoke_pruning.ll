; Unknown Invoke paths must not be claimed as guaranteed exception exits.
;
; x == 2 is necessary for unwinding, but may_throw is unknown and can return
; normally instead. An empty Partial guarantee must retain the base flow.

; CHECK-LABEL: Function: exception_only_on_two
; CHECK: C_exception (parameter constraints):
; CHECK-NEXT:     false

; CHECK-LABEL: Function: test_invoke
; CHECK: C_exception (parameter constraints):
; CHECK-NEXT:     false

; ANALYZER: Skipping partial caller summary for function 'exception_only_on_two'
; ANALYZER-LABEL: Function: test_invoke
; ANALYZER: C_exception (parameter constraints):
; ANALYZER-NEXT:     false

declare void @may_throw()
declare i32 @__gxx_personality_v0(...)

define void @exception_only_on_two(i32 %x)
    personality i32 (...)* @__gxx_personality_v0 {
entry:
  %is_two = icmp eq i32 %x, 2
  br i1 %is_two, label %invoke_block, label %normal

invoke_block:
  invoke void @may_throw()
          to label %normal unwind label %exception

normal:
  ret void

exception:
  %lp = landingpad {i8*, i32}
          cleanup
  resume {i8*, i32} %lp
}

define i32 @test_invoke(i32 %x)
    personality i32 (...)* @__gxx_personality_v0 {
entry:
  invoke void @exception_only_on_two(i32 %x)
          to label %normal unwind label %exception

normal:
  ret i32 %x

exception:
  %lp = landingpad {i8*, i32}
          cleanup
  resume {i8*, i32} %lp
}
