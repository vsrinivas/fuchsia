// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_SERVER_END_H_
#define LIB_FIDL_LLCPP_SERVER_END_H_

#include <lib/fidl/epitaph.h>
#include <lib/fidl/llcpp/internal/transport.h>
#include <lib/fidl/llcpp/internal/transport_end.h>
#include <lib/fidl/llcpp/soft_migration.h>
#include <lib/fidl/llcpp/traits.h>
#include <lib/zx/channel.h>
#include <zircon/assert.h>

namespace fidl {
namespace internal {

template <typename Protocol, typename Transport>
class ServerEndBase : public TransportEnd<Protocol, Transport> {
  using TransportEnd = TransportEnd<Protocol, Transport>;

 public:
  using TransportEnd::TransportEnd;
};

}  // namespace internal

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_SERVER_END_H_
