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

#include "private/fuchsia_i.h"

HIDDEN const int fuchsia_greg_offset[] =
{
    0x000, // UNW_X86_64_RAX
    0x018, // UNW_X86_64_RDX
    0x010, // UNW_X86_64_RCX
    0x008, // UNW_X86_64_RBX
    0x020, // UNW_X86_64_RSI
    0x028, // UNW_X86_64_RDI
    0x030, // UNW_X86_64_RBP
    0x038, // UNW_X86_64_RSP
    0x040, // UNW_X86_64_R8
    0x048,
    0x050,
    0x058,
    0x060,
    0x068,
    0x070,
    0x078, // UNW_X86_64_R15
    0x080, // UNW_X86_64_RIP
};
