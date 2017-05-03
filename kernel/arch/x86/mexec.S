// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/defines.h>
#include <asm.h>

.section .text
FUNCTION(mexec_asm)
	// Unimplemented for now, emit an undefined instruction.
	UD2
DATA(mexec_asm_end)