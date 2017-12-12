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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <zircon/types.h>

#include "libunwind.h"

typedef int (unw_dso_lookup_func_t) (void* context, unw_word_t pc,
                                     unw_word_t* base, const char** name);

typedef struct unw_fuchsia_info unw_fuchsia_info_t;

extern unw_fuchsia_info_t* unw_create_fuchsia(zx_handle_t process,
                                              zx_handle_t thread,
                                              void* context,
                                              unw_dso_lookup_func_t* lookup_dso);

extern void unw_destroy_fuchsia(unw_fuchsia_info_t*);

extern const unw_accessors_t _UFuchsia_accessors;

#ifdef __cplusplus
}
#endif
