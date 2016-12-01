// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/services/context/context_engine.fidl.h"
#include "apps/maxwell/src/acquirers/gps.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace maxwell {
namespace acquirers {

class MockGps : public GpsAcquirer, public ContextPublisherController {
 public:
  MockGps(ContextEngine* context_engine);
  void Publish(float latitude, float longitude);
  void OnHasSubscribers() override;
  void OnNoSubscribers() override;

  bool has_subscribers() const { return has_subscribers_; }

 private:
  fidl::Binding<ContextPublisherController> ctl_;
  ContextPublisherLinkPtr out_;
  bool has_subscribers_ = false;
};

}  // namespace acquirers
}  // namespace maxwell
