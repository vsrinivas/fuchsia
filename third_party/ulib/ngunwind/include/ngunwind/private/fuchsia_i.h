/* libunwind - a platform-independent unwind library
   Copyright 2016 The Fuchsia Authors. All rights reserved.

This file is part of libunwind.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.  */

// Internal fuchsia definitions.

#pragma once

#include <zircon/types.h>

#include <ngunwind/fuchsia.h>

#include "libunwind_i.h"

struct unw_fuchsia_info {
#if 0
    struct _Unwind_Context context; // must be first
    unw_addr_space_t remote_as;
#endif
    zx_handle_t process;
    zx_handle_t thread;

    // Dso lookup context, and lookup function.
    // The context is passed in to unw_create_fuchsia and is maintained by
    // lookup_dso.
    void* context;
    unw_dso_lookup_func_t* lookup_dso;

    uintptr_t segbase;
    struct as_elf_dyn_info edi;
};

extern const int fuchsia_greg_offset[];
