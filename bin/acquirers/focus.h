// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace maxwell {
namespace acquirers {

class FocusAcquirer {
 public:
  virtual ~FocusAcquirer() {}

  static constexpr char kLabel[] = "/modular_state";
  static constexpr char kSchema[] = "int";
};

}  // namespace acquirers
}  // namespace maxwell
