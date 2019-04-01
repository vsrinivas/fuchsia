// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/string_printf.h>

#include <array>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include <zircon/assert.h>
#include <fbl/unique_ptr.h>

namespace fbl {

String StringPrintf(const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    String rv = StringVPrintf(format, ap);
    va_end(ap);
    return rv;
}

String StringVPrintf(const char* format, va_list ap) {
    // Size of the small stack buffer to use first. This should be kept in sync
    // with the numbers in StringPrintfTest.StringPrintf_Boundary.
    constexpr size_t kStackBufferSize = 1024U;

    // First, try with a small buffer on the stack.
    std::array<char, kStackBufferSize> stack_buf;
    // Copy |ap| (which can only be used once), in case we need to retry.
    va_list ap_copy;
    va_copy(ap_copy, ap);

    // TODO(johngro): Remove this when clang-tidy (or the clang analyzer) gets
    // fixed.  Right now, it seems unaware that va_copy is initializing
    // |ap_copy|.
    //
    // NOLINTNEXTLINE (clang-analyzer-valist.Uninitialized)
    int result = vsnprintf(stack_buf.data(), kStackBufferSize, format, ap_copy);
    va_end(ap_copy);
    if (result < 0) {
        // As far as I can tell, we'd only get |EOVERFLOW| if the result is so large
        // that it can't be represented by an |int| (in which case retrying would be
        // futile), so Chromium's implementation is wrong.
        return String();
    }
    // |result| should be the number of characters we need, not including the
    // terminating null. However, |vsnprintf()| always null-terminates!
    size_t output_size = static_cast<size_t>(result);
    // Check if the output fit into our stack buffer. This is "<" not "<=", since
    // |vsnprintf()| will null-terminate.
    if (output_size < kStackBufferSize) {
        // It fit.
        return String(stack_buf.data(), static_cast<size_t>(result));
    }

    // Since we have the required output size, we can just heap allocate that.
    // (Add 1 because |vsnprintf()| will always null-terminate.)
    size_t heap_buf_size = output_size + 1U;

    // TODO(johngro): remove this exception if clang-tidy learns to ignore this.
    // Our configuration of clang-tidy really does not want to see _any_
    // traditional C arrays in it's world, so it demands that you convert them
    // all to std::arrays.  In this case, however, that is not good advice.  The
    // code below is deliberately dynamically allocating a character buffer to
    // sprintf into.  Since the point of std::array is to have a compile time
    // sized array type, it is really not appropriate to use here.
    //
    // NOLINTNEXTLINE (modernize-avoid-c-arrays)
    std::unique_ptr<char[]> heap_buf(new char[heap_buf_size]);
    result = vsnprintf(heap_buf.get(), heap_buf_size, format, ap);
    ZX_ASSERT(result >= 0 && static_cast<size_t>(result) == output_size);
    return String(heap_buf.get(), static_cast<size_t>(result));
}

} // namespace fbl
