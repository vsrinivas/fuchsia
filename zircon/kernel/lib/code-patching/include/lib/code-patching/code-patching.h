// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_CODE_PATCHING_INCLUDE_LIB_CODE_PATCHING_CODE_PATCHING_H_
#define ZIRCON_KERNEL_LIB_CODE_PATCHING_INCLUDE_LIB_CODE_PATCHING_CODE_PATCHING_H_

#include <lib/arch/nop.h>
#include <zircon/assert.h>

#include <cstddef>
#include <cstring>

#include <fbl/span.h>

namespace code_patching {

// A patch case identifier, corresponding to particular hard-coded details on
// how and when code should be the replaced.
using CaseId = uint32_t;

// A patch directive, giving the 'what' of an instruction range and the 'how'
// and 'when' of a patch case identifier.
struct Directive {
  uint64_t range_start;
  uint32_t range_size;
  CaseId id;
};

// Ensures against alignment padding.
static_assert(std::has_unique_object_representations_v<Directive>);

// Replaces a range of instuctions with the minimal number of `nop`
// instructions.
using arch::NopFill;

}  // namespace code_patching

#endif  // ZIRCON_KERNEL_LIB_CODE_PATCHING_INCLUDE_LIB_CODE_PATCHING_CODE_PATCHING_H_
