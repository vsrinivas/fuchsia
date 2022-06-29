// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_UNKNOWN_INTERACTION_HANDLER_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_UNKNOWN_INTERACTION_HANDLER_H_

#include <lib/fidl/llcpp/transaction.h>
#include <lib/fidl/llcpp/unknown_interactions.h>

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
  UnknownInteractionType unknown_interaction_type;
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

// Interface implemented by FIDL open and ajar protocols to handle unknown interactions.
template <typename Protocol>
class UnknownMethodHandler {
 public:
  virtual ~UnknownMethodHandler() = default;

  virtual void handle_unknown_method(UnknownMethodMetadata<Protocol> metadata,
                                     UnknownMethodCompleter::Sync& completer) = 0;
};

}  // namespace fidl

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_UNKNOWN_INTERACTION_HANDLER_H_
