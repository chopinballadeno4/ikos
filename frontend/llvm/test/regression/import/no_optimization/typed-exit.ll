target datalayout = "e-m:o-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-apple-macosx10.14.0"

; CHECK-LABEL: Bundle
; CHECK: target-endianness = little-endian
; CHECK: target-pointer-size = 64 bits
; CHECK: target-triple = x86_64-apple-macosx10.14.0

declare void @may_throw()

declare i32 @__gxx_personality_v0(...)
; CHECK: declare si32 @__gxx_personality_v0(...)
; CHECK: declare void @may_throw()

define void @normal_or_exception() personality i8* bitcast (i32 (...)* @__gxx_personality_v0 to i8*) {
entry:
  invoke void @may_throw()
          to label %normal unwind label %exception

normal:
  ret void

exception:
  %lp = landingpad { i8*, i32 }
          cleanup
  resume { i8*, i32 } %lp
}
; CHECK: define void @normal_or_exception() {
; CHECK: #entry !entry successors={#1, #2} {
; CHECK:   invoke @may_throw() normal=#1 exc=#2
; CHECK: }
; CHECK: #1 !normal-exit predecessors={#entry} successors={#unified-exit} {
; CHECK:   return
; CHECK: }
; CHECK: #2 !exception-exit predecessors={#entry} successors={#unified-exit} {
; CHECK:   {0: si8*, 8: si32} %lp = landingpad
; CHECK:   resume %lp
; CHECK: }
; CHECK: #unified-exit !exit predecessors={#1, #2} {
; CHECK: }
; CHECK: }

define void @program_only() {
entry:
  unreachable
}
; CHECK: define void @program_only() {
; CHECK: #entry !entry !exit !program-exit {
; CHECK:   unreachable
; CHECK: }
; CHECK: }

define void @two_exception_paths(i1 %condition) personality i8* bitcast (i32 (...)* @__gxx_personality_v0 to i8*) {
entry:
  br i1 %condition, label %call1, label %call2

call1:
  invoke void @may_throw()
          to label %normal unwind label %exception1

call2:
  invoke void @may_throw()
          to label %normal unwind label %exception2

normal:
  ret void

exception1:
  %lp1 = landingpad { i8*, i32 }
          cleanup
  resume { i8*, i32 } %lp1

exception2:
  %lp2 = landingpad { i8*, i32 }
          cleanup
  resume { i8*, i32 } %lp2
}
; CHECK: define void @two_exception_paths(si1 %condition) {
; CHECK: #entry !entry successors={#1, #2} {
; CHECK: }
; CHECK: #1 predecessors={#entry} successors={#3, #4} {
; CHECK:   %condition sieq -1
; CHECK:   invoke @may_throw() normal=#3 exc=#4
; CHECK: }
; CHECK: #2 predecessors={#entry} successors={#5, #6} {
; CHECK:   %condition sieq 0
; CHECK:   invoke @may_throw() normal=#5 exc=#6
; CHECK: }
; CHECK: #3 predecessors={#1} successors={#normal} {
; CHECK: }
; CHECK: #4 predecessors={#1} successors={#exception-exit} {
; CHECK:   {0: si8*, 8: si32} %lp1 = landingpad
; CHECK:   resume %lp1
; CHECK: }
; CHECK: #5 predecessors={#2} successors={#normal} {
; CHECK: }
; CHECK: #6 predecessors={#2} successors={#exception-exit} {
; CHECK:   {0: si8*, 8: si32} %lp2 = landingpad
; CHECK:   resume %lp2
; CHECK: }
; CHECK: #normal !normal-exit predecessors={#3, #5} successors={#unified-exit} {
; CHECK:   return
; CHECK: }
; CHECK: #exception-exit !exception-exit predecessors={#4, #6} successors={#unified-exit} {
; CHECK: }
; CHECK: #unified-exit !exit predecessors={#normal, #exception-exit} {
; CHECK: }
; CHECK: }
