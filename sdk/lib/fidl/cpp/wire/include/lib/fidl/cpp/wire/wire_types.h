// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_WIRE_TYPES_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_WIRE_TYPES_H_

#include <lib/fidl/cpp/wire/decoded_value.h>
#include <lib/fidl/cpp/wire/envelope.h>
#include <lib/fidl/cpp/wire/object_view.h>
#include <lib/fidl/cpp/wire/wire_coding_traits.h>
#include <lib/fidl/cpp/wire_format_metadata.h>
#include <lib/stdcompat/span.h>
#include <zircon/assert.h>

#ifdef __Fuchsia__
#include <lib/fidl/cpp/wire/internal/transport_channel.h>
#else
#include <lib/fidl/cpp/wire/internal/transport_channel_host.h>
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

// |WireTableBuilder| is a helper class for building tables. They're created by
// calling the static |Build(AnyArena&)| on a FIDL wire table type. The table's
// frame and members will be allocated from supplied arena.
//
// Table members are set by passing constructor arguments or |ObjectView|s into
// a builder method named for the member.
//
// To get the built table call |Build()|. The builder must not be used after
// |Build()| has been called.
template <typename FidlTable>
class WireTableBuilder;

// |WireTableExternalBuilder| is a low-level helper class for building tables.
// They're created by calling the static
// |Build(fidl::ObjectView<fidl::WireTableFrame<T>>)| on a FIDL wire table type,
// passing in an externally managed table frame object view.
//
// Table members are set by passing |ObjectView|s into a builder method named
// for the member.
//
// To get the built table call |Build()|. The builder must not be used after
// |Build()| has been called.
template <typename FidlTable>
class WireTableExternalBuilder;

namespace internal {

// |WireTableBaseBuilder| holds the shared code between |WireTableBuilder| and
// |WireTableExternalBuilder|. It shouldn't be used directly.
template <typename FidlTable, typename Builder>
class WireTableBaseBuilder;

}  // namespace internal

}  // namespace fidl

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_WIRE_TYPES_H_
