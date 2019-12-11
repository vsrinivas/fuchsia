// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/clocks/testing/device_id_manager_empty_impl.h"

#include "src/ledger/lib/logging/logging.h"

namespace clocks {

DeviceIdManagerEmptyImpl::DeviceIdManagerEmptyImpl() = default;
DeviceIdManagerEmptyImpl::~DeviceIdManagerEmptyImpl() = default;

ledger::Status DeviceIdManagerEmptyImpl::OnPageDeleted(coroutine::CoroutineHandler* /*handler*/) {
  LEDGER_NOTIMPLEMENTED();
  return ledger::Status::OK;
}

ledger::Status DeviceIdManagerEmptyImpl::GetNewDeviceId(coroutine::CoroutineHandler* /*handler*/,
                                                        DeviceId* device_id) {
  LEDGER_NOTIMPLEMENTED();
  *device_id = {"device", 0};
  return ledger::Status::OK;
}

}  // namespace clocks
