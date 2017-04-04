// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "apps/maxwell/services/context/context_publisher.fidl.h"

namespace maxwell {

class Repo;

class ContextPublisherImpl : public ContextPublisher {
 public:
  ContextPublisherImpl(const std::string& source_url, Repo* repo);
  ~ContextPublisherImpl() override;

 private:
  // |ContextPublisher|
  void Publish(const fidl::String& topic, const fidl::String& json_data) override;

  /* const std::string& source_url_; */  // Unused now, will be used soon.
  Repo* repo_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ContextPublisherImpl);
};

}  // namespace maxwell
