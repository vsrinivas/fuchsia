// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/forward_declarations.h"
#include "escher/resources/resource_core.h"
#include "ftl/memory/ref_counted.h"

namespace escher {

// Base class for any resource that must be kept alive until all CommandBuffers
// that reference it have finished executing.
// TODO: named Resource2 to avoid confusion with impl::Resource.  The goal is
// to get rid of impl::Resource, and then rename this class to Resource.
class Resource2 : public ftl::RefCountedThreadSafe<Resource2> {
 public:
  Resource2(std::unique_ptr<ResourceCore> core);
  virtual ~Resource2();

  void KeepAlive(impl::CommandBuffer* command_buffer);

  const ResourceCore* core() const { return core_.get(); }

 private:
  virtual void KeepDependenciesAlive(impl::CommandBuffer* command_buffer) = 0;

  std::unique_ptr<ResourceCore> core_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Resource2);
};

typedef ftl::RefPtr<Resource2> Resource2Ptr;

}  // namespace escher
