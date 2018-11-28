// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fuchsia_port.h"
#include "garnet/lib/overnet/vocabulary/slice.h"
#include "garnet/lib/overnet/vocabulary/status.h"
#include "garnet/public/lib/fidl/cpp/coding_traits.h"
#include "garnet/public/lib/fidl/cpp/decoder.h"
#include "garnet/public/lib/fidl/cpp/encoder.h"

namespace overnetstack {

template <class T>
overnet::Slice EncodeMessage(T* message) {
  fidl::Encoder encoder(0);
  message->Encode(&encoder, encoder.Alloc(fidl::CodingTraits<T>::encoded_size));
  const auto& bytes = encoder.GetMessage().bytes();
  // Omit 16-byte RPC header.
  // TODO(ctiller): Fix FIDL API's for this use case.
  return overnet::Slice::FromCopiedBuffer(bytes.begin() + 16,
                                          bytes.actual() - 16);
}

template <class T>
overnet::StatusOr<T> DecodeMessage(overnet::Slice message) {
  fidl::Message fidl_msg(fidl::BytePart(const_cast<uint8_t*>(message.begin()),
                                        message.length(), message.length()),
                         fidl::HandlePart());
  const char* err_msg;
  auto status = fidl_msg.Decode(T::FidlType, &err_msg);
  if (status != ZX_OK) {
    return ToOvernetStatus(status).WithContext(err_msg);
  }
  fidl::Decoder decoder(std::move(fidl_msg));
  T result;
  T::Decode(&decoder, &result, 0);
  return result;
}

}  // namespace overnetstack
