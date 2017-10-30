// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_ACQUIRERS_GPS_H_
#define PERIDOT_BIN_ACQUIRERS_GPS_H_

namespace maxwell {
namespace acquirers {

class GpsAcquirer {
 public:
  virtual ~GpsAcquirer() {}

  static constexpr char kLabel[] = "/location/gps";
};

}  // namespace acquirers
}  // namespace maxwell

#endif  // PERIDOT_BIN_ACQUIRERS_GPS_H_
