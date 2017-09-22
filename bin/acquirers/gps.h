// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace maxwell {
namespace acquirers {

class GpsAcquirer {
 public:
  virtual ~GpsAcquirer() {}

  static constexpr char kLabel[] = "/location/gps";
};

}  // namespace acquirers
}  // namespace maxwell
