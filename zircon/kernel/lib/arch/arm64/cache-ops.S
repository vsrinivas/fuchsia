// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/asm.h>
#include <lib/arch/cache.h>

.text

// void Arm64LocalInvalidateAllCaches()
.function Arm64LocalInvalidateAllCaches, global, none
    cache_way_set_op isw, invalidate

    // dump the instruction cache as well
    ic      iallu
    isb

    ret
    speculation_postfence
.end_function

// void Arm64LocalCleanAllCaches()
.function Arm64LocalCleanAllCaches, global, none
    cache_way_set_op csw, clean

    // dump the instruction cache as well
    ic      iallu
    isb

    ret
    speculation_postfence
.end_function

// void Arm64LocalCleanAndInvalidateAllCaches()
.function Arm64LocalCleanAndInvalidateAllCaches, global, none
    cache_way_set_op cisw, clean_invalidate

    // dump the instruction cache as well
    ic      iallu
    isb

    ret
    speculation_postfence
.end_function

