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
  static constexpr char kSchema[] =
      "https://developers.google.com/maps/documentation/javascript/3.exp/"
      "reference#LatLngLiteral";
};

}  // namespace acquirers
}  // namespace maxwell
