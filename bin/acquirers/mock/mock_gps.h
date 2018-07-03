// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_ACQUIRERS_MOCK_MOCK_GPS_H_
#define PERIDOT_BIN_ACQUIRERS_MOCK_MOCK_GPS_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

#include "peridot/bin/acquirers/gps.h"

namespace maxwell {
namespace acquirers {

class MockGps : public GpsAcquirer {
 public:
  MockGps(fuchsia::modular::ContextEngine* context_engine);
  void Publish(float latitude, float longitude);

 private:
  fuchsia::modular::ContextWriterPtr writer_;
};

}  // namespace acquirers
}  // namespace maxwell

#endif  // PERIDOT_BIN_ACQUIRERS_MOCK_MOCK_GPS_H_
