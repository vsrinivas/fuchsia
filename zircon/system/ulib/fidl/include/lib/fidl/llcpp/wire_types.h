// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_WIRE_TYPES_H_
#define LIB_FIDL_LLCPP_WIRE_TYPES_H_

#ifdef __Fuchsia__
#include <lib/fidl/llcpp/internal/transport_channel.h>
#endif  // __Fuchsia__

// # Wire domain objects
//
// This header contains forward definitions that are part of wire domain
// objects. The code generator should populate the implementation by generating
// template specializations for each FIDL data type.
namespace fidl {

// |WireTableFrame| stores the envelope header for each field in a table.
// In their current wire format representation, a table is a vector of
// envelopes. The table frame is the vector body portion of the table.
//
// It is recommended that table frames are managed automatically using arenas.
// Only directly construct a table frame when performance is key and arenas are
// insufficient. Once created, a frame can only be used for one single table.
template <typename FidlTable>
struct WireTableFrame;

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_WIRE_TYPES_H_
