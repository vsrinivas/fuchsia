// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

// [START include]
#include <fuchsia/examples/cpp/fidl_v2.h>
// [END include]

namespace {

// Verify that the wire bindings is available.
using WireFileMode = fuchsia_examples::wire::FileMode;
using ProtocolMarker = fuchsia_examples::Echo;

// Verify that the HLCPP domain objects are available.
// TODO(fxbug.dev/60240): We should alias the HLCPP types into the
// unified namespace (`fuchsia_examples`).
using NaturalFileMode = fuchsia::examples::FileMode;

}  // namespace
