// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_WIRE_PARSER_H_
#define TOOLS_FIDLCAT_LIB_WIRE_PARSER_H_

#include <lib/fidl/cpp/message.h>

#include <cstdint>

#include "tools/fidlcat/lib/library_loader.h"
#include "tools/fidlcat/lib/wire_types.h"

namespace fidlcat {

// Given a wire-formatted |message| and a schema for that message represented by
// |method|, populates |decoded_object| with an object representing that
// request.
//
// Returns false if it cannot decode the message using the metadata associated
// with the method.
bool DecodeRequest(const InterfaceMethod* method, const uint8_t* bytes, uint32_t num_bytes,
                   const zx_handle_t* handles, uint32_t num_handles,
                   std::unique_ptr<Object>* decoded_object);

// Given a wire-formatted |message| and a schema for that message represented by
// |method|,  populates |decoded_object| with an object representing that
// response.
//
// Returns false if it cannot decode the message using the metadata associated
// with the method.
bool DecodeResponse(const InterfaceMethod* method, const uint8_t* bytes, uint32_t num_bytes,
                    const zx_handle_t* handles, uint32_t num_handles,
                    std::unique_ptr<Object>* decoded_object);

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_WIRE_PARSER_H_
