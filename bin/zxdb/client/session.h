// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace zxdb {

// TODO(brettw) this class will store information on the current debug session.
class Session {
 public:
  Session();
  ~Session();
};

}  // namespace zxdb
