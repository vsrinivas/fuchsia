// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/asm.h>

// Since this is in assembly, its C++ caller cannot inline it and thus cannot
// possibly be a leaf function and thus will certainly have spilled its x30
// onto the shadow call stack so that the value returned here includes its own
// return address as expected.
.function GetShadowCallStackPointer, global
  mov x0, shadow_call_sp
  ret
.end_function
