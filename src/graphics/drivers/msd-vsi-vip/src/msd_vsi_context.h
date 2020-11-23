// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_VSI_CONTEXT_H
#define MSD_VSI_CONTEXT_H

#include <memory>

#include "address_space.h"
#include "magma_util/macros.h"
#include "mapped_batch.h"
#include "msd.h"
#include "msd_vsi_connection.h"
#include "ringbuffer.h"

class MsdVsiContext {
 public:
  static std::shared_ptr<MsdVsiContext> Create(std::weak_ptr<MsdVsiConnection> connection,
                                               std::shared_ptr<AddressSpace> address_space,
                                               Ringbuffer* ringbuffer);

  MsdVsiContext(std::weak_ptr<MsdVsiConnection> connection,
                std::shared_ptr<AddressSpace> address_space)
      : connection_(connection), address_space_(std::move(address_space)) {}

  std::shared_ptr<AddressSpace> exec_address_space() { return address_space_; }
  std::weak_ptr<MsdVsiConnection> connection() { return connection_; }

  magma::Status SubmitBatch(std::unique_ptr<MappedBatch> batch);

  // |exec_resources| may contain up to 2 resources. If resources are provided,
  // one of the resources must be the batch buffer. The other resource may be an optional
  // context state buffer, which will be executed before the batch buffer if |context|
  // differs from the context of the last executed command buffer.
  static std::unique_ptr<MappedBatch> CreateBatch(std::shared_ptr<MsdVsiContext> context,
                                                  magma_system_command_buffer* cmd_buf,
                                                  magma_system_exec_resource* exec_resources,
                                                  msd_buffer_t** msd_buffers,
                                                  msd_semaphore_t** msd_wait_semaphores,
                                                  msd_semaphore_t** msd_signal_semaphores);

  bool MapRingbuffer(Ringbuffer* ringbuffer);

  void Kill();

  bool killed() { return killed_; }

 private:
  std::weak_ptr<MsdVsiConnection> connection_;
  std::shared_ptr<AddressSpace> address_space_;
  std::atomic_bool killed_ = false;
};

class MsdVsiAbiContext : public msd_context_t {
 public:
  MsdVsiAbiContext(std::shared_ptr<MsdVsiContext> ptr) : ptr_(std::move(ptr)) { magic_ = kMagic; }

  static MsdVsiAbiContext* cast(msd_context_t* context) {
    DASSERT(context);
    DASSERT(context->magic_ == kMagic);
    return static_cast<MsdVsiAbiContext*>(context);
  }
  std::shared_ptr<MsdVsiContext> ptr() { return ptr_; }

 private:
  std::shared_ptr<MsdVsiContext> ptr_;
  static const uint32_t kMagic = 0x63747874;  // "ctxt"
};

#endif  // MSD_VSI_CONTEXT_H
