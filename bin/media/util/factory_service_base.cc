// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/util/factory_service_base.h"

#include "lib/ftl/logging.h"

FactoryServiceBase::ProductBase::ProductBase(FactoryServiceBase* owner)
    : owner_(owner) {
  FTL_DCHECK(owner_);
}

FactoryServiceBase::ProductBase::~ProductBase() {}

FactoryServiceBase::FactoryServiceBase()
    : application_context_(
          app::ApplicationContext::CreateFromStartupInfo()) {}

FactoryServiceBase::~FactoryServiceBase() {}
