// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/Support/crypto/WeaveRNG.h>
// clang-format on

using namespace ::nl;
using namespace ::nl::Weave;

namespace nl {
namespace Weave {
namespace DeviceLayer {
namespace Internal {

WEAVE_ERROR InitEntropy() { return WEAVE_NO_ERROR; }

}  // namespace Internal
}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl
