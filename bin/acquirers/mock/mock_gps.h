// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/services/context/context_engine.fidl.h"
#include "apps/maxwell/src/acquirers/gps.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace maxwell {
namespace acquirers {

class MockGps : public GpsAcquirer {
 public:
  MockGps(ContextEngine* context_engine);
  void Publish(float latitude, float longitude);

 private:
  ContextPublisherLinkPtr out_;
};

}  // namespace acquirers
}  // namespace maxwell
