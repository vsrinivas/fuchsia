// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_UNKNOWN_INTERACTION_HANDLER_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_UNKNOWN_INTERACTION_HANDLER_H_

#include <lib/fidl/cpp/wire/transaction.h>
#include <lib/fidl/cpp/wire/unknown_interactions.h>

namespace fidl {

using UnknownMethodCompleter = ::fidl::Completer<>;

// Base template for unknown interaction metadata.
template <typename Protocol, typename Enable = void>
struct UnknownMethodMetadata;

// Unknown interaction metadata for open protocols.
//
// Allows UnknownMethodHandler on the server to inspect the ordinal and
// direction of a method that was called.
template <typename Protocol>
struct UnknownMethodMetadata<
    Protocol, std::enable_if_t<Protocol::kOpenness == ::fidl::internal::Openness::kOpen, void>> {
  // Ordinal of the method that was called.
  uint64_t method_ordinal;
  // Whether the method that was called was a one-way method or a two-way
  // method.
  UnknownMethodType unknown_interaction_type;
};

// Unknown interaction metadata for ajar protocols.
//
// Allows UnknownMethodHandler to inspect the ordinal of a method that was
// called.
template <typename Protocol>
struct UnknownMethodMetadata<
    Protocol, std::enable_if_t<Protocol::kOpenness == ::fidl::internal::Openness::kAjar, void>> {
  // Ordinal of the method that was called.
  uint64_t method_ordinal;
};

// Interface implemented by FIDL open and ajar protocols to handle unknown
// methods on the server.
template <typename Protocol>
class UnknownMethodHandler {
 public:
  virtual ~UnknownMethodHandler() = default;

  virtual void handle_unknown_method(UnknownMethodMetadata<Protocol> metadata,
                                     UnknownMethodCompleter::Sync& completer) = 0;
};

// Base template for unknown interaction metadata.
template <typename Protocol, typename Enable = void>
struct UnknownEventMetadata;

// Unknown interaction metadata for open or ajar protocols.
//
// Allows UnknownEventHandler on the client to inspect the ordinal of a method
// that was called.
template <typename Protocol>
struct UnknownEventMetadata<
    Protocol, std::enable_if_t<Protocol::kOpenness == ::fidl::internal::Openness::kOpen ||
                                   Protocol::kOpenness == ::fidl::internal::Openness::kAjar,
                               void>> {
  // Ordinal of the method that was called.
  uint64_t method_ordinal;
};

// Interface implemented by FIDL open and ajar protocols to handle unknown
// events on the client.
template <typename Protocol>
class UnknownEventHandler {
 public:
  virtual ~UnknownEventHandler() = default;

  virtual void handle_unknown_event(UnknownEventMetadata<Protocol> metadata) = 0;
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_UNKNOWN_INTERACTION_HANDLER_H_
