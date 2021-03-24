// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_FUZZING_DECODER_ENCODER_H_
#define LIB_FIDL_CPP_FUZZING_DECODER_ENCODER_H_

#include <stdint.h>
#include <zircon/types.h>

#include <functional>
#include <utility>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif

namespace fidl {
namespace fuzzing {

// An DecoderEncoder is a function that encapsulates the FIDL type-specific logic for attempting to
// decode and (if decode succeeds) re-encode a FIDL message via the interface documented at
// https://fuchsia.dev/fuchsia-src/reference/fidl/bindings/llcpp-bindings#encoding-decoding.
// Note that a function pointer is used instead of `::std::function` to facilitate header-only
// constexpr globals of type `::std::array<DecoderEncoder, n>`.
using DecoderEncoder = ::std::pair<zx_status_t, zx_status_t> (*)(uint8_t* bytes, uint32_t num_bytes,
                                                                 zx_handle_info_t* handles,
                                                                 uint32_t handle_actual);

}  // namespace fuzzing
}  // namespace fidl

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // LIB_FIDL_CPP_FUZZING_DECODER_ENCODER_H_
