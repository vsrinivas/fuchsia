// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace zxdb {

// The type used for storing pointers on the target system. This is to
// help differentiate pointers from random integers.
//
// This is the static type used to store pointer data, so it will always have
// to be large enough to hold pointers for the largest-bittedness-system we
// support. Because this won't change with the target architecture,
// computations involving the bit size of the target architecture should use
// kTargetPointerSize.
using TargetPointer = uint64_t;

// Size of a pointer on the target system. Currently we only support 64-bit.
// Use this constant isntead of sizeof(TargetPointer) so that in the future if
// we support non-64-bit target architectures we can search for this constant
// and replace it with a more complex query that uses the computed pointer
// size for the declared architecture.
constexpr size_t kTargetPointerSize = sizeof(TargetPointer);

}  // namespace zxdb
