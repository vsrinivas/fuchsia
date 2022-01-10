// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_INTERNAL_TRANSPORT_CHANNEL_HOST_H_
#define LIB_FIDL_LLCPP_INTERNAL_TRANSPORT_CHANNEL_HOST_H_

#include <lib/fidl/llcpp/internal/transport.h>

namespace fidl::internal {

struct ChannelTransport {
  using HandleMetadata = fidl_channel_handle_metadata_t;

  static const TransportVTable VTable;
  static const CodingConfig EncodingConfiguration;
};

template <>
struct AssociatedTransportImpl<fidl_channel_handle_metadata_t> {
  using type = ChannelTransport;
};

}  // namespace fidl::internal

#endif  // LIB_FIDL_LLCPP_INTERNAL_TRANSPORT_CHANNEL_HOST_H_
