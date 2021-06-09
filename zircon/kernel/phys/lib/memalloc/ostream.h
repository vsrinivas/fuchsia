// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_OSTREAM_H_
#define ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_OSTREAM_H_

#include <lib/memalloc/range.h>

#include <ostream>

namespace memalloc {

std::ostream& operator<<(std::ostream& stream, MemRange range);

}  // namespace memalloc

#endif  // ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_OSTREAM_H_
