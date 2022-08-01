// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_NATURAL_MESSAGING_H_
#define LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_NATURAL_MESSAGING_H_

#include <lib/fdf/cpp/arena.h>
#include <lib/fidl/cpp/internal/thenable.h>
#include <lib/fidl/cpp/wire/internal/server_details.h>
#include <lib/fidl/cpp/wire/message.h>
#include <lib/fidl/cpp/wire/unknown_interaction_handler.h>
#include <lib/fidl_driver/cpp/internal/endpoint_conversions.h>
#include <lib/fidl_driver/cpp/natural_client.h>
#include <lib/fidl_driver/cpp/unified_messaging_declarations.h>

namespace fdf::internal {

extern const char* const kFailedToCreateDriverArena;

fidl::OutgoingMessage MoveToArena(fidl::OutgoingMessage& message, const fdf::Arena& arena);

}  // namespace fdf::internal

#endif  // LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_NATURAL_MESSAGING_H_
