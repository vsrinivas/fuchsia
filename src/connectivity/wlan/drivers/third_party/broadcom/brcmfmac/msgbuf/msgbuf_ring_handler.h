// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_MSGBUF_MSGBUF_RING_HANDLER_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_MSGBUF_MSGBUF_RING_HANDLER_H_

#include <lib/fit/function.h>
#include <lib/zx/time.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <thread>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_pool.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_ring.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/fwil_types.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/msgbuf_interfaces.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/msgbuf_structs.h"

namespace wlan {
namespace brcmfmac {

// This class handles communication across the DMA rings provided by a DmaRingProviderInterface, for
// the MSGBUF protocol.  As this class is expected to handle interrupts and API calls on aribtrary
// threads, it is designed to be thread-safe.
class MsgbufRingHandler : public InterruptProviderInterface::InterruptHandler {
 public:
  // This is the interface for event handlers that can be registered to the MsgbufRingHandler.
  class EventHandler {
   public:
    virtual ~EventHandler();

    // Callback to handle a firmware event.
    virtual void HandleWlEvent(const void* data, size_t size) = 0;
  };

  MsgbufRingHandler();
  ~MsgbufRingHandler();

  // Static factory function for MsgbufRingHandler instances.
  static zx_status_t Create(DmaRingProviderInterface* dma_ring_provider,
                            InterruptProviderInterface* interrupt_provider,
                            std::unique_ptr<DmaPool> rx_buffer_pool,
                            std::unique_ptr<DmaPool> tx_buffer_pool, EventHandler* event_handler,
                            std::unique_ptr<MsgbufRingHandler>* out_handler);

  // Get a buffer for use with submitting data over DMA.
  zx_status_t GetTxBuffer(DmaPool::Buffer* out_buffer);

  // Submit an ioctl request in `tx_data`, returning the response in another Buffer which will be
  // returned in `rx_data`.  Any reported firmware error will be returned in `firmware_error`.  Note
  // that it is possible for this call to succeed, but a firmware error to still be reported; the
  // zx_status_t status returned indicates the success state of the mechanics of making the ioctl
  // request, whereas the firmware may still report an error in processing the request.
  zx_status_t Ioctl(uint8_t interface_index, uint32_t command, DmaPool::Buffer tx_data,
                    size_t tx_data_size, DmaPool::Buffer* rx_data, size_t* rx_data_size,
                    bcme_status_t* firmware_error, zx::duration timeout = zx::duration::infinite());

  // InterruptHandler implementation.
  uint32_t HandleInterrupt(uint32_t mailboxint) override;

 private:
  // TL;WR: MsgbufRingHandler is thread-safe on its external API, and in its interrupt handling.
  // Callbacks performed through the EventHandler interface will be performed from a common, but
  // internal worker thread.
  //
  // MsgbufRingHandler provides a thread-safe API, but it has to deal with API calls being made from
  // multiple threads:
  //
  // * The devhost thread, on creation.
  // * wlanstack worker threads, through WlanInterface.
  // * The interrupt handler thread from InterruptProviderInterface.
  //
  // In addition, the MsgbufRingHandler keeps an internal worker thread, for the purposes of:
  //
  // * Offloading potentially long-running work from the interrupt handler thread.
  // * Writing to the control, RX buffer, and flow submit rings.
  //
  // This means a lot of potentially racy data access.  Thus, while exposing a thread-safe API
  // externally, the internal threading model is somewhat pointy and sharp in uncomfortable places.
  // But at least we keep it internal.  It works like this:
  //
  // * Thread-safe (or otherwise safe) member objects require no locking.
  // * The submit rings (control, RX buffer, and flow rings), and associated state such as the RX
  //   buffer counts are all locked and exclusively accessed from the worker thread.  The event
  //   handlers are also worker thread-exclusive.  All significant work is posted to this thread on
  //   the work function queue.
  // * The completion rings are locked and exclusively read on the interrupt handler thread.  Event
  //   though the interrupt handler is non-concurrent with itself, for completeness we take the
  //   (uncontended) mutex once at handler invocation, and release it on return.
  //
  // To help enforce these conventions, we also make extensive use of annotations for static
  // compiler threading analysis.

  //
  // Internal struct definitions.
  //

  // Our work list type.
  using WorkList = std::list<fit::function<void()>>;

  // Struct for state internal to an Ioctl() call.
  struct IoctlState;

  //
  // Work functions.
  //

  // Handle each of the relevant types of MSGBUF messages.
  void HandleMsgbufIoctlResponse(const MsgbufIoctlResponse& ioctl_response, WorkList* work_list)
      __TA_REQUIRES(interrupt_handler_mutex_);
  void HandleMsgbufWlEvent(const MsgbufWlEvent& wl_event, WorkList* work_list)
      __TA_REQUIRES(interrupt_handler_mutex_);

  // Process events on each DMA completion ring.
  void ProcessControlCompleteRing(WorkList* work_list) __TA_REQUIRES(interrupt_handler_mutex_);
  void ProcessTxCompleteRing(WorkList* work_list) __TA_REQUIRES(interrupt_handler_mutex_);
  void ProcessRxCompleteRing(WorkList* work_list) __TA_REQUIRES(interrupt_handler_mutex_);

  // Append a list of work to the work queue.
  void AppendToWorkQueue(WorkList work_list);

  // Worker thread main function.
  void WorkerThreadFunction();

  // Assert that the current thread is the worker thread.  Performs an assert on debug builds; no-op
  // on release.  Used in both cases to signal to the threading analysis that we intend a given
  // scope to be run on the worker thread.
  void AssertIsWorkerThread() const __TA_ASSERT(worker_thread_mutex_);

  // Queue up available RX buffers for the hardware to write to.
  zx_status_t QueueRxBuffers() __TA_REQUIRES(worker_thread_mutex_);

  //
  // Thread-safe state.
  //

  // Interface providers.
  InterruptProviderInterface* interrupt_provider_ = nullptr;

  // DMA buffer pools, for each direction.  These are thread-safe.
  std::unique_ptr<DmaPool> rx_buffer_pool_;
  std::unique_ptr<DmaPool> tx_buffer_pool_;

  // Other state.
  size_t rx_data_offset_;

  //
  // Interrupt handler state.
  //

  // Complete rings, to be used only from the interrupt handler.
  std::mutex interrupt_handler_mutex_ __TA_ACQUIRED_BEFORE(work_queue_mutex_);
  ReadDmaRing* control_complete_ring_ __TA_GUARDED(interrupt_handler_mutex_) = nullptr;
  ReadDmaRing* tx_complete_ring_ __TA_GUARDED(interrupt_handler_mutex_) = nullptr;
  ReadDmaRing* rx_complete_ring_ __TA_GUARDED(interrupt_handler_mutex_) = nullptr;

  //
  // Work queue state.
  //

  std::mutex work_queue_mutex_;
  std::condition_variable work_queue_condvar_ __TA_GUARDED(work_queue_mutex_);
  WorkList work_queue_ __TA_GUARDED(work_queue_mutex_);

  //
  // Worker thread state.
  //

  // Submit rings and buffer counts.
  std::mutex worker_thread_mutex_ __TA_ACQUIRED_BEFORE(work_queue_mutex_);
  WriteDmaRing* control_submit_ring_ __TA_GUARDED(worker_thread_mutex_) = nullptr;
  WriteDmaRing* rx_buffer_submit_ring_ __TA_GUARDED(worker_thread_mutex_) = nullptr;
  int required_ioctl_rx_buffers_ __TA_GUARDED(worker_thread_mutex_) = 0;
  int required_event_rx_buffers_ __TA_GUARDED(worker_thread_mutex_) = 0;
  int required_rx_buffers_ __TA_GUARDED(worker_thread_mutex_) = 0;

  EventHandler* event_handler_ __TA_GUARDED(worker_thread_mutex_) = nullptr;
  IoctlState* ioctl_state_ __TA_GUARDED(worker_thread_mutex_) = nullptr;
  uint16_t ioctl_transaction_id_ __TA_GUARDED(worker_thread_mutex_) = 0;

  bool worker_thread_exit_ __TA_GUARDED(worker_thread_mutex_) = false;
  std::thread worker_thread_;
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_MSGBUF_MSGBUF_RING_HANDLER_H_
