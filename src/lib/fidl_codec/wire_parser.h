// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CODEC_WIRE_PARSER_H_
#define SRC_LIB_FIDL_CODEC_WIRE_PARSER_H_

#include <lib/fidl/cpp/message.h>

#include <cstdint>

#include "src/lib/fidl_codec/library_loader.h"
#include "src/lib/fidl_codec/wire_types.h"

namespace fidl_codec {

// Given a wire-formatted |message| and a schema for that message represented by
// |method|, populates |decoded_object| with an object representing that
// request.
//
// Returns false if it cannot decode the message using the metadata associated
// with the method.
bool DecodeRequest(const InterfaceMethod* method, const uint8_t* bytes, uint32_t num_bytes,
                   const zx_handle_info_t* handles, uint32_t num_handles,
                   std::unique_ptr<Object>* decoded_object);

// Given a wire-formatted |message| and a schema for that message represented by
// |method|,  populates |decoded_object| with an object representing that
// response.
//
// Returns false if it cannot decode the message using the metadata associated
// with the method.
bool DecodeResponse(const InterfaceMethod* method, const uint8_t* bytes, uint32_t num_bytes,
                    const zx_handle_info_t* handles, uint32_t num_handles,
                    std::unique_ptr<Object>* decoded_object);

}  // namespace fidl_codec

#endif  // SRC_LIB_FIDL_CODEC_WIRE_PARSER_H_
