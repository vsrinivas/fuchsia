// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/asm.h>
#include <lib/arch/cache.h>

.text

// void InvalidateLocalCaches()
.function InvalidateLocalCaches, global, none
   data_cache_way_set_op isw, invalidate
   ic  iallu
   isb

   ret
   speculation_postfence
.end_function

// void CleanLocalCaches()
.function CleanLocalCaches, global, none
   data_cache_way_set_op csw, clean
   ic  iallu
   isb

   ret
   speculation_postfence
.end_function

// void CleanAndInvalidateLocalCaches()
.function CleanAndInvalidateLocalCaches, global, none
   data_cache_way_set_op cisw, clean_and_invalidate
   ic  iallu
   isb

   ret
   speculation_postfence
.end_function

