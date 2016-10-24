// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/interfaces/context_engine.mojom.h"
#include "mojo/public/interfaces/application/shell.mojom.h"

#include "apps/maxwell/acquirers/gps.h"
#include "mojo/public/cpp/bindings/binding.h"

namespace maxwell {
namespace acquirers {

class MockGps : public GpsAcquirer,
                public context_engine::ContextPublisherController {
 public:
  MockGps(mojo::Shell* shell);
  void Publish(float latitude, float longitude);
  void OnHasSubscribers() override;
  void OnNoSubscribers() override;

  bool has_subscribers() const { return has_subscribers_; }

 private:
  mojo::Binding<context_engine::ContextPublisherController> ctl_;
  context_engine::ContextPublisherLinkPtr out_;
  bool has_subscribers_ = false;
};

}  // namespace acquirers
}  // namespace maxwell
