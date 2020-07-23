// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_SESSION_H_
#define SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_SESSION_H_

#include <lib/async/dispatcher.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/fzl/pinned-vmo.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/event.h>
#include <lib/zx/fifo.h>
#include <lib/zx/vmo.h>
#include <threads.h>
#include <zircon/device/network.h>

#include <ddktl/protocol/network/device.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/span.h>

#include "data_structs.h"
#include "definitions.h"
#include "rx_queue.h"

namespace network::internal {
class DeviceInterface;

struct RefCountedFifo : public fbl::RefCounted<RefCountedFifo> {
  zx::fifo fifo;
};

// A client session with a network device interface.
//
// Session will spawn a thread that will handle the fuchsia.hardware.network.Session FIDL control
// plane calls and service the Tx FIFO associated with the client session.
//
// It is invalid to destroy a Session that has outstanding buffers, that is, buffers that are
// currently owned by the interface's Rx or Tx queues.
class Session : public fbl::DoublyLinkedListable<std::unique_ptr<Session>>,
                public netdev::Session::Interface {
 public:
  ~Session() override;
  // Creates a new session with the provided parameters.
  //
  // The session will service fuchsia.hardware.network.Session FIDL calls on the provided `control`
  // channel.
  //
  // All control plane calls are operated on the provided `dispatcher`, and a dedicated thread will
  // be spawned to handle data fast path operations (tx data plane).
  //
  // The resulting session is stored in `out_session`, and the created FIFO objects used for the
  // data path are stored in `out_fifos` upon successful creation.
  static zx_status_t Create(async_dispatcher_t* dispatcher, netdev::SessionInfo info,
                            fidl::StringView name, DeviceInterface* parent, zx::channel control,
                            std::unique_ptr<Session>* out_session, netdev::Fifos* out_fifos);
  bool IsPrimary() const;
  bool IsListen() const;
  bool IsPaused() const;
  // Checks if this session is eligible to take over the primary session from `current_primary`.
  // `current_primary` can be null, meaning there's no current primary session.
  bool ShouldTakeOverPrimary(const Session* current_primary) const;

  // FIDL interface implementation:
  void SetPaused(bool paused, SetPausedCompleter::Sync _completer) override;
  void Close(CloseCompleter::Sync _completer) override;

  // Sets the return code for a tx descriptor.
  void MarkTxReturnResult(uint16_t descriptor, zx_status_t status);
  // Returns tx descriptors to the session client.
  void ReturnTxDescriptors(const uint16_t* descriptors, uint32_t count);
  // Signals the session thread to observe the tx FIFO object.
  void ResumeTx();
  // Signals the session thread to stop servicing the session channel and FIFOs.
  // When the session thread is finished, it notifies the DeviceInterface parent through
  // `NotifyDeadSession`.
  void Kill();

  // Loads rx descriptors into the provided session transaction, fetching more from the rx FIFO if
  // needed.
  zx_status_t LoadRxDescriptors(RxQueue::SessionTransaction* transact);
  // Sets the data in the space buffer `buff` to region described by `descriptor_index`.
  zx_status_t FillRxSpace(uint16_t descriptor_index, rx_space_buffer_t* buff);
  // Completes rx for `descriptor_index`. Returns true if the buffer can be immediately reused.
  bool CompleteRx(uint16_t descriptor_index, const rx_buffer_t* buff);
  // Completes rx by copying the data from another session into one of this session's available rx
  // buffers.
  void CompleteRxWith(const Session& owner, uint16_t owner_index, const rx_buffer_t* buff);
  // Copies data from a tx frame from another session into one of this session's available rx
  // buffers.
  bool ListenFromTx(const Session& owner, uint16_t owner_index);
  // Commits pending rx buffers, sending them back to the session client.
  void CommitRx();
  bool IsSubscribedToFrameType(uint8_t frame_type);

  inline void TxTaken() { in_flight_tx_++; }
  inline void TxReturned() {
    ZX_ASSERT(in_flight_tx_ != 0);
    in_flight_tx_--;
  }
  inline void RxTaken() { in_flight_rx_++; }
  inline bool RxReturned() {
    ZX_ASSERT(in_flight_rx_ != 0);
    in_flight_rx_--;
    return rx_valid_;
  }
  inline void StopRx() { rx_valid_ = false; }
  inline bool CanDestroy() { return in_flight_rx_ == 0 && in_flight_tx_ == 0; }
  // Clears internal references to data VMO, returning the vmo_id that was associated with this
  // session.
  uint8_t ReleaseDataVmo();

  const fbl::RefPtr<RefCountedFifo>& rx_fifo() { return fifo_rx_; }
  const char* name() const { return name_.data(); }

 private:
  Session(async_dispatcher_t* dispatcher, netdev::SessionInfo* info, fidl::StringView name,
          DeviceInterface* parent);
  zx_status_t Init(netdev::Fifos* out);
  zx_status_t Bind(zx::channel channel);
  void StopTxThread();
  void OnUnbind(fidl::UnbindInfo::Reason reason, zx::channel channel);
  int Thread();
  // Fetch tx descriptors from the FIFO and queue them in the parent `DeviceInterface`'s TxQueue.
  zx_status_t FetchTx();
  buffer_descriptor_t* descriptor(uint16_t index);

  const buffer_descriptor_t* descriptor(uint16_t index) const;
  fbl::Span<uint8_t> data_at(uint64_t offset, uint64_t len) const;
  // Loads a completed rx buffer information back into the descriptor with the provided index.
  zx_status_t LoadRxInfo(uint16_t descriptor_index, const rx_buffer_t* buff);
  // Loads all rx descriptors that are already available into the given transaction.
  bool LoadAvailableRxDescriptors(RxQueue::SessionTransaction* transact);
  // Fetches rx descriptors from the rx FIFO.
  zx_status_t FetchRxDescriptors();

  async_dispatcher_t* dispatcher_;
  std::array<char, netdev::MAX_SESSION_NAME + 1> name_{};
  // `MAX_VMOS` is used as a marker for invalid VMO identifier.
  // The destructor checks that vmo_id is set to `MAX_VMOS`, which verifies that `ReleaseDataVmo`
  // was called before destruction.
  uint8_t vmo_id_ = MAX_VMOS;
  // Unowned pointer to data VMO stored in DeviceInterface.
  // Set by Session::Create.
  DataVmoStore::StoredVmo* data_vmo_ = nullptr;
  zx::port tx_port_;
  fit::optional<fidl::ServerBindingRef<netdev::Session>> binding_;
  // The control channel is only set by the session teardown process if an epitaph must be sent when
  // all the buffers are properly reclaimed. It is set to the channel that was previously bound in
  // the `binding_` Server.
  fit::optional<zx::channel> control_channel_;
  zx::vmo vmo_descriptors_;
  fzl::VmoMapper descriptors_;
  fbl::RefPtr<RefCountedFifo> fifo_rx_;
  zx::fifo fifo_tx_;
  std::atomic_bool paused_;
  uint16_t descriptor_count_;
  uint64_t descriptor_length_;
  netdev::SessionFlags flags_;
  std::array<uint8_t, netdev::MAX_FRAME_TYPES> frame_types_{};
  uint32_t frame_type_count_;
  // Pointer to parent network device, not owned.
  DeviceInterface* parent_;
  fit::optional<thrd_t> thread_;
  std::unique_ptr<uint16_t[]> rx_return_queue_;
  size_t rx_return_queue_count_ = 0;
  std::unique_ptr<uint16_t[]> rx_avail_queue_;
  size_t rx_avail_queue_count_ = 0;

  size_t in_flight_tx_ = 0;
  size_t in_flight_rx_ = 0;
  bool rx_valid_ = true;

  DISALLOW_COPY_ASSIGN_AND_MOVE(Session);
};

using SessionList = fbl::DoublyLinkedList<std::unique_ptr<Session>>;

}  // namespace network::internal

#endif  // SRC_CONNECTIVITY_NETWORK_DRIVERS_NETWORK_DEVICE_DEVICE_SESSION_H_
