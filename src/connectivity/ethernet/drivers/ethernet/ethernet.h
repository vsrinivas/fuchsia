// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_ETHERNET_DRIVERS_ETHERNET_ETHERNET_H_
#define SRC_CONNECTIVITY_ETHERNET_DRIVERS_ETHERNET_ETHERNET_H_

#include <fuchsia/hardware/ethernet/c/fidl.h>
#include <lib/fidl-utils/bind.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/operation/ethernet.h>
#include <lib/sync/completion.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/fifo.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <zircon/assert.h>
#include <zircon/device/ethernet.h>
#include <zircon/listnode.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <memory>
#include <optional>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/ethernet.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/ethernet.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

namespace eth {

class EthDev0;
class EthDev;

struct TransmitInfo {
  TransmitInfo() {}
  TransmitInfo(fbl::RefPtr<EthDev> ethdev) : edev(std::move(ethdev)) {}

  uint64_t fifo_cookie = 0;
  fbl::RefPtr<EthDev> edev;
};

using TransmitBuffer = eth::Operation<TransmitInfo>;
using TransmitBufferPool = eth::OperationPool<TransmitInfo>;

using EthDev0Type = ddk::Device<EthDev0, ddk::Openable, ddk::Unbindable>;

class EthDev0 : public EthDev0Type, public ddk::EmptyProtocol<ZX_PROTOCOL_ETHERNET> {
 public:
  EthDev0(zx_device_t* parent);

  EthDev0(const EthDev0&) = delete;
  EthDev0(EthDev0&&) = delete;
  EthDev0& operator=(const EthDev0&) = delete;
  EthDev0& operator=(EthDev0&&) = delete;

  ~EthDev0();

  static zx_status_t EthBind(void* ctx, zx_device_t* dev);
  zx_status_t DdkOpen(zx_device_t** dev_out, uint32_t flags);
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  zx_status_t AddDevice();
  void SetStatus(uint32_t status);
  void Recv(const void* data, size_t len, uint32_t flags);
  void CompleteTx(ethernet_netbuf_t* netbuf, zx_status_t status);

 protected:
  void DestroyAllEthDev();

 private:
  friend class EthDev;

  // Resend transmitted packets for loopback.
  void TransmitEcho(const void* data, size_t len);

  ddk::EthernetImplProtocolClient mac_;
  ethernet_info_t info_ = {};
  uint32_t status_ = 0;

  int32_t promisc_requesters_ = 0;
  int32_t multicast_promisc_requesters_ = 0;

  fbl::Mutex ethdev_lock_;

  // Active and idle instances (EthDev).
  fbl::DoublyLinkedList<fbl::RefPtr<EthDev>> list_active_ __TA_GUARDED(ethdev_lock_);
  fbl::DoublyLinkedList<fbl::RefPtr<EthDev>> list_idle_ __TA_GUARDED(ethdev_lock_);
};

using EthDevType = ddk::Device<EthDev, ddk::Openable, ddk::Closable, ddk::Messageable>;

class EthDev : public EthDevType,
               public ddk::EmptyProtocol<ZX_PROTOCOL_ETHERNET>,
               public fbl::DoublyLinkedListable<fbl::RefPtr<EthDev>>,
               public fbl::RefCounted<EthDev> {
 public:
  EthDev(zx_device_t* parent, EthDev0* edev0) : EthDevType(parent), edev0_(edev0), open_count_(1) {}

  EthDev(const EthDev&) = delete;
  EthDev(EthDev&&) = delete;
  EthDev& operator=(const EthDev&) = delete;
  EthDev& operator=(EthDev&&) = delete;

  zx_status_t DdkOpen(zx_device_t** dev_out, uint32_t flags);
  zx_status_t DdkClose(uint32_t flags);
  void DdkRelease();
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
  zx_status_t AddDevice(zx_device_t** out);

  // These methods are guarded by EthDev0's ethdev_lock_.
  zx_status_t MsgGetInfoLocked(fidl_txn_t* txn) __TA_REQUIRES(edev0_->ethdev_lock_);
  zx_status_t MsgGetFifosLocked(fidl_txn_t* txn) __TA_REQUIRES(edev0_->ethdev_lock_);
  zx_status_t MsgSetIOBufferLocked(zx_handle_t h, fidl_txn_t* txn)
      __TA_REQUIRES(edev0_->ethdev_lock_);
  zx_status_t MsgStartLocked(fidl_txn_t* txn) __TA_REQUIRES(edev0_->ethdev_lock_);
  zx_status_t MsgStopLocked(fidl_txn_t* txn) __TA_REQUIRES(edev0_->ethdev_lock_);
  zx_status_t MsgListenStartLocked(fidl_txn_t* txn) __TA_REQUIRES(edev0_->ethdev_lock_);
  zx_status_t MsgListenStopLocked(fidl_txn_t* txn) __TA_REQUIRES(edev0_->ethdev_lock_);
  zx_status_t MsgSetClientNameLocked(const char* buf, size_t len, fidl_txn_t* txn)
      __TA_REQUIRES(edev0_->ethdev_lock_);
  zx_status_t MsgGetStatusLocked(fidl_txn_t* txn) __TA_REQUIRES(edev0_->ethdev_lock_);
  zx_status_t MsgSetPromiscLocked(bool enabled, fidl_txn_t* txn)
      __TA_REQUIRES(edev0_->ethdev_lock_);
  zx_status_t MsgConfigMulticastAddMacLocked(const fuchsia_hardware_ethernet_MacAddress* mac,
                                             fidl_txn_t* txn) __TA_REQUIRES(edev0_->ethdev_lock_);
  zx_status_t MsgConfigMulticastDeleteMacLocked(const fuchsia_hardware_ethernet_MacAddress* mac,
                                                fidl_txn_t* txn)
      __TA_REQUIRES(edev0_->ethdev_lock_);
  zx_status_t MsgConfigMulticastSetPromiscuousModeLocked(bool enabled, fidl_txn_t* txn)
      __TA_REQUIRES(edev0_->ethdev_lock_);
  zx_status_t MsgConfigMulticastTestFilterLocked(fidl_txn_t* txn)
      __TA_REQUIRES(edev0_->ethdev_lock_);
  zx_status_t MsgDumpRegistersLocked(fidl_txn_t* txn) __TA_REQUIRES(edev0_->ethdev_lock_);
  ~EthDev();

 private:
  friend class EthDev0;

  // Transmit thread has been created.
  static constexpr uint32_t kStateTransmitThreadCreated = (1 << 0);
  // Connected to the ethmac and handling traffic.
  static constexpr uint32_t kStateRunning = (1 << 1);
  // Being destroyed.
  static constexpr uint32_t kStateDead = (1 << 2);
  // This client should loopback tx packets to rx path.
  static constexpr uint32_t kStateTransmissionLoopback = (1 << 3);
  // This client wants to observe loopback tx packets.
  static constexpr uint32_t kStateTransmissionListen = (1 << 4);
  // This client has requested promisc mode.
  static constexpr uint32_t kStatePromiscuous = (1 << 5);
  // This client has requested multicast promisc mode.
  static constexpr uint32_t kStateMulticastPromiscuous = (1 << 6);

  static constexpr uint32_t kFifoDepth = 256;
  static constexpr uint32_t kFifoEntrySize = sizeof(eth_fifo_entry_t);
  static constexpr uint32_t kPageMask = PAGE_SIZE - 1;
  // Ensure that we will not exceed fifo capacity.
  // Limited to one page - see fifo_create.md.
  static_assert((kFifoDepth * kFifoEntrySize) <= 4096, "");
  // Number of empty fifo entries to read at a time.
  static constexpr uint32_t kFifoBatchSize = 32;

  // How many multicast addresses to remember before punting and turning on multicast-promiscuous.
  // TODO(eventually): enable deleting addresses.
  // If this value is changed, change the EthernetMulticastPromiscOnOverflow() test in
  //   src/connectivity/ethernet/tests/ethernet/ethernet.cpp.
  static constexpr uint32_t kMulticastListLimit = 32;

  static constexpr uint32_t kFailureReportRate = 50;

  int TransmitThread();
  zx_status_t PromiscHelperLogicLocked(bool req_on, uint32_t state_bit, uint32_t param_id,
                                       int32_t* requesters_count)
      __TA_REQUIRES(edev0_->ethdev_lock_);
  zx_status_t SetPromiscLocked(bool req_on) __TA_REQUIRES(edev0_->ethdev_lock_);
  zx_status_t SetMulticastPromiscLocked(bool req_on) __TA_REQUIRES(edev0_->ethdev_lock_);
  zx_status_t RebuildMulticastFilterLocked() __TA_REQUIRES(edev0_->ethdev_lock_);
  int MulticastAddressIndex(const uint8_t* mac) __TA_REQUIRES(edev0_->ethdev_lock_);
  zx_status_t AddMulticastAddressLocked(const uint8_t* mac) __TA_REQUIRES(edev0_->ethdev_lock_);
  zx_status_t DelMulticastAddressLocked(const uint8_t* mac) __TA_REQUIRES(edev0_->ethdev_lock_);
  zx_status_t TestClearMulticastPromiscLocked() __TA_REQUIRES(edev0_->ethdev_lock_);

  int TransmitFifoWrite(eth_fifo_entry_t* entries, size_t count);

  // Borrows a TX buffer from the pool. Logs and returns std::nullopt if none is available.
  std::optional<TransmitBuffer> GetTransmitBuffer();
  // Returns a TX buffer to the pool.
  void PutTransmitBuffer(TransmitBuffer buffer);

  int Send(eth_fifo_entry_t* entries, size_t count);
  void StopAndKill();

  // These methods are guarded by EthDev0's ethdev_lock_.
  void RecvLocked(const void* data, size_t len, uint32_t extra) __TA_REQUIRES(edev0_->ethdev_lock_);
  void KillLocked() __TA_REQUIRES(edev0_->ethdev_lock_);
  zx_status_t StopLocked() __TA_REQUIRES(edev0_->ethdev_lock_);
  void ClearFilteringLocked() __TA_REQUIRES(edev0_->ethdev_lock_);
  zx_status_t SetClientNameLocked(const void* in_buf, size_t in_len)
      __TA_REQUIRES(edev0_->ethdev_lock_);
  zx_status_t GetStatusLocked(void* out_buf, size_t out_len, size_t* out_actual)
      __TA_REQUIRES(edev0_->ethdev_lock_);
  zx_status_t StartLocked() __TA_REQUIRES(edev0_->ethdev_lock_);
  zx_status_t TransmitListenLocked(bool yes) __TA_REQUIRES(edev0_->ethdev_lock_);
  zx_status_t GetFifosLocked(struct fuchsia_hardware_ethernet_Fifos* fifos)
      __TA_REQUIRES(edev0_->ethdev_lock_);
  zx_status_t SetIObufLocked(zx_handle_t vmo) __TA_REQUIRES(edev0_->ethdev_lock_);

  // This is used for signaling that TransmitThread() should exit.
  static const zx_signals_t kSignalFifoTerminate = ZX_USER_SIGNAL_0;

  EthDev0* edev0_ = nullptr;

  uint8_t multicast_[kMulticastListLimit][ETH_MAC_SIZE] = {};
  uint32_t num_multicast_ = 0;

  uint32_t state_ = 0;
  char name_[fuchsia_hardware_ethernet_MAX_CLIENT_NAME_LEN + 1] = "";

  // Fifos are named from the perspective of the packet from the client
  // to the network interface.
  zx::fifo transmit_fifo_;
  uint32_t transmit_fifo_depth_ = 0;
  zx::fifo receive_fifo_;
  uint32_t receive_fifo_depth_ = 0;
  eth_fifo_entry_t receive_fifo_entries_[kFifoBatchSize] = {};
  size_t receive_fifo_entry_count_ = 0;

  // io buffer.
  zx::vmo io_vmo_;
  fzl::VmoMapper io_buffer_;
  std::unique_ptr<zx_paddr_t[]> paddr_map_ = nullptr;
  zx::pmt pmt_;

  TransmitBufferPool free_transmit_buffers_;

  fbl::Mutex lock_;  // Protects free_tx_bufs.
  uint64_t open_count_ __TA_GUARDED(lock_) = 0;

  // fifo transmit thread.
  thrd_t transmit_thread_ = {};

  uint32_t fail_receive_read_ = 0;
  uint32_t fail_receive_write_ = 0;
  uint32_t ethernet_request_count_ = 0;
  uint32_t ethernet_response_count_ = 0;
  // sync_completion_t* completion_ = nullptr;
};

}  // namespace eth

#endif  // SRC_CONNECTIVITY_ETHERNET_DRIVERS_ETHERNET_ETHERNET_H_
