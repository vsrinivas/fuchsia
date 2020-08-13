// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/msgbuf_ring_handler.h"

#include <lib/sync/completion.h>
#include <netinet/if_ether.h>
#include <zircon/assert.h>
#include <zircon/limits.h>
#include <zircon/status.h>
#include <zircon/time.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil_types.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/regs.h"

namespace wlan {
namespace brcmfmac {

namespace {

union ControlSubmitRingEntry {
  MsgbufCommonHeader common_header;
  MsgbufIoctlRequest ioctl_request;
  MsgbufIoctlOrEventBufferPost ioctl_or_event_buffer_post;
};

union RxBufferSubmitRingEntry {
  MsgbufCommonHeader common_header;
  MsgbufRxBufferPost rx_buffer_post;
};

union ControlCompleteRingEntry {
  MsgbufCommonHeader common_header;
  MsgbufIoctlResponse ioctl_response;
  MsgbufWlEvent wl_event;
};

union TxCompleteRingEntry {
  MsgbufCommonHeader common_header;
};

union RxCompleteRingEntry {
  MsgbufCommonHeader common_header;
};

}  // namespace

struct MsgbufRingHandler::IoctlState {
  DmaPool::Buffer response;
  size_t response_size = 0;
  uint16_t transaction_id = 0;
  bcme_status_t firmware_error = BCME_OK;
  zx_status_t status = ZX_OK;
  sync_completion_t completion;
};

MsgbufRingHandler::EventHandler::~EventHandler() = default;

MsgbufRingHandler::MsgbufRingHandler() = default;

MsgbufRingHandler::~MsgbufRingHandler() {
  if (interrupt_provider_ != nullptr) {
    interrupt_provider_->RemoveInterruptHandler(this);

    // Manually invoke the interrupt handler one more time before we stop the worker thread.  Mostly
    // for the benefit of unit tests, though this is not incorrect on actual hardware either.
    HandleInterrupt(BRCMF_PCIE_MB_INT_D2H_DB);
  }

  if (worker_thread_.joinable()) {
    WorkList work_list;
    work_list.emplace_back([this]() {
      AssertIsWorkerThread();
      worker_thread_exit_ = true;
    });
    AppendToWorkQueue(std::move(work_list));
    worker_thread_.join();
  }
}

// static
zx_status_t MsgbufRingHandler::Create(DmaRingProviderInterface* dma_ring_provider,
                                      InterruptProviderInterface* interrupt_provider,
                                      std::unique_ptr<DmaPool> rx_buffer_pool,
                                      std::unique_ptr<DmaPool> tx_buffer_pool,
                                      EventHandler* event_handler,
                                      std::unique_ptr<MsgbufRingHandler>* out_handler) {
  zx_status_t status = ZX_OK;

  auto handler = std::make_unique<MsgbufRingHandler>();

  if (dma_ring_provider->GetControlSubmitRing() == nullptr ||
      dma_ring_provider->GetControlSubmitRing()->item_size() < sizeof(ControlSubmitRingEntry)) {
    BRCMF_ERR("Control submission ring too small: has %zu, requires %zu",
              dma_ring_provider->GetControlSubmitRing()->item_size(),
              sizeof(ControlSubmitRingEntry));
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  if (dma_ring_provider->GetRxBufferSubmitRing() == nullptr ||
      dma_ring_provider->GetRxBufferSubmitRing()->item_size() < sizeof(RxBufferSubmitRingEntry)) {
    BRCMF_ERR("Rx buffer submission ring too small: has %zu, requires %zu",
              dma_ring_provider->GetRxBufferSubmitRing()->item_size(),
              sizeof(RxBufferSubmitRingEntry));
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  if (dma_ring_provider->GetControlCompleteRing() == nullptr ||
      dma_ring_provider->GetControlCompleteRing()->item_size() < sizeof(ControlCompleteRingEntry)) {
    BRCMF_ERR("Control completion ring too small: has %zu, requires %zu",
              dma_ring_provider->GetControlCompleteRing()->item_size(),
              sizeof(ControlCompleteRingEntry));
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  if (dma_ring_provider->GetTxCompleteRing() == nullptr ||
      dma_ring_provider->GetTxCompleteRing()->item_size() < sizeof(TxCompleteRingEntry)) {
    BRCMF_ERR("Tx completion ring too small: has %zu, requires %zu",
              dma_ring_provider->GetTxCompleteRing()->item_size(), sizeof(TxCompleteRingEntry));
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  if (dma_ring_provider->GetRxCompleteRing() == nullptr ||
      dma_ring_provider->GetRxCompleteRing()->item_size() < sizeof(RxCompleteRingEntry)) {
    BRCMF_ERR("Rx completion ring too small: has %zu, requires %zu",
              dma_ring_provider->GetRxCompleteRing()->item_size(), sizeof(RxCompleteRingEntry));
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  // Set up thread-safe state.  No locking necessary.

  const auto& dma_config = dma_ring_provider->GetDmaConfig();

  const int min_rx_buffer_count =
      dma_config.max_ioctl_rx_buffers + dma_config.max_event_rx_buffers + dma_config.max_rx_buffers;
  if (rx_buffer_pool->buffer_count() < min_rx_buffer_count) {
    BRCMF_ERR("Rx buffer pool too small: req %u avail %u", min_rx_buffer_count,
              rx_buffer_pool->buffer_count());
    return ZX_ERR_INVALID_ARGS;
  }
  handler->rx_buffer_pool_ = std::move(rx_buffer_pool);

  const int min_tx_buffer_count = 1;
  if (tx_buffer_pool->buffer_count() < min_tx_buffer_count) {
    BRCMF_ERR("Rx buffer pool too small: req %u avail %u", min_tx_buffer_count,
              tx_buffer_pool->buffer_count());
    return ZX_ERR_INVALID_ARGS;
  }
  handler->tx_buffer_pool_ = std::move(tx_buffer_pool);

  // Register the handler for interrupts now.
  if ((status = interrupt_provider->AddInterruptHandler(handler.get())) != ZX_OK) {
    BRCMF_ERR("Failed to add interrupt: %s", zx_status_get_string(status));
    return status;
  }
  handler->interrupt_provider_ = interrupt_provider;

  handler->rx_data_offset_ = dma_config.rx_data_offset;

  // Set up thread-sensitive state.  Note that, strictly speaking, locking is unnecessary, but we do
  // it here anyways because:
  //
  // 1.  It's pedantically correct.
  // 2.  It's negligible-cost here, running once during initialization in an uncontended
  //     single-threaded context.
  // 3.  Most importantly, it keeps the compiler threading analysis happy, which allows us to keep
  //     it enabled.

  {
    std::lock_guard lock(handler->interrupt_handler_mutex_);
    handler->control_complete_ring_ = dma_ring_provider->GetControlCompleteRing();
    handler->tx_complete_ring_ = dma_ring_provider->GetTxCompleteRing();
    handler->rx_complete_ring_ = dma_ring_provider->GetRxCompleteRing();
  }

  {
    std::lock_guard lock(handler->worker_thread_mutex_);
    handler->control_submit_ring_ = dma_ring_provider->GetControlSubmitRing();
    handler->rx_buffer_submit_ring_ = dma_ring_provider->GetRxBufferSubmitRing();
    handler->required_ioctl_rx_buffers_ = dma_config.max_ioctl_rx_buffers;
    handler->required_event_rx_buffers_ = dma_config.max_event_rx_buffers;
    handler->required_rx_buffers_ = dma_config.max_rx_buffers;
    if ((status = handler->QueueRxBuffers()) != ZX_OK) {
      BRCMF_ERR("Failed to queue initial rx buffers: %s", zx_status_get_string(status));
      return status;
    }

    handler->event_handler_ = event_handler;
    handler->worker_thread_ = std::thread(&MsgbufRingHandler::WorkerThreadFunction, handler.get());
  }

  *out_handler = std::move(handler);
  return ZX_OK;
}

zx_status_t MsgbufRingHandler::GetTxBuffer(DmaPool::Buffer* out_buffer) {
  return tx_buffer_pool_->Allocate(out_buffer);
}

zx_status_t MsgbufRingHandler::Ioctl(uint8_t interface_index, uint32_t command,
                                     DmaPool::Buffer tx_data, size_t tx_data_size,
                                     DmaPool::Buffer* rx_data, size_t* rx_data_size,
                                     bcme_status_t* firmware_error, zx::duration timeout) {
  zx_status_t status = ZX_OK;

  // Device ioctls can only go up to the ethernet frame size.
  constexpr size_t kMaxIoctlDataSize = ETH_FRAME_LEN;
  if (tx_data_size > kMaxIoctlDataSize) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  zx_paddr_t tx_data_dma_address = 0;
  if ((status = tx_data.Pin(&tx_data_dma_address)) != ZX_OK) {
    return status;
  }

  WorkList work_list;

  // Submit the ioctl request on the worker thread.
  IoctlState ioctl_state = {};
  ioctl_state.response = std::move(tx_data);
  ioctl_state.status = ZX_OK;
  work_list.emplace_back([&]() {
    AssertIsWorkerThread();

    const zx_status_t status = [&]() {
      AssertIsWorkerThread();
      zx_status_t status = ZX_OK;

      if (ioctl_state_ != nullptr) {
        // Only one Ioctl() call can be in-flight at any time.
        return ZX_ERR_ALREADY_EXISTS;
      }

      void* buffer = nullptr;
      if ((status = control_submit_ring_->MapWrite(1, &buffer)) != ZX_OK) {
        return status;
      }
      const auto ioctl_request = new (buffer) MsgbufIoctlRequest{};
      ioctl_request->msg.msgtype = MsgbufIoctlRequest::kMsgType;
      ioctl_request->msg.ifidx = interface_index;
      ioctl_request->cmd = command;
      ioctl_request->trans_id = ioctl_transaction_id_;
      ioctl_request->output_buf_len = rx_buffer_pool_->buffer_size();
      ioctl_request->input_buf_len = tx_data_size;
      ioctl_request->req_buf_addr = tx_data_dma_address;

      ioctl_state.transaction_id = ioctl_transaction_id_;
      ++ioctl_transaction_id_;

      // Submit the command.
      if ((status = control_submit_ring_->CommitWrite(1)) != ZX_OK) {
        return status;
      }

      ioctl_state_ = &ioctl_state;
      return ZX_OK;
    }();

    if (status != ZX_OK) {
      ioctl_state.status = status;
      sync_completion_signal(&ioctl_state.completion);
    }
  });
  AppendToWorkQueue(std::move(work_list));

  // Wait for the ioctl to be serviced.
  if ((status = sync_completion_wait(&ioctl_state.completion, timeout.get())) != ZX_OK) {
    if (status != ZX_ERR_TIMED_OUT) {
      return status;
    }

    // If we timeout on the completion wait, cancel the ioctl request.
    WorkList work_list;
    work_list.emplace_back([this]() {
      AssertIsWorkerThread();
      if (ioctl_state_ == nullptr) {
        // It's possible that we sent the ioctl request cancel task, but the ioctl managed to
        // complete betwwen the time that we timed out on the completion and the time the cancel
        // task actually executed.  In this case we will just allow the task completion to be
        // reported normally.
        return;
      }

      ioctl_state_->status = ZX_ERR_TIMED_OUT;
      sync_completion_signal(&ioctl_state_->completion);
      ioctl_state_ = nullptr;
    });
    AppendToWorkQueue(std::move(work_list));
    sync_completion_wait(&ioctl_state.completion, ZX_TIME_INFINITE);
  }

  if (ioctl_state.status != ZX_OK) {
    return ioctl_state.status;
  }

  *firmware_error = ioctl_state.firmware_error;
  *rx_data = std::move(ioctl_state.response);
  *rx_data_size = ioctl_state.response_size;
  return ZX_OK;
}

uint32_t MsgbufRingHandler::HandleInterrupt(uint32_t mailboxint) {
  constexpr uint32_t kInterruptMask = BRCMF_PCIE_MB_INT_D2H_DB;
  if ((mailboxint & kInterruptMask) == 0) {
    return 0;
  }

  std::lock_guard lock(interrupt_handler_mutex_);

  // Process our rings.
  WorkList work_list;
  ProcessControlCompleteRing(&work_list);
  ProcessTxCompleteRing(&work_list);
  ProcessRxCompleteRing(&work_list);

  // Append ring entries to the respective work queues.
  AppendToWorkQueue(std::move(work_list));

  return kInterruptMask;
}

void MsgbufRingHandler::HandleMsgbufIoctlResponse(const MsgbufIoctlResponse& ioctl_response,
                                                  WorkList* work_list) {
  work_list->emplace_back([this, request_id = ioctl_response.msg.request_id,
                           resp_len = ioctl_response.resp_len, trans_id = ioctl_response.trans_id,
                           firmware_status = ioctl_response.compl_hdr.status]() {
    AssertIsWorkerThread();
    if (ioctl_state_ == nullptr) {
      BRCMF_ERR("Received ioctl completion without request");
      return;
    }

    IoctlState* const ioctl_state = ioctl_state_;
    ioctl_state_ = nullptr;
    const zx_status_t status = [&]() {
      AssertIsWorkerThread();
      zx_status_t status = ZX_OK;

      if (trans_id != ioctl_state->transaction_id) {
        BRCMF_ERR("Mismatched transaction id, expected %d found %d", ioctl_state->transaction_id,
                  trans_id);
        return ZX_ERR_IO_DATA_INTEGRITY;
      }

      DmaPool::Buffer buffer;
      if ((status = rx_buffer_pool_->Acquire(request_id, &buffer)) != ZX_OK) {
        BRCMF_ERR("Failed to acquire rx buffer %d", request_id);
        return status;
      }
      // We have consumed one RX buffer for this ioctl response.
      ++required_ioctl_rx_buffers_;

      // Fill out the ioctl response, defensively.
      if (resp_len > buffer.size()) {
        BRCMF_ERR("Received bad response length %u, max %zu", resp_len, buffer.size());
        return ZX_ERR_IO_DATA_INTEGRITY;
      }

      ioctl_state->response = std::move(buffer);
      ioctl_state->response_size = resp_len;
      ioctl_state->firmware_error = static_cast<bcme_status_t>(firmware_status);
      return ZX_OK;
    }();

    ioctl_state->status = status;
    sync_completion_signal(&ioctl_state->completion);
  });
}

void MsgbufRingHandler::HandleMsgbufWlEvent(const MsgbufWlEvent& wl_event, WorkList* work_list) {
  // Invoke the event handler, defensively.
  work_list->emplace_back(
      [this, request_id = wl_event.msg.request_id, event_size = wl_event.event_data_len]() {
        AssertIsWorkerThread();
        zx_status_t status = ZX_OK;

        DmaPool::Buffer buffer;
        if ((status = rx_buffer_pool_->Acquire(request_id, &buffer)) != ZX_OK) {
          BRCMF_ERR("Failed to acquire rx buffer %d", request_id);
          return;
        }
        // We have consumed one RX buffer for this ioctl response.
        ++required_event_rx_buffers_;

        if (event_handler_ == nullptr) {
          // No event handler to invoke.
          return;
        }

        const size_t event_size_with_offset = event_size + rx_data_offset_;
        if (event_size_with_offset > buffer.size()) {
          BRCMF_ERR("Received bad data length %zu, max %zu", event_size_with_offset, buffer.size());
          return;
        }
        const void* data = nullptr;
        if ((status = buffer.MapRead(event_size_with_offset, &data)) != ZX_OK) {
          BRCMF_ERR("Failed to map rx buffer %d", buffer.index());
          return;
        }

        const void* const event_data =
            reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(data) + rx_data_offset_);
        event_handler_->HandleWlEvent(event_data, event_size);
      });
}

void MsgbufRingHandler::ProcessControlCompleteRing(WorkList* work_list) {
  zx_status_t status = ZX_OK;
  const void* buffer = nullptr;

  // IMPORTANT: we are reading data back from firmware-written command rings, which may be
  // unreliable and/or security-compromised.  Program defensively!

  uint16_t entry_count = 0;
  while ((entry_count = control_complete_ring_->GetAvailableReads()) > 0) {
    if ((status = control_complete_ring_->MapRead(entry_count, &buffer)) != ZX_OK) {
      BRCMF_ERR("Failed to map control complete ring: %s", zx_status_get_string(status));
      break;
    }
    for (uint16_t entry_index = 0; entry_index < entry_count; ++entry_index) {
      const ControlCompleteRingEntry* const entry =
          reinterpret_cast<const ControlCompleteRingEntry*>(
              reinterpret_cast<const char*>(buffer) +
              entry_index * control_complete_ring_->item_size());
      switch (entry->common_header.msgtype) {
        case MsgbufCommonHeader::MsgType::kIoctlAck: {
          // Nothing to do, really.
          break;
        }

        case MsgbufCommonHeader::MsgType::kIoctlResponse: {
          HandleMsgbufIoctlResponse(entry->ioctl_response, work_list);
          break;
        }

        case MsgbufCommonHeader::MsgType::kWlEvent: {
          HandleMsgbufWlEvent(entry->wl_event, work_list);
          break;
        }

        default: {
          BRCMF_ERR("Invalid msgtype %d", static_cast<int>(entry->common_header.msgtype));
          break;
        }
      }
    }
    control_complete_ring_->CommitRead(entry_count);
  }
}

void MsgbufRingHandler::ProcessTxCompleteRing(WorkList* work_list) {
  zx_status_t status = ZX_OK;
  const void* buffer = nullptr;

  // IMPORTANT: we are reading data back from firmware-written command rings, which may be
  // unreliable and/or security-compromised.  Program defensively!

  uint16_t entry_count = 0;
  while ((entry_count = tx_complete_ring_->GetAvailableReads()) > 0) {
    if ((status = tx_complete_ring_->MapRead(entry_count, &buffer)) != ZX_OK) {
      BRCMF_ERR("Failed to map tx complete ring: %s", zx_status_get_string(status));
      break;
    }
    for (uint16_t entry_index = 0; entry_index < entry_count; ++entry_index) {
      const TxCompleteRingEntry* const entry = reinterpret_cast<const TxCompleteRingEntry*>(
          reinterpret_cast<const char*>(buffer) + entry_index * tx_complete_ring_->item_size());
      switch (entry->common_header.msgtype) {
        default: {
          BRCMF_ERR("Invalid msgtype %d", static_cast<int>(entry->common_header.msgtype));
          break;
        }
      }
    }
    tx_complete_ring_->CommitRead(entry_count);
  }
}

void MsgbufRingHandler::ProcessRxCompleteRing(WorkList* work_list) {
  zx_status_t status = ZX_OK;
  const void* buffer = nullptr;

  // IMPORTANT: we are reading data back from firmware-written command rings, which may be
  // unreliable and/or security-compromised.  Program defensively!

  uint16_t entry_count = 0;
  while ((entry_count = rx_complete_ring_->GetAvailableReads()) > 0) {
    if ((status = rx_complete_ring_->MapRead(entry_count, &buffer)) != ZX_OK) {
      BRCMF_ERR("Failed to map rx complete ring: %s", zx_status_get_string(status));
      break;
    }

    for (uint16_t entry_index = 0; entry_index < entry_count; ++entry_index) {
      const RxCompleteRingEntry* const entry = reinterpret_cast<const RxCompleteRingEntry*>(
          reinterpret_cast<const char*>(buffer) + entry_index * rx_complete_ring_->item_size());
      switch (entry->common_header.msgtype) {
        default: {
          BRCMF_ERR("Invalid msgtype %d", static_cast<int>(entry->common_header.msgtype));
          break;
        }
      }
    }

    rx_complete_ring_->CommitRead(entry_count);
  }
}

void MsgbufRingHandler::AppendToWorkQueue(WorkList work_list) {
  if (work_list.empty()) {
    return;
  }

  std::lock_guard lock(work_queue_mutex_);
  work_queue_.splice(work_queue_.end(), std::move(work_list));
  work_queue_condvar_.notify_all();
}

void MsgbufRingHandler::WorkerThreadFunction() {
  std::lock_guard worker_thread_lock(worker_thread_mutex_);

  WorkList work_list;
  std::lock_guard lock(work_queue_mutex_);
  while (true) {
    // Wait for something interesting to happen.
    while (true) {
      if (worker_thread_exit_) {
        return;
      }

      using std::swap;
      swap(work_list, work_queue_);
      if (!work_list.empty()) {
        break;
      }

      // Since std::unique_lock does not play nice with threading annotations (yet), we hold the
      // work_queue_mutex_ using an std::lock_guard, and shim around it here.
      std::unique_lock ulock(work_queue_mutex_, std::adopt_lock);
      work_queue_condvar_.wait(ulock);
      ulock.release();
    }
    work_queue_mutex_.unlock();

    // Now do the interesting things.
    while (!work_list.empty()) {
      work_list.front()();
      work_list.pop_front();
    }

    {
      // Perform our once-per-batch work.
      zx_status_t status = ZX_OK;
      if ((status = QueueRxBuffers()) != ZX_OK) {
        BRCMF_ERR("Failed to queue rx buffers: %s", zx_status_get_string(status));
      }
    }

    work_queue_mutex_.lock();
  }
}

void MsgbufRingHandler::AssertIsWorkerThread() const {
  ZX_DEBUG_ASSERT(std::this_thread::get_id() == worker_thread_.get_id());
}

zx_status_t MsgbufRingHandler::QueueRxBuffers() {
  zx_status_t status = ZX_OK;

  // Utility lambda to perform the WriteDmaRing queueing, since this is common to all three RX
  // buffer types.
  auto queue_rx_buffers = [&](WriteDmaRing* submit_ring, int* max_entry_count, auto fill_in_entry) {
    int max_count = *max_entry_count;

    const zx_status_t status = [&]() {
      while (true) {
        zx_status_t status = ZX_OK;

        // Submit as many buffers as available entries in the submit ring.
        const int entry_count = std::min<int>(max_count, submit_ring->GetAvailableWrites());
        if (entry_count <= 0) {
          return ZX_OK;
        }

        void* ring_buffer = nullptr;
        if ((status = submit_ring->MapWrite(entry_count, &ring_buffer)) != ZX_OK) {
          return status;
        }

        // Queue up to `entry_count` entries.
        int entries_queued = 0;
        status = [&]() {
          for (entries_queued = 0; entries_queued < entry_count; ++entries_queued) {
            DmaPool::Buffer rx_buffer;
            zx_paddr_t rx_dma_address = 0;
            if ((status = rx_buffer_pool_->Allocate(&rx_buffer)) != ZX_OK) {
              if (status == ZX_ERR_NO_RESOURCES) {
                // This is fine, just try later.
                status = ZX_OK;
              }
              return status;
            }
            if ((status = rx_buffer.Pin(&rx_dma_address)) != ZX_OK) {
              return status;
            }
            void* const data = reinterpret_cast<void*>(reinterpret_cast<char*>(ring_buffer) +
                                                       entries_queued * submit_ring->item_size());
            fill_in_entry(data, rx_buffer.index(), rx_buffer.size(), rx_dma_address);
            rx_buffer.Release();  // The buffer is now owned by the hardware.
          };
          return ZX_OK;
        }();

        if (status != ZX_OK) {
          BRCMF_ERR("Failed to write submit ring entry: %s", zx_status_get_string(status));
          if (entries_queued == 0) {
            // We don't propagate the error as commiting the ring writes up to here can still
            // succeed.
            return ZX_OK;
          }
        }

        // Commit the entries that we did queue.
        if ((status = submit_ring->CommitWrite(entries_queued)) != ZX_OK) {
          return status;
        }
        max_count -= entries_queued;
      }
    }();

    *max_entry_count = max_count;
    return status;
  };

  // Queue ioctl rx buffers on the control submit ring.
  if ((status = queue_rx_buffers(control_submit_ring_, &required_ioctl_rx_buffers_,
                                 [](void* data, int rx_index, size_t rx_size, zx_paddr_t rx_dma) {
                                   auto entry = new (data) MsgbufIoctlOrEventBufferPost{};
                                   entry->msg.msgtype =
                                       MsgbufCommonHeader::MsgType::kIoctlBufferPost;
                                   entry->msg.request_id = rx_index;
                                   entry->host_buf_len = rx_size;
                                   entry->host_buf_addr = rx_dma;
                                 })) != ZX_OK) {
    BRCMF_ERR("Failed to queue ioctl rx buffers: %s", zx_status_get_string(status));
    return status;
  }

  // Queue event rx buffers on the control submit ring.
  if ((status = queue_rx_buffers(control_submit_ring_, &required_event_rx_buffers_,
                                 [](void* data, int rx_index, size_t rx_size, zx_paddr_t rx_dma) {
                                   auto entry = new (data) MsgbufIoctlOrEventBufferPost{};
                                   entry->msg.msgtype =
                                       MsgbufCommonHeader::MsgType::kEventBufferPost;
                                   entry->msg.request_id = rx_index;
                                   entry->host_buf_len = rx_size;
                                   entry->host_buf_addr = rx_dma;
                                 })) != ZX_OK) {
    BRCMF_ERR("Failed to queue event rx buffers: %s", zx_status_get_string(status));
    return status;
  }

  // Queue rx buffers on the rx buffer submit ring.
  if ((status = queue_rx_buffers(rx_buffer_submit_ring_, &required_rx_buffers_,
                                 [](void* data, int rx_index, size_t rx_size, zx_paddr_t rx_dma) {
                                   auto entry = new (data) MsgbufRxBufferPost{};
                                   entry->msg.msgtype = MsgbufRxBufferPost::kMsgType;
                                   entry->msg.request_id = rx_index;
                                   entry->data_buf_len = rx_size;
                                   entry->data_buf_addr = rx_dma;
                                 })) != ZX_OK) {
    BRCMF_ERR("Failed to queue rx buffers: %s", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

}  // namespace brcmfmac
}  // namespace wlan
