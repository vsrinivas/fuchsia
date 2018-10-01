// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace zxdb {

// Options to pass to ResolveInputLocation that controls how symbols are
// converted to addresses.
struct ResolveOptions {
  // Set to true to symbolize the results. Otherwise the results will be just
  // addresses.
  bool symbolize = true;
};

}  // namespace zxdb
