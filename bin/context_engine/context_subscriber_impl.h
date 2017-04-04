// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/services/context/context_subscriber.fidl.h"

namespace maxwell {

class ContextRepository;

class ContextSubscriberImpl : public ContextSubscriber {
 public:
  ContextSubscriberImpl(ContextRepository* repository);
  ~ContextSubscriberImpl() override;

 private:
  // |ContextSubscriber|
  void Subscribe(
      const fidl::String& label,
      fidl::InterfaceHandle<ContextSubscriberLink> link_handle) override;

  ContextRepository* repository_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ContextSubscriberImpl);
};

}  // namespace maxwell
