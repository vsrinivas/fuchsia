// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERNAL_STRING_SERIALIZATION_H_
#define LIB_FIDL_CPP_BINDINGS_INTERNAL_STRING_SERIALIZATION_H_

#include "lib/fidl/cpp/bindings/internal/array_internal.h"
#include "lib/fidl/cpp/bindings/string.h"

namespace fidl {

size_t GetSerializedSize_(const String& input);
void SerializeString_(const String& input,
                      internal::Buffer* buffer,
                      internal::String_Data** output);

void Deserialize_(internal::String_Data* input, String* output);

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERNAL_STRING_SERIALIZATION_H_
