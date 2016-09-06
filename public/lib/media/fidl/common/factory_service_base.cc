// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/services/common/factory_service_base.h"

#include "lib/ftl/logging.h"

namespace mojo {
namespace media {

FactoryServiceBase::ProductBase::ProductBase(FactoryServiceBase* owner)
    : owner_(owner) {
  FTL_DCHECK(owner_);
}

FactoryServiceBase::ProductBase::~ProductBase() {}

FactoryServiceBase::FactoryServiceBase() {}

FactoryServiceBase::~FactoryServiceBase() {}

}  // namespace media
}  // namespace mojo
