// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/connectivity/overnet/lib/embedded/basic_overnet_embedded.h"
#include "src/connectivity/overnet/lib/links/stream_link.h"
#include "src/connectivity/overnet/lib/protocol/stream_framer.h"
#include "src/connectivity/overnet/lib/vocabulary/socket.h"

namespace overnet {

void RegisterStreamSocketLink(BasicOvernetEmbedded* app, Socket socket,
                              std::unique_ptr<StreamFramer> framer,
                              bool eager_announce, TimeDelta read_timeout,
                              Callback<void> destroyed);

}  // namespace overnet
