// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_WEAVESTACK_FIDL_STACK_UTILS_H_
#define SRC_CONNECTIVITY_WEAVE_WEAVESTACK_FIDL_STACK_UTILS_H_

#include <fuchsia/weave/cpp/fidl.h>

namespace weavestack {

fuchsia::weave::PairingState CurrentPairingState();
fuchsia::weave::Host HostFromHostname(std::string hostname);

}  // namespace weavestack

#endif
