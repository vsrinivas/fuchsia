// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/services/suggestion/shell_client.fidl.h"
#include "lib/fidl/cpp/bindings/formatting.h"

namespace maxwell {
namespace suggestion {

std::ostream& operator<<(std::ostream& os,
                         const maxwell::suggestion::Display& o);
std::ostream& operator<<(std::ostream& os, const Suggestion& o);

}  // namespace suggestion
}  // namespace maxwell
