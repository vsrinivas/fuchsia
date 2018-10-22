// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/ref_counted.h>
#include <lib/async-loop/cpp/loop.h>
#include <trace-provider/provider.h>
#include <virtio/block.h>

#include "garnet/bin/guest/vmm/device/block_dispatcher.h"
#include "garnet/bin/guest/vmm/device/device_base.h"
#include "garnet/bin/guest/vmm/device/stream_base.h"
#include "garnet/lib/machina/device/block.h"

enum class Queue : uint16_t {
  REQUEST = 0,
};

// A single asynchronous block request.
class Request : public fbl::RefCounted<Request> {
 public:
  Request(machina::VirtioChain chain) : chain_(std::move(chain)) {}

  ~Request() {
    if (status_ptr_ != nullptr) {
      *status_ptr_ = status_;
    }
    chain_.Return();
  }

  bool NextDescriptor(machina::VirtioDescriptor* desc, bool writable) {
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
  machina::VirtioChain chain_;
  uint8_t status_ = VIRTIO_BLK_S_OK;
  uint8_t* status_ptr_ = nullptr;
};

// Stream for request queue.
class RequestStream : public StreamBase {
 public:
  void Init(std::unique_ptr<BlockDispatcher> disp,
            const machina::PhysMem& phys_mem,
            machina::VirtioQueue::InterruptFn interrupt) {
    dispatcher_ = std::move(disp);
    StreamBase::Init(phys_mem, std::move(interrupt));
  }

  void DoRequest(bool read_only) {
    while (queue_.NextChain(&chain_)) {
      auto request = fbl::MakeRefCounted<Request>(std::move(chain_));
      if (!request->NextDescriptor(&desc_, false /* writable */) ||
          desc_.len != sizeof(virtio_blk_req_t)) {
        DoError(std::move(request), VIRTIO_BLK_S_IOERR);
        continue;
      }
      auto header = static_cast<virtio_blk_req_t*>(desc_.addr);
      // Virtio 1.0, Section 5.2.5.2: If the VIRTIO_BLK_F_BLK_SIZE feature is
      // negotiated, blk_size can be read to determine the optimal sector size
      // for the driver to use. This does not affect the units used in the
      // protocol (always 512 bytes), but awareness of the correct value can
      // affect performance.
      uint64_t off = header->sector * machina::kBlockSectorSize;
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

  void DoRead(fbl::RefPtr<Request> request, uint64_t off) {
    while (request->NextDescriptor(&desc_, true /* writable */)) {
      if (desc_.len % machina::kBlockSectorSize != 0) {
        request->SetStatus(VIRTIO_BLK_S_IOERR);
        continue;
      }
      auto callback = [request, len = desc_.len](zx_status_t status) {
        if (status != ZX_OK) {
          request->SetStatus(VIRTIO_BLK_S_IOERR);
        }
        request->AddUsed(len);
      };
      dispatcher_->ReadAt(desc_.addr, desc_.len, off, callback);
      off += desc_.len;
    }
  }

  void DoWrite(fbl::RefPtr<Request> request, uint64_t off) {
    while (request->NextDescriptor(&desc_, false /* writable */)) {
      if (desc_.len % machina::kBlockSectorSize != 0) {
        request->SetStatus(VIRTIO_BLK_S_IOERR);
        continue;
      }
      auto callback = [request](zx_status_t status) {
        if (status != ZX_OK) {
          request->SetStatus(VIRTIO_BLK_S_IOERR);
        }
      };
      dispatcher_->WriteAt(desc_.addr, desc_.len, off, callback);
      off += desc_.len;
    }
  }

  void DoSync(fbl::RefPtr<Request> request) {
    auto callback = [request](zx_status_t status) {
      if (status != ZX_OK) {
        request->SetStatus(VIRTIO_BLK_S_IOERR);
      }
    };
    dispatcher_->Sync(callback);
    while (request->NextDescriptor(&desc_, false /* writable */)) {
    }
  }

  void DoId(fbl::RefPtr<Request> request) {
    while (request->NextDescriptor(&desc_, true /* writable */)) {
      if (desc_.len != VIRTIO_BLK_ID_BYTES) {
        request->SetStatus(VIRTIO_BLK_S_IOERR);
        continue;
      }
      auto len = std::min<uint32_t>(sizeof(machina::kBlockId), desc_.len);
      memcpy(desc_.addr, machina::kBlockId, len);
      request->AddUsed(len);
    }
  }

  void DoError(fbl::RefPtr<Request> request, uint8_t status) {
    request->SetStatus(status);
    while (request->NextDescriptor(&desc_, false /* writable */)) {
    }
  }
};

// Implementation of a virtio-block device.
class VirtioBlockImpl : public DeviceBase<VirtioBlockImpl>,
                        public fuchsia::guest::device::VirtioBlock {
 public:
  VirtioBlockImpl(component::StartupContext* context) : DeviceBase(context) {}

  // |fuchsia::guest::device::VirtioDevice|
  void NotifyQueue(uint16_t queue) override {
    switch (static_cast<Queue>(queue)) {
      case Queue::REQUEST:
        request_stream_.DoRequest(negotiated_features_ & VIRTIO_BLK_F_RO);
        break;
      default:
        FXL_CHECK(false) << "Queue index " << queue << " out of range";
        __UNREACHABLE;
    }
  }

 private:
  // |fuchsia::guest::device::VirtioBlock|
  void Start(fuchsia::guest::device::StartInfo start_info,
             fuchsia::guest::device::BlockMode mode,
             fuchsia::guest::device::BlockFormat format,
             fidl::InterfaceHandle<fuchsia::io::File> file,
             StartCallback callback) override {
    PrepStart(std::move(start_info));

    NestedBlockDispatcherCallback nested =
        [this, callback = std::move(callback)](
            size_t size, std::unique_ptr<BlockDispatcher> disp) {
          request_stream_.Init(std::move(disp), phys_mem_,
                               fit::bind_member<zx_status_t, DeviceBase>(
                                   this, &VirtioBlockImpl::Interrupt));
          callback(size);
        };

    if (mode == fuchsia::guest::device::BlockMode::VOLATILE_WRITE) {
      nested = [nested = std::move(nested)](
                   size_t size, std::unique_ptr<BlockDispatcher> disp) mutable {
        CreateVolatileWriteBlockDispatcher(size, std::move(disp),
                                           std::move(nested));
      };
    }

    if (format == fuchsia::guest::device::BlockFormat::QCOW) {
      nested = [nested = std::move(nested)](
                   size_t size, std::unique_ptr<BlockDispatcher> disp) mutable {
        CreateQcowBlockDispatcher(std::move(disp), std::move(nested));
      };
    }

    CreateRawBlockDispatcher(file.Bind(), std::move(nested));
  }

  // |fuchsia::guest::device::VirtioDevice|
  void ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc,
                      zx_gpaddr_t avail, zx_gpaddr_t used) override {
    switch (static_cast<Queue>(queue)) {
      case Queue::REQUEST:
        request_stream_.Configure(size, desc, avail, used);
        break;
      default:
        FXL_CHECK(false) << "Queue index " << queue << " out of range";
        __UNREACHABLE;
    }
  }

  // |fuchsia::guest::device::VirtioDevice|
  void Ready(uint32_t negotiated_features) override {
    negotiated_features_ = negotiated_features;
  }

  uint32_t negotiated_features_;
  RequestStream request_stream_;
};

int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());
  std::unique_ptr<component::StartupContext> context =
      component::StartupContext::CreateFromStartupInfo();

  // The virtio-block device is single-threaded.
  VirtioBlockImpl virtio_block(context.get());
  return loop.Run();
}
