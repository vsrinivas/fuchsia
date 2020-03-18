// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/defer.h>
#include <lib/trace-provider/provider.h>
#include <lib/trace/event.h>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <virtio/block.h>

#include "src/virtualization/bin/vmm/device/block.h"
#include "src/virtualization/bin/vmm/device/block_dispatcher.h"
#include "src/virtualization/bin/vmm/device/device_base.h"
#include "src/virtualization/bin/vmm/device/stream_base.h"

enum class Queue : uint16_t {
  REQUEST = 0,
};

// A single asynchronous block request.
class Request : public fbl::RefCounted<Request> {
 public:
  Request(VirtioChain chain) : chain_(std::move(chain)) {
    TRACE_FLOW_BEGIN("machina", "block:request", nonce_);
  }

  ~Request() {
    if (status_ptr_ != nullptr) {
      *status_ptr_ = status_;
    }
    chain_.Return();
    TRACE_FLOW_END("machina", "block:request", nonce_);
  }

  bool NextDescriptor(VirtioDescriptor* desc, bool writable) {
    TRACE_DURATION("machina", "Request::NextDescriptor");
    bool has_next;
    // Read the next descriptor. If we have previously encountered an error,
    // keep reading descriptors until we find the status byte.
    do {
      has_next = chain_.NextDescriptor(desc);
      if (desc->len == 1 && desc->writable && !chain_.HasDescriptor()) {
        // A request ends with a single status byte.
        status_ptr_ = static_cast<uint8_t*>(desc->addr);
        return false;
      } else if (desc->writable != writable) {
        // If a request is not block-sized, or does not match expected
        // writability, set status to error.
        status_ = VIRTIO_BLK_S_IOERR;
      }
    } while (has_next && status_ != VIRTIO_BLK_S_OK);
    return has_next;
  }

  void SetStatus(uint8_t status) { status_ = status; }
  void AddUsed(uint32_t used) { *chain_.Used() += used; }

 private:
  VirtioChain chain_;
  trace_async_id_t nonce_ = TRACE_NONCE();
  uint8_t status_ = VIRTIO_BLK_S_OK;
  uint8_t* status_ptr_ = nullptr;
};

// Stream for request queue.
class RequestStream : public StreamBase {
 public:
  void Init(std::unique_ptr<BlockDispatcher> disp, const std::string& id, const PhysMem& phys_mem,
            VirtioQueue::InterruptFn interrupt) {
    dispatcher_ = std::move(disp);
    id_ = id;
    StreamBase::Init(phys_mem, std::move(interrupt));
  }

  void DoRequest(bool read_only) {
    TRACE_DURATION("machina", "RequestStream::DoRequest");
    while (queue_.NextChain(&chain_)) {
      auto request = fbl::MakeRefCounted<Request>(std::move(chain_));
      if (!request->NextDescriptor(&desc_, false /* writable */) ||
          desc_.len != sizeof(virtio_blk_req_t)) {
        DoError(std::move(request), VIRTIO_BLK_S_IOERR);
        continue;
      }
      const auto header = static_cast<virtio_blk_req_t*>(desc_.addr);
      // Virtio 1.0, Section 5.2.5.2: If the VIRTIO_BLK_F_BLK_SIZE feature is
      // negotiated, blk_size can be read to determine the optimal sector size
      // for the driver to use. This does not affect the units used in the
      // protocol (always 512 bytes), but awareness of the correct value can
      // affect performance.
      uint64_t off = header->sector * kBlockSectorSize;
      switch (header->type) {
        case VIRTIO_BLK_T_IN:
          DoRead(std::move(request), off);
          break;
        case VIRTIO_BLK_T_OUT:
          // Virtio 1.0, Section 5.2.6.2: A device MUST set the status byte to
          // VIRTIO_BLK_S_IOERR for a write request if the VIRTIO_BLK_F_RO
          // feature if offered, and MUST NOT write any data.
          if (read_only) {
            DoError(std::move(request), VIRTIO_BLK_S_IOERR);
          } else {
            DoWrite(std::move(request), off);
          }
          break;
        case VIRTIO_BLK_T_FLUSH:
          // Virtio 1.0, Section 5.2.6.1: A driver MUST set sector to 0 for a
          // VIRTIO_BLK_T_FLUSH request. A driver SHOULD NOT include any data in
          // a VIRTIO_BLK_T_FLUSH request.
          if (header->sector != 0) {
            DoError(std::move(request), VIRTIO_BLK_S_IOERR);
          } else {
            DoSync(std::move(request));
          }
          break;
        case VIRTIO_BLK_T_GET_ID:
          DoId(std::move(request));
          break;
        default:
          DoError(std::move(request), VIRTIO_BLK_S_UNSUPP);
          break;
      }
    }
  }

 private:
  std::unique_ptr<BlockDispatcher> dispatcher_;
  std::string id_;

  void DoRead(fbl::RefPtr<Request> request, uint64_t off) {
    TRACE_DURATION("machina", "RequestStream::DoRead");
    while (request->NextDescriptor(&desc_, true /* writable */)) {
      const uint32_t size = desc_.len;
      if (size % kBlockSectorSize != 0) {
        request->SetStatus(VIRTIO_BLK_S_IOERR);
        continue;
      }
      const trace_async_id_t nonce = TRACE_NONCE();
      auto callback = [request, nonce, size](zx_status_t status) {
        TRACE_DURATION("machina", "RequestStream::DoRead Callback");
        if (status != ZX_OK) {
          request->SetStatus(VIRTIO_BLK_S_IOERR);
        }
        request->AddUsed(size);
        TRACE_FLOW_END("machina", "block:read-at", nonce);
      };
      TRACE_FLOW_BEGIN("machina", "block:read-at", nonce, "size", size, "off", off);
      dispatcher_->ReadAt(desc_.addr, size, off, callback);
      off += size;
    }
  }

  void DoWrite(fbl::RefPtr<Request> request, uint64_t off) {
    TRACE_DURATION("machina", "RequestStream::DoWrite");
    while (request->NextDescriptor(&desc_, false /* writable */)) {
      const uint32_t size = desc_.len;
      if (size % kBlockSectorSize != 0) {
        request->SetStatus(VIRTIO_BLK_S_IOERR);
        continue;
      }
      const trace_async_id_t nonce = TRACE_NONCE();
      auto callback = [request, nonce](zx_status_t status) {
        TRACE_DURATION("machina", "RequestStream::DoWrite Callback");
        if (status != ZX_OK) {
          request->SetStatus(VIRTIO_BLK_S_IOERR);
        }
        TRACE_FLOW_END("machina", "block:write-at", nonce);
      };
      TRACE_FLOW_BEGIN("machina", "block:write-at", nonce, "size", size, "off", off);
      dispatcher_->WriteAt(desc_.addr, size, off, callback);
      off += size;
    }
  }

  void DoSync(fbl::RefPtr<Request> request) {
    TRACE_DURATION("machina", "RequestStream::DoSync");
    const trace_async_id_t nonce = TRACE_NONCE();
    auto callback = [request, nonce](zx_status_t status) {
      TRACE_DURATION("machina", "RequestStream::DoSync Callback");
      if (status != ZX_OK) {
        request->SetStatus(VIRTIO_BLK_S_IOERR);
      }
      TRACE_FLOW_END("machina", "block:sync", nonce);
    };
    TRACE_FLOW_BEGIN("machina", "block:sync", nonce);
    dispatcher_->Sync(callback);
    while (request->NextDescriptor(&desc_, false /* writable */)) {
    }
  }

  void DoId(fbl::RefPtr<Request> request) {
    TRACE_DURATION("machina", "RequestStream::DoId");
    while (request->NextDescriptor(&desc_, true /* writable */)) {
      if (desc_.len != VIRTIO_BLK_ID_BYTES) {
        request->SetStatus(VIRTIO_BLK_S_IOERR);
        continue;
      }
      TRACE_DURATION("machina", "block:id");
      auto size = std::min<uint32_t>(id_.size() + 1, desc_.len);
      memcpy(desc_.addr, id_.c_str(), size);
      request->AddUsed(size);
    }
  }

  void DoError(fbl::RefPtr<Request> request, uint8_t status) {
    TRACE_DURATION("machina", "RequestStream::DoError");
    request->SetStatus(status);
    while (request->NextDescriptor(&desc_, false /* writable */)) {
    }
  }
};

// Implementation of a virtio-block device.
class VirtioBlockImpl : public DeviceBase<VirtioBlockImpl>,
                        public fuchsia::virtualization::hardware::VirtioBlock {
 public:
  VirtioBlockImpl(sys::ComponentContext* context) : DeviceBase(context) {}

  // |fuchsia::virtualization::hardware::VirtioDevice|
  void NotifyQueue(uint16_t queue) override {
    TRACE_DURATION("machina", "VirtioBlockImpl::NotifyQueue");
    switch (static_cast<Queue>(queue)) {
      case Queue::REQUEST:
        request_stream_.DoRequest(read_only_);
        break;
      default:
        FX_CHECK(false) << "Queue index " << queue << " out of range";
        __UNREACHABLE;
    }
  }

 private:
  // |fuchsia::virtualization::hardware::VirtioBlock|
  void Start(fuchsia::virtualization::hardware::StartInfo start_info, std::string id,
             fuchsia::virtualization::BlockMode mode, fuchsia::virtualization::BlockFormat format,
             fidl::InterfaceHandle<fuchsia::io::File> file, StartCallback callback) override {
    read_only_ = mode == fuchsia::virtualization::BlockMode::READ_ONLY;
    PrepStart(std::move(start_info));

    NestedBlockDispatcherCallback nested = [this, id = std::move(id),
                                            callback = std::move(callback)](
                                               size_t size, std::unique_ptr<BlockDispatcher> disp) {
      request_stream_.Init(
          std::move(disp), id, phys_mem_,
          fit::bind_member<zx_status_t, DeviceBase>(this, &VirtioBlockImpl::Interrupt));
      callback(size);
    };

    if (mode == fuchsia::virtualization::BlockMode::VOLATILE_WRITE) {
      nested = [nested = std::move(nested)](size_t size,
                                            std::unique_ptr<BlockDispatcher> disp) mutable {
        CreateVolatileWriteBlockDispatcher(size, std::move(disp), std::move(nested));
      };
    }

    if (format == fuchsia::virtualization::BlockFormat::QCOW) {
      nested = [nested = std::move(nested)](size_t size,
                                            std::unique_ptr<BlockDispatcher> disp) mutable {
        CreateQcowBlockDispatcher(std::move(disp), std::move(nested));
      };
    }

    uint32_t vmo_flags = fuchsia::io::VMO_FLAG_READ;
    if (mode == fuchsia::virtualization::BlockMode::READ_WRITE) {
      vmo_flags |= fuchsia::io::VMO_FLAG_WRITE;
    }
    CreateVmoBlockDispatcher(file.Bind(), vmo_flags, std::move(nested));
  }

  // |fuchsia::virtualization::hardware::VirtioDevice|
  void ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc, zx_gpaddr_t avail,
                      zx_gpaddr_t used, ConfigureQueueCallback callback) override {
    auto deferred = fit::defer(std::move(callback));
    switch (static_cast<Queue>(queue)) {
      case Queue::REQUEST:
        request_stream_.Configure(size, desc, avail, used);
        break;
      default:
        FX_CHECK(false) << "Queue index " << queue << " out of range";
        __UNREACHABLE;
    }
  }

  // |fuchsia::virtualization::hardware::VirtioDevice|
  void Ready(uint32_t negotiated_features, ReadyCallback callback) override { callback(); }

  bool read_only_;
  RequestStream request_stream_;
};

int main(int argc, char** argv) {
  syslog::InitLogger({"virtio_block"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());
  std::unique_ptr<sys::ComponentContext> context = sys::ComponentContext::Create();

  // The virtio-block device is single-threaded.
  VirtioBlockImpl virtio_block(context.get());
  return loop.Run();
}
