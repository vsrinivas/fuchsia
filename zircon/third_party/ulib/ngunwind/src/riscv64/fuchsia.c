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

#include "fuchsia_i.h"

HIDDEN const int fuchsia_greg_offset[] =
{
    0x000,
    0x008,
    0x010,
    0x018,
    0x020,
    0x028,
    0x030,
    0x038,
    0x040,
    0x048,
    0x050,
    0x058,
    0x060,
    0x068,
    0x070,
    0x078,
    0x080,
    0x088,
    0x090,
    0x098,
    0x0a0,
    0x0a8,
    0x0b0,
    0x0b8,
    0x0c0,
    0x0c8,
    0x0d0,
    0x0d8,
    0x0e0,
    0x0e8,
    0x0f0,
    0x0f8,
    0x100,
    0x108,
};
