target datalayout = "e-m:o-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-apple-macosx10.14.0"

define void @offset_locations(i32* %p) {
entry:
  %p0 = getelementptr i32, i32* %p, i64 0
  %p1 = getelementptr i32, i32* %p, i64 1
  store i32 10, i32* %p0
  store i32 20, i32* %p1
  ret void
}

define void @symbolic_offset(i32* %p, i64 %index) {
entry:
  %element = getelementptr i32, i32* %p, i64 %index
  store i32 30, i32* %element
  ret void
}

define i32 @merge_missing(i32* %p, i1 %condition) {
entry:
  br i1 %condition, label %write, label %skip

write:
  store i32 10, i32* %p
  br label %join

skip:
  br label %join

join:
  %value = load i32, i32* %p
  ret i32 %value
}

; CHECK-LABEL: Function: merge_missing
; CHECK: mu_normal_exit (caller-visible):
; CHECK-NEXT: L[p](ext) -> TOP
; CHECK: coverage: Partial
; CHECK: C_normal (parameter constraints):
; CHECK-NEXT: false
;
; CHECK-LABEL: Function: symbolic_offset
; CHECK: mu_normal_exit (caller-visible):
; CHECK-NEXT: Loc(L[p], 4*index0)(ext) -> 30
;
; CHECK-LABEL: Function: offset_locations
; CHECK: mu_normal_exit (caller-visible):
; CHECK-NEXT: L[p](ext) -> 10
; CHECK-NEXT: Loc(L[p], 4)(ext) -> 20
