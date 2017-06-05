// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/util/factory_service_base.h"

#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

FactoryServiceBase::ProductBase::ProductBase(FactoryServiceBase* owner)
    : owner_(owner) {
  FTL_DCHECK(owner_);
}

FactoryServiceBase::ProductBase::~ProductBase() {
  if (quit_on_destruct_) {
    mtl::MessageLoop::GetCurrent()->PostQuitTask();
  }
}

FactoryServiceBase::FactoryServiceBase()
    : application_context_(app::ApplicationContext::CreateFromStartupInfo()),
      task_runner_(mtl::MessageLoop::GetCurrent()->task_runner()) {}

FactoryServiceBase::~FactoryServiceBase() {}

void FactoryServiceBase::RemoveProduct(std::shared_ptr<ProductBase> product) {
  ftl::MutexLocker locker(&mutex_);
  bool erased = products_.erase(product);
  FTL_DCHECK(erased);
}
