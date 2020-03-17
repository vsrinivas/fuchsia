// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_VSL_CONTEXT_H
#define MSD_VSL_CONTEXT_H

#include <memory>

#include "address_space.h"
#include "magma_util/macros.h"
#include "mapped_batch.h"
#include "msd.h"
#include "msd_vsl_connection.h"
#include "ringbuffer.h"

class MsdVslContext {
 public:
  static std::shared_ptr<MsdVslContext> Create(std::weak_ptr<MsdVslConnection> connection,
                                               std::shared_ptr<AddressSpace> address_space,
                                               Ringbuffer* ringbuffer);

  MsdVslContext(std::weak_ptr<MsdVslConnection> connection,
                std::shared_ptr<AddressSpace> address_space)
      : connection_(connection), address_space_(std::move(address_space)) {}

  std::shared_ptr<AddressSpace> exec_address_space() { return address_space_; }
  std::weak_ptr<MsdVslConnection> connection() { return connection_; }

  magma::Status SubmitBatch(std::unique_ptr<MappedBatch> batch);

  static std::unique_ptr<MappedBatch> CreateBatch(std::shared_ptr<MsdVslContext> context,
                                                  magma_system_command_buffer* cmd_buf,
                                                  magma_system_exec_resource* exec_resources,
                                                  msd_buffer_t** msd_buffers,
                                                  msd_semaphore_t** msd_wait_semaphores,
                                                  msd_semaphore_t** msd_signal_semaphores);

  bool MapRingbuffer(Ringbuffer* ringbuffer);

 private:
  std::weak_ptr<MsdVslConnection> connection_;
  std::shared_ptr<AddressSpace> address_space_;
};

class MsdVslAbiContext : public msd_context_t {
 public:
  MsdVslAbiContext(std::shared_ptr<MsdVslContext> ptr) : ptr_(std::move(ptr)) { magic_ = kMagic; }

  static MsdVslAbiContext* cast(msd_context_t* context) {
    DASSERT(context);
    DASSERT(context->magic_ == kMagic);
    return static_cast<MsdVslAbiContext*>(context);
  }
  std::shared_ptr<MsdVslContext> ptr() { return ptr_; }

 private:
  std::shared_ptr<MsdVslContext> ptr_;
  static const uint32_t kMagic = 0x63747874;  // "ctxt"
};

#endif  // MSD_VSL_CONTEXT_H
