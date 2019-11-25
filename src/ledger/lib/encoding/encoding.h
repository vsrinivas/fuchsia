// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_ENCODING_ENCODING_H_
#define SRC_LEDGER_LIB_ENCODING_ENCODING_H_

#include <lib/fidl/cpp/encoder.h>

#include <string>
#include <vector>

#include "src/lib/fsl/vmo/vector.h"
#include "src/lib/fxl/logging.h"

namespace ledger {

// Serializes T in a buffer. In our use case, we do not rely on FIDL encoding
// being stable: the buffer is simply a way to extend the maximum message size,
// and we expect the buffer to be immediately deserialized.
// Caveats: see https://fuchsia-review.googlesource.com/c/fuchsia/+/262309
// TODO(ambre): rewrite this using a more-supported API when available.
template <typename T>
bool EncodeToBuffer(T* data, fuchsia::mem::Buffer* buffer) {
  FXL_DCHECK(data);
  FXL_DCHECK(buffer);

  fidl::Encoder encoder(fidl::Encoder::NO_HEADER);
  // We need to preallocate the size of the structure in the encoder, the rest
  // is allocated when the vector is encoded.
  encoder.Alloc(fidl::EncodingInlineSize<T, fidl::Encoder>(&encoder));
  fidl::Encode(&encoder, data, 0);
  return fsl::VmoFromVector(encoder.TakeBytes(), buffer);
}

// Deserialization of T from a buffer. See the comment in EncodeToBuffer for
// details/caveats.
// TODO(ambre): rewrite this using a supported API when available.
template <typename T>
bool DecodeFromBuffer(const fuchsia::mem::Buffer& buffer, T* data) {
  FXL_DCHECK(data);

  std::vector<uint8_t> bytes;
  if (!fsl::VectorFromVmo(buffer, &bytes)) {
    return false;
  }

  if (!bytes.data()) {
    // Message::Decode cannot handle a nullptr input and has an assertion checking for this case.
    return false;
  }
  fidl::Message message(fidl::BytePart(bytes.data(), bytes.size(), bytes.size()),
                        fidl::HandlePart());
  const char* error_msg;
  zx_status_t status = message.Decode(T::FidlType, &error_msg);
  if (status != ZX_OK) {
    return false;
  }

  fidl::Decoder decoder(std::move(message));
  T::Decode(&decoder, data, 0);
  return true;
}

}  // namespace ledger

#endif  // SRC_LEDGER_LIB_ENCODING_ENCODING_H_
