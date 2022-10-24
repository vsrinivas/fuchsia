// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "console.h"

#include <fidl/fuchsia.hardware.pty/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/zx/vmar.h>
#include <string.h>

#include <algorithm>
#include <memory>
#include <utility>

#include <ddktl/fidl.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <virtio/virtio.h>

namespace virtio {

namespace {

zx_status_t QueueTransfer(Ring* ring, uintptr_t phys, uint32_t len, bool write) {
  uint16_t index;
  vring_desc* desc = ring->AllocDescChain(1, &index);
  if (!desc) {
    // This should not happen
    zxlogf(ERROR, "Failed to find free descriptor for the virtio ring");
    return ZX_ERR_NO_MEMORY;
  }

  desc->addr = phys;
  desc->len = len;
  // writeable for the driver is readonly for the device and vice versa
  desc->flags = write ? 0 : VRING_DESC_F_WRITE;
  ring->SubmitChain(index);

  return ZX_OK;
}

}  // namespace

TransferBuffer::TransferBuffer() { memset(&buf_, 0, sizeof(buf_)); }

TransferBuffer::~TransferBuffer() { io_buffer_release(&buf_); }

zx_status_t TransferBuffer::Init(const zx::bti& bti, size_t count, uint32_t chunk_size) {
  if (!count)
    return ZX_OK;

  count_ = count;
  chunk_size_ = chunk_size;
  size_ = count * chunk_size;

  TransferDescriptor* descriptor = new TransferDescriptor[count_];
  if (!descriptor) {
    zxlogf(ERROR, "Failed to allocate transfer descriptors (%d)", ZX_ERR_NO_MEMORY);
    return ZX_ERR_NO_MEMORY;
  }

  descriptor_.reset(descriptor, count_);

  zx_status_t status = io_buffer_init(&buf_, bti.get(), size_, IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to allocate transfer buffers (%d)", status);
    return status;
  }

  void* virt = io_buffer_virt(&buf_);
  zx_paddr_t phys = io_buffer_phys(&buf_);
  for (size_t i = 0; i < count_; ++i) {
    TransferDescriptor& desc = descriptor_[i];

    desc.virt = reinterpret_cast<uint8_t*>(virt) + i * chunk_size;
    desc.phys = phys + i * chunk_size;
    desc.total_len = chunk_size;
    desc.used_len = 0;
    desc.processed_len = 0;
  }

  return ZX_OK;
}

TransferDescriptor* TransferBuffer::GetDescriptor(size_t index) {
  if (index > count_)
    return nullptr;
  return &descriptor_[index];
}

TransferDescriptor* TransferBuffer::PhysicalToDescriptor(uintptr_t phys) {
  zx_paddr_t base = io_buffer_phys(&buf_);
  if (phys < base || phys >= base + size_)
    return nullptr;
  return &descriptor_[(phys - base) / chunk_size_];
}

void TransferQueue::Add(TransferDescriptor* desc) { queue_.push_front(desc); }

TransferDescriptor* TransferQueue::Peek() {
  if (queue_.is_empty())
    return nullptr;
  return &queue_.back();
}

TransferDescriptor* TransferQueue::Dequeue() {
  if (queue_.is_empty())
    return nullptr;
  return queue_.pop_back();
}

bool TransferQueue::IsEmpty() const { return queue_.is_empty(); }

ConsoleDevice::ConsoleDevice(zx_device_t* bus_device, zx::bti bti, std::unique_ptr<Backend> backend)
    : virtio::Device(bus_device, std::move(bti), std::move(backend)), DeviceType(bus_device) {}

ConsoleDevice::~ConsoleDevice() = default;

// We don't need to hold request_lock_ during initialization
zx_status_t ConsoleDevice::Init() TA_NO_THREAD_SAFETY_ANALYSIS {
  zxlogf(TRACE, "%s: entry", __func__);

  if (zx_status_t status = zx::eventpair::create(0, &event_, &event_remote_); status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to create event pair (%d)", tag(), status);
    return status;
  }

  // It's a common part for all virtio devices: reset the device, notify
  // about the driver and negotiate supported features
  DeviceReset();
  DriverStatusAck();
  if (!DeviceFeatureSupported(VIRTIO_F_VERSION_1)) {
    zxlogf(ERROR, "%s: Legacy virtio interface is not supported by this driver", tag());
    return ZX_ERR_NOT_SUPPORTED;
  }
  DriverFeatureAck(VIRTIO_F_VERSION_1);

  if (zx_status_t status = DeviceStatusFeaturesOk(); status != ZX_OK) {
    zxlogf(ERROR, "%s: Feature negotiation failed (%d)", tag(), status);
    return status;
  }

  if (zx_status_t status = port0_receive_queue_.Init(0, kDescriptors); status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to initialize receive queue (%d)", tag(), status);
    return status;
  }

  if (zx_status_t status = port0_receive_buffer_.Init(bti_, kDescriptors, kChunkSize);
      status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to allocate buffers for receive queue (%d)", tag(), status);
    return status;
  }

  // Initially the whole receive buffer is available for device to write, so
  // put all descriptors in the virtio ring available list
  for (size_t i = 0; i < kDescriptors; ++i) {
    TransferDescriptor* desc = port0_receive_buffer_.GetDescriptor(i);
    QueueTransfer(&port0_receive_queue_, desc->phys, desc->total_len, /*write*/ false);
  }
  // Notify the device
  port0_receive_queue_.Kick();

  if (zx_status_t status = port0_transmit_queue_.Init(1, kDescriptors); status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to initialize transmit queue (%d)", tag(), status);
    return status;
  }

  if (zx_status_t status = port0_transmit_buffer_.Init(bti_, kDescriptors, kChunkSize);
      status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to allocate buffers for transmit queue (%d)", tag(), status);
    return status;
  }

  // Initially the whole transmit buffer available for writing, so put all the
  // descriptors in the queue
  for (size_t i = 0; i < kDescriptors; ++i) {
    TransferDescriptor* desc = port0_transmit_buffer_.GetDescriptor(i);
    port0_transmit_descriptors_.Add(desc);
  }

  {
    zx_status_t status = DdkAdd("virtio-console");
    device_ = zxdev();
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to register device (%d)", tag(), status);
      device_ = nullptr;
      return status;
    }
  }

  StartIrqThread();
  DriverStatusOk();

  zxlogf(TRACE, "%s: exit", __func__);
  return ZX_OK;
}

void ConsoleDevice::Unbind(ddk::UnbindTxn txn) {
  unbind_txn_ = std::move(txn);

  for (auto& [handle, binding] : bindings_) {
    binding.Unbind();
  }
}

void ConsoleDevice::IrqRingUpdate() {
  zxlogf(TRACE, "%s: entry", __func__);

  fbl::AutoLock a(&request_lock_);

  // These callbacks are called synchronously, so we don't need to acquire request_lock_
  port0_receive_queue_.IrqRingUpdate([this](vring_used_elem* elem) TA_NO_THREAD_SAFETY_ANALYSIS {
    uint16_t index = static_cast<uint16_t>(elem->id);
    vring_desc* desc = port0_receive_queue_.DescFromIndex(index);
    uint32_t remain = elem->len;

    for (;;) {
      bool has_next = desc->flags & VRING_DESC_F_NEXT;
      uint16_t next = desc->next;

      TransferDescriptor* trans = port0_receive_buffer_.PhysicalToDescriptor(desc->addr);

      trans->processed_len = 0;
      trans->used_len = std::min(trans->total_len, remain);
      remain -= trans->used_len;
      port0_receive_descriptors_.Add(trans);

      port0_receive_queue_.FreeDesc(index);
      if (!has_next)
        break;

      index = next;
      desc = port0_receive_queue_.DescFromIndex(index);
    }
    event_.signal_peer(0, DEV_STATE_READABLE);
  });

  port0_transmit_queue_.IrqRingUpdate([this](vring_used_elem* elem) TA_NO_THREAD_SAFETY_ANALYSIS {
    uint16_t index = static_cast<uint16_t>(elem->id);
    vring_desc* desc = port0_transmit_queue_.DescFromIndex(index);

    for (;;) {
      bool has_next = desc->flags & VRING_DESC_F_NEXT;
      uint16_t next = desc->next;

      TransferDescriptor* trans = port0_transmit_buffer_.PhysicalToDescriptor(desc->addr);

      port0_transmit_descriptors_.Add(trans);

      port0_transmit_queue_.FreeDesc(index);
      if (!has_next)
        break;

      index = next;
      desc = port0_transmit_queue_.DescFromIndex(index);
    }
    event_.signal_peer(0, DEV_STATE_WRITABLE);
  });
  zxlogf(TRACE, "%s: exit", __func__);
}

void ConsoleDevice::AddConnection(fidl::ServerEnd<fuchsia_hardware_pty::Device> server_end) {
  const zx_handle_t key = server_end.channel().get();
  auto [it, inserted] = bindings_.insert(
      {key, fidl::BindServer(
                fdf::Dispatcher::GetCurrent()->async_dispatcher(), std::move(server_end),
                static_cast<fidl::WireServer<fuchsia_hardware_pty::Device>*>(this),
                [](fidl::WireServer<fuchsia_hardware_pty::Device>* impl, fidl::UnbindInfo,
                   fidl::ServerEnd<fuchsia_hardware_pty::Device> key) {
                  ConsoleDevice& self = *static_cast<ConsoleDevice*>(impl);
                  size_t erased = self.bindings_.erase(key.channel().get());
                  ZX_ASSERT_MSG(erased == 1, "erased=%zu", erased);
                  if (self.bindings_.empty()) {
                    if (std::optional txn = std::exchange(self.unbind_txn_, {}); txn.has_value()) {
                      txn.value().Reply();
                    }
                  }
                })});
  ZX_ASSERT_MSG(inserted, "handle=%d", key);
}

void ConsoleDevice::Clone2(Clone2RequestView request, Clone2Completer::Sync& completer) {
  AddConnection(fidl::ServerEnd<fuchsia_hardware_pty::Device>(request->request.TakeChannel()));
}

void ConsoleDevice::Close(CloseCompleter::Sync& completer) {
  completer.ReplySuccess();
  completer.Close(ZX_OK);
}

void ConsoleDevice::Query(QueryCompleter::Sync& completer) {
  const std::string_view kProtocol = fuchsia_hardware_pty::wire::kDeviceProtocolName;
  uint8_t* data = reinterpret_cast<uint8_t*>(const_cast<char*>(kProtocol.data()));
  completer.Reply(fidl::VectorView<uint8_t>::FromExternal(data, kProtocol.size()));
}

void ConsoleDevice::Read(ReadRequestView request, ReadCompleter::Sync& completer) {
  zxlogf(TRACE, "%s: entry", __func__);

  fbl::AutoLock a(&request_lock_);

  TransferDescriptor* desc = port0_receive_descriptors_.Peek();
  if (!desc) {
    event_.signal_peer(DEV_STATE_READABLE, 0);
    return completer.ReplyError(ZX_ERR_SHOULD_WAIT);
  }

  uint8_t buf[fuchsia_io::wire::kMaxBuf];
  uint32_t len = static_cast<uint32_t>(std::min(std::initializer_list<uint64_t>{
      std::numeric_limits<uint32_t>::max(),
      sizeof(buf),
      request->count,
      desc->used_len - desc->processed_len,
  }));
  memcpy(buf, desc->virt + desc->processed_len, len);
  desc->processed_len += len;

  // Did we read the whole buffer? If so return it back to the device
  if (desc->processed_len == desc->used_len) {
    port0_receive_descriptors_.Dequeue();
    QueueTransfer(&port0_receive_queue_, desc->phys, desc->total_len, /*write*/ false);
    port0_receive_queue_.Kick();
  }

  zxlogf(TRACE, "%s: exit", __func__);
  completer.ReplySuccess(fidl::VectorView<uint8_t>::FromExternal(buf, len));
}

void ConsoleDevice::Write(WriteRequestView request, WriteCompleter::Sync& completer) {
  zxlogf(TRACE, "%s: entry", __func__);

  uint64_t count = request->data.count();
  if (count > UINT32_MAX)
    count = UINT32_MAX;

  fbl::AutoLock a(&request_lock_);

  TransferDescriptor* desc = port0_transmit_descriptors_.Dequeue();
  if (!desc) {
    event_.signal_peer(DEV_STATE_WRITABLE, 0);
    return completer.ReplyError(ZX_ERR_SHOULD_WAIT);
  }

  uint32_t len = std::min(static_cast<uint32_t>(count), desc->total_len);
  memcpy(desc->virt, request->data.data(), len);
  desc->used_len = len;

  QueueTransfer(&port0_transmit_queue_, desc->phys, desc->used_len, /*write*/ true);
  port0_transmit_queue_.Kick();

  zxlogf(TRACE, "%s: exit", __func__);
  completer.ReplySuccess(len);
}

void ConsoleDevice::Describe(DescribeCompleter::Sync& completer) {
  zx::eventpair event;
  if (zx_status_t status = event_remote_.duplicate(ZX_RIGHT_SAME_RIGHTS, &event); status != ZX_OK) {
    completer.Close(status);
  } else {
    fidl::Arena alloc;
    completer.Reply(fuchsia_hardware_pty::wire::DeviceDescribeResponse::Builder(alloc)
                        .event(std::move(event))
                        .Build());
  }
}

void ConsoleDevice::OpenClient(OpenClientRequestView request,
                               OpenClientCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED);
}
void ConsoleDevice::ClrSetFeature(ClrSetFeatureRequestView request,
                                  ClrSetFeatureCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, {});
}
void ConsoleDevice::GetWindowSize(GetWindowSizeCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, {});
}
void ConsoleDevice::MakeActive(MakeActiveRequestView request,
                               MakeActiveCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED);
}
void ConsoleDevice::ReadEvents(ReadEventsCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, {});
}
void ConsoleDevice::SetWindowSize(SetWindowSizeRequestView request,
                                  SetWindowSizeCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED);
}

}  // namespace virtio
