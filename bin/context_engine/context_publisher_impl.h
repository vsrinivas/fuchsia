// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "apps/maxwell/services/context/context_publisher.fidl.h"
#include "apps/maxwell/services/user/scope.fidl.h"

namespace maxwell {

class ContextRepository;

class ContextPublisherImpl : public ContextPublisher {
 public:
  ContextPublisherImpl(ComponentScopePtr scope,
                       ContextRepository* repository);
  ~ContextPublisherImpl() override;

 private:
  // |ContextPublisher|
  void Publish(const fidl::String& topic,
               const fidl::String& json_data) override;

  ComponentScopePtr scope_;
  ContextRepository* repository_;  // Not owned.

  FTL_DISALLOW_COPY_AND_ASSIGN(ContextPublisherImpl);
};

}  // namespace maxwell
