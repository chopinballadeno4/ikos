; Certify only supported, initialized exception objects and legal rethrows.
target datalayout = "e-m:o-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-apple-macosx10.14.0"

@_ZTIi = external global i8
declare i8* @__cxa_allocate_exception(i64)
declare void @__cxa_free_exception(i8*)
declare void @__cxa_throw(i8*, i8*, i8*) noreturn
declare void @__cxa_rethrow() noreturn
declare void @unknown_destructor(i8*)

define void @scalar_throw(i32 %n) {
entry:
  %bad = icmp sle i32 %n, 0
  br i1 %bad, label %throw, label %normal
throw:
  %object = call i8* @__cxa_allocate_exception(i64 4)
  %p = bitcast i8* %object to i32*
  store i32 1, i32* %p
  call void @__cxa_throw(i8* %object, i8* @_ZTIi, i8* null)
  unreachable
normal:
  ret void
}

define void @unsupported_destructor(i32 %n) {
entry:
  %bad = icmp sle i32 %n, 0
  br i1 %bad, label %throw, label %normal
throw:
  %object = call i8* @__cxa_allocate_exception(i64 4)
  %p = bitcast i8* %object to i32*
  store i32 1, i32* %p
  %destructor = bitcast void (i8*)* @unknown_destructor to i8*
  call void @__cxa_throw(i8* %object, i8* @_ZTIi, i8* %destructor)
  unreachable
normal:
  ret void
}

define void @uninitialized_throw() {
entry:
  %object = call i8* @__cxa_allocate_exception(i64 4)
  call void @__cxa_throw(i8* %object, i8* @_ZTIi, i8* null)
  unreachable
}

define void @freed_throw() {
entry:
  %object = call i8* @__cxa_allocate_exception(i64 4)
  %p = bitcast i8* %object to i32*
  store i32 1, i32* %p
  call void @__cxa_free_exception(i8* %object)
  call void @__cxa_throw(i8* %object, i8* @_ZTIi, i8* null)
  unreachable
}

define void @orphan_rethrow() {
entry:
  call void @__cxa_rethrow()
  unreachable
}

; Check each function independently of call-graph traversal order.
; ORPHAN-LABEL: Function: orphan_rethrow
; ORPHAN: coverage: Partial
; ORPHAN: C_exception (parameter constraints):
; ORPHAN-NEXT: false
; FREED-LABEL: Function: freed_throw
; FREED: coverage: Partial
; FREED: C_exception (parameter constraints):
; FREED-NEXT: false
; UNINITIALIZED-LABEL: Function: uninitialized_throw
; UNINITIALIZED: coverage: Partial
; UNINITIALIZED: C_exception (parameter constraints):
; UNINITIALIZED-NEXT: false
; DESTRUCTOR-LABEL: Function: unsupported_destructor
; DESTRUCTOR: coverage: Partial
; DESTRUCTOR: C_normal (parameter constraints):
; DESTRUCTOR-NEXT: n0 > 0
; DESTRUCTOR: coverage: Partial
; DESTRUCTOR: C_exception (parameter constraints):
; DESTRUCTOR-NEXT: false
; SCALAR-LABEL: Function: scalar_throw
; SCALAR: coverage: Complete
; SCALAR: C_normal (parameter constraints):
; SCALAR-NEXT: n0 > 0
; SCALAR: coverage: Complete
; SCALAR: C_exception (parameter constraints):
; SCALAR-NEXT: n0 <= 0
