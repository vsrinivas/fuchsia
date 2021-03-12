// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_SERIAL_DRIVERS_VIRTIO_CONSOLE_CONSOLE_H_
#define SRC_DEVICES_SERIAL_DRIVERS_VIRTIO_CONSOLE_CONSOLE_H_

#include <fuchsia/hardware/virtioconsole/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/virtio/device.h>
#include <lib/virtio/ring.h>
#include <lib/zircon-internal/thread_annotations.h>

#include <memory>

#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/array.h>
#include <fbl/intrusive_double_list.h>

#include "src/lib/storage/vfs/cpp/managed_vfs.h"

namespace virtio {

// Describes a chunk of memory used for data transfers between the device and the driver,
// points to the memory inside TransferBuffer below
struct TransferDescriptor : fbl::DoublyLinkedListable<TransferDescriptor*> {
  uint8_t* virt;
  uintptr_t phys;
  uint32_t total_len;
  uint32_t used_len;
  uint32_t processed_len;
};

// Manages memory we use for transfers, TransferDescriptor points to the memory inside the class
class TransferBuffer {
 public:
  TransferBuffer();
  ~TransferBuffer();

  zx_status_t Init(const zx::bti& bti, size_t count, uint32_t chunk_size);

  TransferDescriptor* GetDescriptor(size_t index);
  TransferDescriptor* PhysicalToDescriptor(uintptr_t phys);

 private:
  size_t count_ = 0;
  size_t size_ = 0;
  uint32_t chunk_size_ = 0;

  io_buffer_t buf_;
  fbl::Array<TransferDescriptor> descriptor_;
};

// Just a list of descriptors
class TransferQueue {
 public:
  void Add(TransferDescriptor* desc);
  TransferDescriptor* Peek();
  TransferDescriptor* Dequeue();
  bool IsEmpty() const;

 private:
  fbl::DoublyLinkedList<TransferDescriptor*> queue_;
};

// Actual virtio console implementation
class ConsoleDevice : public Device,
                      public ddk::Device<ConsoleDevice, ddk::Messageable>,
                      public ddk::EmptyProtocol<ZX_PROTOCOL_CONSOLE>,
                      public fuchsia_hardware_virtioconsole::Device::RawChannelInterface {
 public:
  explicit ConsoleDevice(zx_device_t* device, zx::bti bti, std::unique_ptr<Backend> backend);
  ~ConsoleDevice() override;
  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn);
  void DdkRelease() { virtio::Device::Release(); }

  zx_status_t Init() override;
  void Unbind(ddk::UnbindTxn txn) override;

  void IrqRingUpdate() override;
  void IrqConfigChange() override {}  // No need to handle configuration changes
  const char* tag() const override { return "virtio-console"; }

  void GetChannel(zx::channel req, GetChannelCompleter::Sync& completer) override;
  zx_status_t GetEvent(zx::eventpair* event) {
    return event_remote_.duplicate(ZX_RIGHTS_BASIC, event);
  }

  zx_status_t Read(void* buf, size_t len, size_t* actual);
  zx_status_t Write(const void* buf, size_t len, size_t* actual);

 private:
  // For two queues it sums up to 32KiB, we probably don't need that much
  constexpr static size_t kDescriptors = 32;
  constexpr static uint32_t kChunkSize = 512;

  static zx_status_t virtio_console_read(void* ctx, void* buf, size_t len, zx_off_t off,
                                         size_t* actual);
  static zx_status_t virtio_console_write(void* ctx, const void* buf, size_t len, zx_off_t off,
                                          size_t* actual);

  fbl::Mutex request_lock_;

  TransferBuffer port0_receive_buffer_ TA_GUARDED(request_lock_);
  TransferQueue port0_receive_descriptors_ TA_GUARDED(request_lock_);
  Ring port0_receive_queue_ TA_GUARDED(request_lock_) = {this};

  TransferBuffer port0_transmit_buffer_ TA_GUARDED(request_lock_);
  TransferQueue port0_transmit_descriptors_ TA_GUARDED(request_lock_);
  Ring port0_transmit_queue_ TA_GUARDED(request_lock_) = {this};

  async::Loop loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
  fs::ManagedVfs vfs_{loop_.dispatcher()};
  fbl::RefPtr<fs::Vnode> console_vnode_;
  zx::eventpair event_, event_remote_;

  std::optional<ddk::UnbindTxn> unbind_txn_;
};

}  // namespace virtio

#endif  // SRC_DEVICES_SERIAL_DRIVERS_VIRTIO_CONSOLE_CONSOLE_H_
