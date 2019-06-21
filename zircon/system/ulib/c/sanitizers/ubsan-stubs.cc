// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// In the UBSan build, this file provides weak definitions for all the
// same entry points that are defined by the UBSan runtime library.  The
// definitions here are stubs that are used only during the dynamic
// linker's startup phase before the UBSan runtime shared library has
// been loaded.  These are required to satisfy the references in libc's
// own code.
//
// LLVM provides no documentation on the ABI between the compiler and
// the runtime.  The set of function signatures here was culled from the
// LLVM sources for the runtime (see compiler-rt/lib/ubsan/*).

#include <zircon/compiler.h>

#if __has_feature(undefined_behavior_sanitizer)

#define STUB_HANDLER(name, ...)                                        \
    [[gnu::weak]] extern "C" void __ubsan_handle_##name(__VA_ARGS__) { \
        __builtin_trap();                                              \
    }

#define UNRECOVERABLE(name, ...) \
    STUB_HANDLER(name)
#define RECOVERABLE(name, ...) \
    STUB_HANDLER(name)         \
    STUB_HANDLER(name##_abort)

// Culled from compiler-rt/lib/ubsan/ubsan_handlers.h:

#include <cstdint>
using uptr = uintptr_t;

struct AlignmentAssumptionData;
struct CFICheckFailData;
struct ImplicitConversionData;
struct InvalidValueData;
struct NonNullArgData;
struct OutOfBoundsData;
struct OverflowData;
struct PointerOverflowData;
struct ShiftOutOfBoundsData;
struct SourceLocation;
struct TypeMismatchData;
struct UnreachableData;
using ValueHandle = uptr;

RECOVERABLE(type_mismatch_v1, TypeMismatchData* Data, ValueHandle Pointer)
RECOVERABLE(alignment_assumption, AlignmentAssumptionData* Data,
            ValueHandle Pointer, ValueHandle Alignment, ValueHandle Offset)
RECOVERABLE(add_overflow, OverflowData* Data, ValueHandle LHS, ValueHandle RHS)
RECOVERABLE(sub_overflow, OverflowData* Data, ValueHandle LHS, ValueHandle RHS)
RECOVERABLE(mul_overflow, OverflowData* Data, ValueHandle LHS, ValueHandle RHS)
RECOVERABLE(negate_overflow, OverflowData* Data, ValueHandle OldVal)
RECOVERABLE(divrem_overflow, OverflowData* Data,
            ValueHandle LHS, ValueHandle RHS)
RECOVERABLE(shift_out_of_bounds, ShiftOutOfBoundsData* Data,
            ValueHandle LHS, ValueHandle RHS)
RECOVERABLE(out_of_bounds, OutOfBoundsData* Data, ValueHandle Index)
UNRECOVERABLE(builtin_unreachable, UnreachableData* Data)
UNRECOVERABLE(missing_return, UnreachableData* Data)
RECOVERABLE(vla_bound_not_positive, VLABoundData* Data, ValueHandle Bound)
RECOVERABLE(float_cast_overflow, void* Data, ValueHandle From)
RECOVERABLE(load_invalid_value, InvalidValueData* Data, ValueHandle Val)
RECOVERABLE(implicit_conversion, ImplicitConversionData* Data,
            ValueHandle Src, ValueHandle Dst)
RECOVERABLE(invalid_builtin, InvalidBuiltinData* Data)
RECOVERABLE(function_type_mismatch, ValueHandle Function)
RECOVERABLE(nonnull_return_v1, NonNullReturnData* Data, SourceLocation* Loc)
RECOVERABLE(nullability_return_v1, NonNullReturnData* Data, SourceLocation* Loc)
RECOVERABLE(nonnull_arg, NonNullArgData* Data)
RECOVERABLE(nullability_arg, NonNullArgData* Data)
RECOVERABLE(pointer_overflow, PointerOverflowData* Data, ValueHandle Base,
            ValueHandle Result)
RECOVERABLE(cfi_check_fail, CFICheckFailData* Data, ValueHandle Function,
            uptr VtableIsValid)
UNRECOVERABLE(cfi_bad_type, CFICheckFailData* Data, ValueHandle Vtable,
              bool ValidVtable, ReportOptions Opts)

#endif // __has_feature(undefined_behavior_sanitizer)
