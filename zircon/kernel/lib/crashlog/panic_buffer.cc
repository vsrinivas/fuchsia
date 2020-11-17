// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/crashlog/panic_buffer.h>

#include <ktl/algorithm.h>

void PanicBuffer::Append(ktl::string_view s) {
  Guard<SpinLock, IrqSave> guard{&lock_};
  const size_t space_avail = sizeof(buffer_) - pos_ - 1;
  if (space_avail > 0) {
    size_t num_to_copy = ktl::min(s.size(), space_avail);
    memcpy(&buffer_[pos_], s.data(), num_to_copy);
    pos_ += num_to_copy;
  }
}
