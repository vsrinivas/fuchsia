// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/legacy_data_pipe/data_pipe_drainer.h"

#include <mojo/system/result.h>

#include <utility>

#include "lib/ftl/logging.h"

namespace mtl {

LegacyDataPipeDrainer::LegacyDataPipeDrainer(Client* client,
                                             const MojoAsyncWaiter* waiter)
    : client_(client), waiter_(waiter), wait_id_(0) {
  FTL_DCHECK(client_);
}

LegacyDataPipeDrainer::~LegacyDataPipeDrainer() {
  if (wait_id_)
    waiter_->CancelWait(wait_id_);
}

void LegacyDataPipeDrainer::Start(mojo::ScopedDataPipeConsumerHandle source) {
  source_ = std::move(source);
  ReadData();
}

void LegacyDataPipeDrainer::ReadData() {
  const void* buffer = nullptr;
  uint32_t num_bytes = 0;
  MojoResult rv = BeginReadDataRaw(source_.get(), &buffer, &num_bytes,
                                   MOJO_READ_DATA_FLAG_NONE);
  if (rv == MOJO_RESULT_OK) {
    client_->OnDataAvailable(buffer, num_bytes);
    EndReadDataRaw(source_.get(), num_bytes);
    WaitForData();
  } else if (rv == MOJO_SYSTEM_RESULT_SHOULD_WAIT) {
    WaitForData();
  } else if (rv == MOJO_SYSTEM_RESULT_FAILED_PRECONDITION) {
    client_->OnDataComplete();
  } else {
    FTL_DCHECK(false) << "Unhandled MojoResult: " << rv;
  }
}

void LegacyDataPipeDrainer::WaitForData() {
  FTL_DCHECK(!wait_id_);
  MojoHandle handle = source_.get().value();
  wait_id_ = waiter_->AsyncWait(handle, MOJO_HANDLE_SIGNAL_READABLE,
                                MOJO_DEADLINE_INDEFINITE, &WaitComplete, this);
}

void LegacyDataPipeDrainer::WaitComplete(void* context, MojoResult result) {
  LegacyDataPipeDrainer* drainer = static_cast<LegacyDataPipeDrainer*>(context);
  drainer->wait_id_ = 0;
  drainer->ReadData();
}

}  // namespace mtl
