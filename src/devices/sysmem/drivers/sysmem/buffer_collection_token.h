// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_SYSMEM_SYSMEM_BUFFER_COLLECTION_TOKEN_H_
#define ZIRCON_SYSTEM_DEV_SYSMEM_SYSMEM_BUFFER_COLLECTION_TOKEN_H_

#include <fuchsia/sysmem/c/fidl.h>
#include <lib/fidl-async-2/fidl_server.h>
#include <lib/fidl-async-2/simple_binding.h>

#include "logging.h"
#include "logical_buffer_collection.h"

namespace sysmem_driver {

class BufferCollectionToken
    : public FidlServer<
          BufferCollectionToken,
          SimpleBinding<BufferCollectionToken, fuchsia_sysmem_BufferCollectionToken_ops_t,
                        fuchsia_sysmem_BufferCollectionToken_dispatch>,
          vLog> {
 public:
  ~BufferCollectionToken();

  zx_status_t Duplicate(uint32_t rights_attenuation_mask,
                        zx_handle_t buffer_collection_token_request);
  zx_status_t Sync(fidl_txn_t* txn);
  zx_status_t Close();

  LogicalBufferCollection* parent();
  fbl::RefPtr<LogicalBufferCollection> parent_shared();

  void SetServerKoid(zx_koid_t server_koid);

  zx_koid_t server_koid();

  bool is_done();

  void SetBufferCollectionRequest(zx::channel buffer_collection_request);

  zx::channel TakeBufferCollectionRequest();

 private:
  friend class FidlServer;
  BufferCollectionToken(Device* parent_device, fbl::RefPtr<LogicalBufferCollection> parent,
                        uint32_t rights_attenuation_mask);

  static const fuchsia_sysmem_BufferCollectionToken_ops_t kOps;

  Device* parent_device_ = nullptr;
  fbl::RefPtr<LogicalBufferCollection> parent_;
  // 1 bit means the right is allowed.  0 bit means the right is attenuated.
  uint32_t rights_attenuation_mask_ = 0;

  zx_koid_t server_koid_ = ZX_KOID_INVALID;

  // Becomes true on the first Close() or BindSharedCollection().  This being
  // true means a channel close is not fatal to the LogicalBufferCollection.
  // However, if the client sends a redundant Close(), that is fatal to the
  // BufferCollectionToken and fatal to the LogicalBufferCollection.
  bool is_done_ = false;

  // This is set up to once during
  // LogicalBufferCollection::BindSharedCollection(), and essentially curries
  // the buffer_collection_request past the processing of any remaining
  // inbound messages on the BufferCollectionToken before starting to serve
  // the BufferCollection that the token was exchanged for.  This way, inbound
  // Duplicate() messages in the BufferCollectionToken are seen before any
  // BufferCollection::SetConstraints() (which might otherwise try to allocate
  // buffers too soon before all tokens are gone).
  zx::channel buffer_collection_request_;
};

}  // namespace sysmem_driver

#endif  // ZIRCON_SYSTEM_DEV_SYSMEM_SYSMEM_BUFFER_COLLECTION_TOKEN_H_
