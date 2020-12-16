// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/test/fake_msgbuf_interfaces.h"

#include <lib/fake-bti/bti.h>
#include <lib/zx/time.h>
#include <lib/zx/vmar.h>

#include <algorithm>
#include <cstring>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_buffer.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/msgbuf_structs.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/regs.h"

namespace wlan {
namespace brcmfmac {
namespace {

// Size of the permanent DMA buffer mapping we keep.  Note that we effectively map and never unmap
// every DmaBuffer we create, so this has to be reasonably sized to accomodate every DMA buffer
// created!
constexpr size_t kDmaBufferPermanentMappingSize = 32 * 1024 * 1024;

// The maximum number of flowrings, as exported by DmaConfig.
constexpr int kDmaConfigMaxFlowRings = 8;

// The offset of the index of the first flow ring.
constexpr int kDmaConfigFlowRingOffset = 32;

// The maximum number of each type of buffer, as exported by DmaConfig.
constexpr int kDmaConfigMaxBuffers = 8;

// The RX data offset value, for testing.
constexpr size_t kRxDataOffset = 0;

// Item size of each DMA ring.
constexpr size_t kControlSubmitRingItemSize = 40;
constexpr size_t kRxBufferSubmitRingItemSize = 32;
constexpr size_t kControlCompleteRingItemSize = 24;
constexpr size_t kTxCompleteRingItemSize = 16;
constexpr size_t kRxCompleteRingItemSize = 32;
constexpr size_t kFlowRingItemSize = 48;

// The item capacity of the DMA rings provided.
constexpr int kDmaRingCapacity = 64;

// Polling interval for interrupt generation.
constexpr zx::duration kInterruptPollingInterval = zx::usec(100);

// A DmaBuffer subclass that allows access to set the DMA address.
class FakeDmaBuffer : public DmaBuffer {
 public:
  void SetDmaAddress(zx_paddr_t dma_address);
};

void FakeDmaBuffer::SetDmaAddress(zx_paddr_t dma_address) { dma_address_ = dma_address; }

}  // namespace

FakeMsgbufInterfaces::FakeMsgbufInterfaces() = default;

FakeMsgbufInterfaces::~FakeMsgbufInterfaces() {
  thread_exit_flag_.store(true);
  if (interrupt_thread_.joinable()) {
    interrupt_thread_.join();
  }
  if (vmar_.is_valid()) {
    vmar_.destroy();
  }
}

// static
zx_status_t FakeMsgbufInterfaces::CreateSubmitRing(DmaBufferProviderInterface* provider,
                                                   size_t item_size, int capacity,
                                                   std::atomic<uint32_t>* write_signal,
                                                   std::unique_ptr<SubmitRing>* out_ring) {
  zx_status_t status = ZX_OK;

  std::unique_ptr<DmaBuffer> dma_buffer;
  if ((status = provider->CreateDmaBuffer(ZX_CACHE_POLICY_CACHED, item_size * capacity,
                                          &dma_buffer)) != ZX_OK) {
    return status;
  }
  auto ring = std::make_unique<SubmitRing>();
  if ((status = WriteDmaRing::Create(std::move(dma_buffer), item_size, capacity, &ring->read_index,
                                     &ring->write_index, write_signal, &ring->ring)) != ZX_OK) {
    return status;
  }

  *out_ring = std::move(ring);
  return ZX_OK;
}

// static
zx_status_t FakeMsgbufInterfaces::CreateCompleteRing(DmaBufferProviderInterface* provider,
                                                     size_t item_size, int capacity,
                                                     std::unique_ptr<CompleteRing>* out_ring) {
  zx_status_t status = ZX_OK;

  std::unique_ptr<DmaBuffer> dma_buffer;
  if ((status = provider->CreateDmaBuffer(ZX_CACHE_POLICY_CACHED, item_size * capacity,
                                          &dma_buffer)) != ZX_OK) {
    return status;
  }
  auto ring = std::make_unique<CompleteRing>();
  if ((status = ReadDmaRing::Create(std::move(dma_buffer), item_size, capacity, &ring->read_index,
                                    &ring->write_index, &ring->ring)) != ZX_OK) {
    return status;
  }

  *out_ring = std::move(ring);
  return ZX_OK;
}

// static
zx_status_t FakeMsgbufInterfaces::Create(std::unique_ptr<FakeMsgbufInterfaces>* out_interfaces) {
  zx_status_t status = ZX_OK;

  std::unique_ptr<FakeMsgbufInterfaces> interfaces(new FakeMsgbufInterfaces());

  zx::bti bti;
  if ((status = fake_bti_create(bti.reset_and_get_address())) != ZX_OK) {
    return status;
  }
  interfaces->bti_ = std::move(bti);

  zx::vmar vmar;
  zx_vaddr_t vmar_address = 0;
  if ((status = zx::vmar::root_self()->allocate2(ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0,
                                                 kDmaBufferPermanentMappingSize, &vmar,
                                                 &vmar_address)) != ZX_OK) {
    return status;
  }
  interfaces->vmar_ = std::move(vmar);

  interfaces->dma_config_.max_flow_rings = kDmaConfigMaxFlowRings;
  interfaces->dma_config_.flow_ring_offset = kDmaConfigFlowRingOffset;
  interfaces->dma_config_.max_ioctl_rx_buffers = kDmaConfigMaxBuffers;
  interfaces->dma_config_.max_event_rx_buffers = kDmaConfigMaxBuffers;
  interfaces->dma_config_.max_rx_buffers = kDmaConfigMaxBuffers;
  interfaces->dma_config_.rx_data_offset = kRxDataOffset;

  if ((status = CreateSubmitRing(interfaces.get(), kControlSubmitRingItemSize, kDmaRingCapacity,
                                 &interfaces->submit_ring_write_signal_,
                                 &interfaces->control_submit_ring_)) != ZX_OK) {
    return status;
  }
  if ((status = CreateSubmitRing(interfaces.get(), kRxBufferSubmitRingItemSize, kDmaRingCapacity,
                                 &interfaces->submit_ring_write_signal_,
                                 &interfaces->rx_buffer_submit_ring_)) != ZX_OK) {
    return status;
  }
  if ((status = CreateCompleteRing(interfaces.get(), kControlCompleteRingItemSize, kDmaRingCapacity,
                                   &interfaces->control_complete_ring_)) != ZX_OK) {
    return status;
  }
  if ((status = CreateCompleteRing(interfaces.get(), kTxCompleteRingItemSize, kDmaRingCapacity,
                                   &interfaces->tx_complete_ring_)) != ZX_OK) {
    return status;
  }
  if ((status = CreateCompleteRing(interfaces.get(), kRxCompleteRingItemSize, kDmaRingCapacity,
                                   &interfaces->rx_complete_ring_)) != ZX_OK) {
    return status;
  }

  interfaces->flow_rings_ = std::vector<FakeMsgbufInterfaces::FlowRing>(kDmaConfigMaxFlowRings);
  interfaces->flow_ring_callbacks_.resize(interfaces->flow_rings_.size());

  interfaces->interrupt_thread_ =
      std::thread(&FakeMsgbufInterfaces::InterruptThread, interfaces.get());
  *out_interfaces = std::move(interfaces);
  return ZX_OK;
}

zx_status_t FakeMsgbufInterfaces::CreateDmaBuffer(uint32_t cache_policy, size_t size,
                                                  std::unique_ptr<DmaBuffer>* out_dma_buffer) {
  zx_status_t status = ZX_OK;

  std::unique_ptr<DmaBuffer> dma_buffer;
  if ((status = DmaBuffer::Create(&bti_, cache_policy, size, &dma_buffer)) != ZX_OK) {
    return status;
  }

  std::lock_guard lock(dma_mutex_);
  // This is a gross hack, but: fake-bti assigns the same zx_paddr_t to all its pinned pages.  We
  // work around that by manually setting the dma_address of the buffers we create for testing.
  static_cast<FakeDmaBuffer*>(dma_buffer.get())->SetDmaAddress(next_dma_address_);
  next_dma_address_ += dma_buffer->size();

  auto iter = dma_buffer_permanent_mapping_.find(dma_buffer->dma_address());
  if (iter != dma_buffer_permanent_mapping_.end()) {
    return ZX_ERR_ALREADY_EXISTS;
  }

  uintptr_t address = 0;
  if ((status = dma_buffer->Map(vmar_, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &address)) != ZX_OK) {
    return status;
  }
  dma_buffer_permanent_mapping_.emplace_hint(iter, dma_buffer->dma_address(),
                                             std::make_pair(dma_buffer->size(), address));

  *out_dma_buffer = std::move(dma_buffer);
  return ZX_OK;
}

const DmaRingProviderInterface::DmaConfig& FakeMsgbufInterfaces::GetDmaConfig() const {
  return dma_config_;
}

WriteDmaRing* FakeMsgbufInterfaces::GetControlSubmitRing() {
  return control_submit_ring_->ring.get();
}

WriteDmaRing* FakeMsgbufInterfaces::GetRxBufferSubmitRing() {
  return rx_buffer_submit_ring_->ring.get();
}

ReadDmaRing* FakeMsgbufInterfaces::GetControlCompleteRing() {
  return control_complete_ring_->ring.get();
}

zx_status_t FakeMsgbufInterfaces::CreateFlowRing(int flow_ring_index,
                                                 std::unique_ptr<WriteDmaRing>* out_flow_ring) {
  zx_status_t status = ZX_OK;
  if (static_cast<size_t>(flow_ring_index) >= flow_rings_.size()) {
    return ZX_ERR_INVALID_ARGS;
  }

  std::unique_ptr<DmaBuffer> dma_buffer;
  if ((status = CreateDmaBuffer(ZX_CACHE_POLICY_CACHED, kFlowRingItemSize * kDmaRingCapacity,
                                &dma_buffer)) != ZX_OK) {
    return status;
  }

  std::unique_ptr<WriteDmaRing> flow_ring;
  if ((status = WriteDmaRing::Create(std::move(dma_buffer), kFlowRingItemSize, kDmaRingCapacity,
                                     &flow_rings_[flow_ring_index].read_index,
                                     &flow_rings_[flow_ring_index].write_index,
                                     &submit_ring_write_signal_, &flow_ring)) != ZX_OK) {
    return status;
  }
  flow_rings_[flow_ring_index].dma_address = flow_ring->dma_address();

  *out_flow_ring = std::move(flow_ring);
  return ZX_OK;
}

ReadDmaRing* FakeMsgbufInterfaces::GetTxCompleteRing() { return tx_complete_ring_->ring.get(); }

ReadDmaRing* FakeMsgbufInterfaces::GetRxCompleteRing() { return rx_complete_ring_->ring.get(); }

zx_status_t FakeMsgbufInterfaces::AddInterruptHandler(InterruptHandler* handler) {
  std::lock_guard lock(interrupt_mutex_);
  if (std::find(interrupt_handlers_.begin(), interrupt_handlers_.end(), handler) !=
      interrupt_handlers_.end()) {
    return ZX_ERR_ALREADY_EXISTS;
  }
  interrupt_handlers_.emplace_back(handler);
  return ZX_OK;
}

zx_status_t FakeMsgbufInterfaces::RemoveInterruptHandler(InterruptHandler* handler) {
  std::lock_guard lock(interrupt_mutex_);
  auto iter = std::find(interrupt_handlers_.begin(), interrupt_handlers_.end(), handler);
  if (iter == interrupt_handlers_.end()) {
    return ZX_ERR_NOT_FOUND;
  }
  interrupt_handlers_.erase(iter);
  return ZX_OK;
}

uintptr_t FakeMsgbufInterfaces::GetDmaBufferAddress(zx_paddr_t dma_address) {
  std::lock_guard lock(dma_mutex_);
  auto iter = dma_buffer_permanent_mapping_.upper_bound(dma_address);
  if (iter == dma_buffer_permanent_mapping_.begin()) {
    return 0;
  }
  --iter;

  const zx_paddr_t buffer_dma_address = iter->first;
  const size_t buffer_size = iter->second.first;
  const uintptr_t buffer_address = iter->second.second;

  if ((dma_address - buffer_dma_address) >= buffer_size) {
    return 0;
  }
  return buffer_address + (dma_address - buffer_dma_address);
}

void FakeMsgbufInterfaces::AddControlSubmitRingCallback(
    std::function<void(const void* buffer, size_t size)> callback) {
  std::lock_guard lock(interrupt_mutex_);
  control_submit_ring_callbacks_.emplace_back(std::move(callback));
}

void FakeMsgbufInterfaces::AddRxBufferSubmitRingCallback(
    std::function<void(const void* buffer, size_t size)> callback) {
  std::lock_guard lock(interrupt_mutex_);
  rx_buffer_submit_ring_callbacks_.emplace_back(std::move(callback));
}

void FakeMsgbufInterfaces::AddFlowRingCallback(
    int flow_ring_index, std::function<void(const void* buffer, size_t size)> callback) {
  std::lock_guard lock(interrupt_mutex_);
  flow_ring_callbacks_[flow_ring_index].emplace_back(std::move(callback));
}

zx_status_t FakeMsgbufInterfaces::AddControlCompleteRingEntry(const void* buffer, size_t size) {
  return AddCompleteRingEntry(control_complete_ring_.get(), buffer, size);
}

zx_status_t FakeMsgbufInterfaces::AddTxCompleteRingEntry(const void* buffer, size_t size) {
  return AddCompleteRingEntry(tx_complete_ring_.get(), buffer, size);
}

zx_status_t FakeMsgbufInterfaces::AddRxCompleteRingEntry(const void* buffer, size_t size) {
  return AddCompleteRingEntry(rx_complete_ring_.get(), buffer, size);
}

FakeMsgbufInterfaces::DmaPoolBuffer FakeMsgbufInterfaces::GetIoctlRxBuffer() {
  std::unique_lock lock(rx_buffers_mutex_);
  while (ioctl_rx_buffers_.empty()) {
    rx_buffers_condvar_.wait(lock);
  }
  auto buffer = ioctl_rx_buffers_.front();
  ioctl_rx_buffers_.pop_front();
  return buffer;
}

FakeMsgbufInterfaces::DmaPoolBuffer FakeMsgbufInterfaces::GetEventRxBuffer() {
  std::unique_lock lock(rx_buffers_mutex_);
  while (event_rx_buffers_.empty()) {
    rx_buffers_condvar_.wait(lock);
  }
  auto buffer = event_rx_buffers_.front();
  event_rx_buffers_.pop_front();
  return buffer;
}

FakeMsgbufInterfaces::DmaPoolBuffer FakeMsgbufInterfaces::GetRxBuffer() {
  std::unique_lock lock(rx_buffers_mutex_);
  while (rx_buffers_.empty()) {
    rx_buffers_condvar_.wait(lock);
  }
  auto buffer = rx_buffers_.front();
  rx_buffers_.pop_front();
  return buffer;
}

void FakeMsgbufInterfaces::InterruptThread() {
  while (true) {
    if (thread_exit_flag_.load()) {
      return;
    }

    bool did_work = false;

    if (submit_ring_write_signal_.exchange(0)) {
      did_work = true;
      uint16_t read_index = control_submit_ring_->read_index.load();
      while (read_index != control_submit_ring_->write_index.load()) {
        const size_t read_size = control_submit_ring_->ring->item_size();
        const void* const read_address = reinterpret_cast<const void*>(
            GetDmaBufferAddress(control_submit_ring_->ring->dma_address()) +
            read_index * read_size);
        read_index = (read_index + 1) % control_submit_ring_->ring->capacity();

        if (ProcessControlSubmitRingEntry(read_address, read_size)) {
          continue;
        }
        std::function<void(const void* buffer, size_t size)> callback;
        {
          std::lock_guard lock(interrupt_mutex_);
          if (control_submit_ring_callbacks_.empty()) {
            continue;
          }
          callback = std::move(control_submit_ring_callbacks_.front());
          control_submit_ring_callbacks_.pop_front();
        }
        callback(read_address, read_size);
      }
      control_submit_ring_->read_index.store(read_index);

      read_index = rx_buffer_submit_ring_->read_index.load();
      while (read_index != rx_buffer_submit_ring_->write_index.load()) {
        const size_t read_size = rx_buffer_submit_ring_->ring->item_size();
        const void* const read_address = reinterpret_cast<const void*>(
            GetDmaBufferAddress(rx_buffer_submit_ring_->ring->dma_address()) +
            read_index * read_size);
        read_index = (read_index + 1) % rx_buffer_submit_ring_->ring->capacity();

        if (ProcessRxBufferSubmitRingEntry(read_address, read_size)) {
          continue;
        }
        std::function<void(const void* buffer, size_t size)> callback;
        {
          std::lock_guard lock(interrupt_mutex_);
          if (rx_buffer_submit_ring_callbacks_.empty()) {
            continue;
          }
          callback = std::move(rx_buffer_submit_ring_callbacks_.front());
          rx_buffer_submit_ring_callbacks_.pop_front();
        }
        callback(read_address, read_size);
      }
      rx_buffer_submit_ring_->read_index.store(read_index);

      for (size_t i = 0; i < flow_rings_.size(); ++i) {
        FlowRing& flow_ring = flow_rings_[i];
        read_index = flow_ring.read_index.load();
        while (read_index != flow_ring.write_index.load()) {
          const size_t read_size = kFlowRingItemSize;
          const void* const read_address = reinterpret_cast<const void*>(
              GetDmaBufferAddress(flow_ring.dma_address) + read_index * read_size);
          read_index = (read_index + 1) % kDmaRingCapacity;

          if (ProcessFlowRingEntry(i, read_address, read_size)) {
            continue;
          }
          std::function<void(const void* buffer, size_t size)> callback;
          {
            std::lock_guard lock(interrupt_mutex_);
            if (flow_ring_callbacks_[i].empty()) {
              continue;
            }
            callback = std::move(flow_ring_callbacks_[i].front());
            flow_ring_callbacks_[i].pop_front();
          }
          callback(read_address, read_size);
        }
        flow_ring.read_index.store(read_index);
      }
    }

    uint32_t interrupt_mask = 0;
    if (complete_ring_write_signal_.exchange(0)) {
      interrupt_mask |= BRCMF_PCIE_MB_INT_D2H_DB;
    }

    if (interrupt_mask != 0) {
      did_work = true;
      std::lock_guard lock(interrupt_mutex_);
      for (auto& handler : interrupt_handlers_) {
        const uint32_t new_mask = handler->HandleInterrupt(interrupt_mask);
        interrupt_mask &= ~new_mask;
      }
    }

    // Use polling to service our interrupts.  Only sleep if we did no work this cycle.
    if (!did_work) {
      zx::nanosleep(zx::deadline_after(kInterruptPollingInterval));
    }
  }
}

bool FakeMsgbufInterfaces::ProcessControlSubmitRingEntry(const void* data, size_t size) {
  const MsgbufCommonHeader* const header = reinterpret_cast<const MsgbufCommonHeader*>(data);
  if (header->msgtype == MsgbufCommonHeader::MsgType::kIoctlBufferPost) {
    const MsgbufIoctlOrEventBufferPost* const ioctl_buffer_post =
        reinterpret_cast<const MsgbufIoctlOrEventBufferPost*>(data);
    if (size < sizeof(*ioctl_buffer_post)) {
      return false;
    }
    std::lock_guard lock(rx_buffers_mutex_);
    auto& buffer = ioctl_rx_buffers_.emplace_back();
    buffer.index = ioctl_buffer_post->msg.request_id;
    buffer.address = GetDmaBufferAddress(ioctl_buffer_post->host_buf_addr);
    buffer.dma_address = ioctl_buffer_post->host_buf_addr;
    buffer.size = ioctl_buffer_post->host_buf_len;
    rx_buffers_condvar_.notify_all();
    return true;
  } else if (header->msgtype == MsgbufCommonHeader::MsgType::kEventBufferPost) {
    const MsgbufIoctlOrEventBufferPost* const event_buffer_post =
        reinterpret_cast<const MsgbufIoctlOrEventBufferPost*>(data);
    if (size < sizeof(*event_buffer_post)) {
      return false;
    }
    std::lock_guard lock(rx_buffers_mutex_);
    auto& buffer = event_rx_buffers_.emplace_back();
    buffer.index = event_buffer_post->msg.request_id;
    buffer.address = GetDmaBufferAddress(event_buffer_post->host_buf_addr);
    buffer.dma_address = event_buffer_post->host_buf_addr;
    buffer.size = event_buffer_post->host_buf_len;
    rx_buffers_condvar_.notify_all();
    return true;
  }
  return false;
}

bool FakeMsgbufInterfaces::ProcessRxBufferSubmitRingEntry(const void* data, size_t size) {
  const MsgbufCommonHeader* const header = reinterpret_cast<const MsgbufCommonHeader*>(data);
  if (header->msgtype == MsgbufCommonHeader::MsgType::kRxBufferPost) {
    const MsgbufRxBufferPost* const rx_buffer_post =
        reinterpret_cast<const MsgbufRxBufferPost*>(data);
    if (size < sizeof(*rx_buffer_post)) {
      return false;
    }
    std::lock_guard lock(rx_buffers_mutex_);
    auto& buffer = rx_buffers_.emplace_back();
    buffer.index = rx_buffer_post->msg.request_id;
    buffer.address = GetDmaBufferAddress(rx_buffer_post->data_buf_addr);
    buffer.dma_address = rx_buffer_post->data_buf_addr;
    buffer.size = rx_buffer_post->data_buf_len;
    rx_buffers_condvar_.notify_all();
    return true;
  }
  return false;
}

bool FakeMsgbufInterfaces::ProcessFlowRingEntry(int flow_ring_index, const void* data,
                                                size_t size) {
  // We don't do anything with flow ring entries here.
  return false;
}

zx_status_t FakeMsgbufInterfaces::AddCompleteRingEntry(CompleteRing* ring, const void* buffer,
                                                       size_t size) {
  if (size > ring->ring->item_size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const uint16_t read_index = ring->read_index.load();
  uint16_t write_index = ring->write_index.load();
  if (((write_index + 1) % ring->ring->capacity()) == read_index) {
    return ZX_ERR_SHOULD_WAIT;
  }

  void* const write_address = reinterpret_cast<void*>(
      GetDmaBufferAddress(ring->ring->dma_address()) + write_index * ring->ring->item_size());
  std::memcpy(write_address, buffer, size);
  std::atomic_thread_fence(std::memory_order::memory_order_release);

  write_index += 1;
  if (write_index == ring->ring->capacity()) {
    write_index = 0;
  }
  ring->write_index.store(write_index);
  complete_ring_write_signal_.store(1);
  return ZX_OK;
}

}  // namespace brcmfmac
}  // namespace wlan
