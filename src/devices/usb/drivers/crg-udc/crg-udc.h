// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_CRG_UDC_CRG_UDC_H_
#define SRC_DEVICES_USB_DRIVERS_CRG_UDC_CRG_UDC_H_

#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <fuchsia/hardware/usb/dci/cpp/banjo.h>
#include <fuchsia/hardware/usb/phy/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/device-protocol/pdev.h>
#include <lib/mmio/mmio.h>
#include <lib/sync/completion.h>
#include <threads.h>

#include <atomic>

#include <ddktl/device.h>
#include <fbl/mutex.h>
#include <usb/dwc2/metadata.h>
#include <usb/request-cpp.h>
#include <usb/usb.h>

#include "crg_udc_regs.h"

namespace crg_udc {

class CrgUdc;
using CrgUdcType = ddk::Device<CrgUdc, ddk::Initializable, ddk::Unbindable, ddk::Suspendable>;

class CrgUdc : public CrgUdcType, public ddk::UsbDciProtocol<CrgUdc, ddk::base_protocol> {
 public:
  explicit CrgUdc(zx_device_t* parent) : CrgUdcType(parent) {}
  explicit CrgUdc(zx_device_t* parent, zx::interrupt irq)
      : CrgUdcType(parent), irq_(std::move(irq)) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);
  zx_status_t Init();
  int IrqThread();

  // Device protocol implementation.
  void DdkInit(ddk::InitTxn txn);
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();
  void DdkSuspend(ddk::SuspendTxn txn);

  // USB DCI protocol implementation.
  void UsbDciRequestQueue(usb_request_t* req, const usb_request_complete_callback_t* cb);
  zx_status_t UsbDciSetInterface(const usb_dci_interface_protocol_t* interface);
  zx_status_t UsbDciConfigEp(const usb_endpoint_descriptor_t* ep_desc,
                             const usb_ss_ep_comp_descriptor_t* ss_comp_desc);
  zx_status_t UsbDciDisableEp(uint8_t ep_address);
  zx_status_t UsbDciEpSetStall(uint8_t ep_address);
  zx_status_t UsbDciEpClearStall(uint8_t ep_address);
  size_t UsbDciGetRequestSize();
  zx_status_t UsbDciCancelAll(uint8_t ep_address);

 private:
  enum class SetupState {
    kWaitForSetup,
    kSetupPktProcessInProgress,
    kDataStageXfer,
    kDataStageRecv,
    kStatusStageXfer,
    kStatusStageRecv,
  };

  enum class DeviceState {
    kUsbStateNotattached,
    kUsbStateAttached,
    kUsbStatePowered,
    kUsbStateReconnecting,
    kUsbStateUnauthenticated,
    kUsbStateDefault,
    kUsbStateAddress,
    kUsbStateConfigured,
    kUsbStateSuspended,
  };

  enum class CmdType {
    kCrgCmdIintEp0,
    kCrgCmdUpdateEp0Cfg,
    kCrgCmdSetAddr,
    kCrgCmdSendDevNotification,
    kCrgCmdConfigEp,
    kCrgCmdSetHalt,
    kCrgCmdClearHalt,
    kCrgCmdResetSeqnum,
    kCrgCmdStopEp,
    kCrgCmdSetTrDqptr,
    kCrgCmdForceFlowControl,
    kCrgCmdReqLdmExchange,
  };

  enum class EpState {
    kEpStateDisabled,
    kEpStateRunning,
    kEpStateHalted,
    kEpStateStopped,
  };

  enum class TrbCmplCode {
    kCmplCodeInvalid,
    kCmplCodeSuccess,
    kCmplCodeDataBufferErr,
    kCmplCodeBabbleDetectedErr,
    kCmplCodeUsbTransErr,
    kCmplCodeTrbErr,
    kCmplCodeTrbStall,
    kCmplCodeInvalidStreamTypeErr = 10,
    kCmplCodeShortPkt = 13,
    kCmplCodeRingUnderrun,
    kCmplCodeRingOverrun,
    kCmplCodeEventRingFullErr = 21,
    kCmplCodeStopped = 26,
    kCmplCodeStoppedLengthInvalid = 27,
    kCmplCodeIsochBufferOverrun = 31,
    kCmplCodeProtocolStall = 192,
    kCmplCodeSetupTagMismatch = 193,
    kCmplCodeHalted = 194,
    kCmplCodeHaltedLengthInvalid = 195,
    kCmplCodeDisabled = 196,
    kCmplCodeDisabledLengthInvalid = 197,
  };

  using Request = usb::BorrowedRequest<void>;
  using RequestQueue = usb::BorrowedRequestQueue<void>;

  struct SetupPacket {
    usb_setup_t usbctrlreq;
    uint16_t setup_tag;
  };

  // DMA buffer
  struct BufferInfo {
    zx_handle_t vmo_handle;
    zx_handle_t pmt_handle;
    void* vaddr;
    zx_paddr_t phys;
    zx_off_t vmo_offset;
    size_t len;
  };

  // Event Ring Segment Table
  struct ErstData {
    uint32_t seg_addr_lo;
    uint32_t seg_addr_hi;
    uint32_t seg_size;
    uint32_t rsvd;
  };

  // Transfer Request Block
  struct TRBlock {
    uint32_t dw0;  // Data word 1
    uint32_t dw1;  // Data word 2
    uint32_t dw2;  // Data word 3
    uint32_t dw3;  // Data word 4
  } __PACKED;

  struct Endpoint {
    // Requests waiting to be processed.
    RequestQueue queued_reqs __TA_GUARDED(lock);
    // Request currently being processed.
    usb_request_t* current_req __TA_GUARDED(lock) = nullptr;

    // Transfer ring for current usb request
    uint32_t req_length_left = 0;
    uint32_t trbs_needed;
    bool all_trbs_queued;

    // Values for current USB request
    uint32_t req_offset = 0;
    uint32_t req_xfersize = 0;
    uint32_t req_length = 0;
    zx_paddr_t phys = 0;
    bool zlp = false;

    // Used for synchronizing endpoint state and ep specific hardware registers.
    // This should be acquired before CrgUdc.lock_ if acquiring both locks.
    fbl::Mutex lock;

    uint16_t max_packet_size = 0;
    uint8_t ep_num = 0;
    bool enabled = false;
    // Endpoint type: control, bulk, interrupt or isochronous
    uint8_t type = 0;
    bool dir_out = false;
    bool dir_in = false;

    BufferInfo dma_buf;
    TRBlock* first_trb;
    TRBlock* last_trb;
    TRBlock* enq_pt;
    TRBlock* deq_pt;
    uint8_t pcs;
    bool transfer_ring_full = false;
    EpState ep_state = EpState::kEpStateDisabled;
  };

  struct UdcEvent {
    // DMA buffer for event ring segment table
    struct BufferInfo erst;
    struct ErstData* p_erst;
    // DMA buffer for event ring
    struct BufferInfo event_ring;
    struct TRBlock* evt_dq_pt;
    uint8_t CCS;
    struct TRBlock* evt_seg0_last_trb;
  };

  // Endpoint context
  struct EpContext {
    uint32_t dw0;  // Data word 1
    uint32_t dw1;  // Data word 2
    uint32_t dw2;  // Data word 3
    uint32_t dw3;  // Data word 4
  } __PACKED;

  CrgUdc(const CrgUdc&) = delete;
  CrgUdc& operator=(const CrgUdc&) = delete;
  CrgUdc(CrgUdc&&) = delete;
  CrgUdc& operator=(CrgUdc&&) = delete;

  // 1. Sets the controller to device role and resets this controller
  // 2. Allocates dma buffer for event ring and device context
  // 3. Allocates dma buffer for transfer ring of EP0
  zx_status_t InitController();

  // Updates the connection status when the port link status is changed
  void SetConnected(bool connected);

  // Handles transfer complete events for endpoints other than endpoint zero
  void HandleTransferComplete(uint8_t ep_num);

  // Queues the next usb request when the current request was completed, calling
  // this function requires holding mutex 'ep->lock' exclusively
  void QueueNextRequest(Endpoint* ep) __TA_REQUIRES(ep->lock);

  // Builds the TRB and starts the DMA transfer, calling this function requires
  // holding mutex 'ep->lock' exclusively
  void StartTransfer(Endpoint* ep, uint32_t length) __TA_REQUIRES(ep->lock);

  // Handles the setup request in enumeration phase
  // out_actual: actual request data length
  zx_status_t HandleSetupRequest(size_t* out_actual);
  // Configures the device address in enumeration phase
  void SetAddress(uint8_t address);

  inline fdf::MmioBuffer* get_mmio() { return &*mmio_; }

  // Updates the dequeue pointer of transfer ring
  // ep: the endpoint which generates this event
  // event: the transfer event TRB
  void UpdateDequeuePt(Endpoint* ep, TRBlock* event);
  void SetEp0Halt();

  //  Handles the transfer event when the completion code is success
  //  ep: the endpoint which generates this event
  //  event: the transfer event TRB
  void HandleCompletionCode(Endpoint* ep, TRBlock* event);
  void SetEpHalt(Endpoint* ep);

  // Handles the transfer event, checking the completion code status
  // event: the transfer event TRB
  zx_status_t HandleXferEvent(TRBlock* event);
  void SetAddressCallback();

  // Fills the setup status TRB
  // p_trb: the output status stage TRB
  // pcs: the cycle bit to mark the enqueue pointer of the transfer ring
  // set_addr: indicates whether the current status stage TRB is for setting address
  // stall: indicates whether to put the EP0 in protocol stall state
  void SetupStatusTrb(TRBlock* p_trb, uint8_t pcs, uint8_t set_addr, uint8_t stall);

  // Builds the transfer TRB for EP0 setup status stage, then start the DMA transfer
  // ep: the EP0 endpoint
  // set_addr: indicates whether the current status stage TRB is for setting address
  // stall: indicates whether to put the EP0 in protocol stall state
  void BuildEp0Status(Endpoint* ep, uint8_t set_addr, uint8_t stall);

  // Get the free size from the transfer ring
  // trbs_num: the total size of this ring
  // xfer_ring: the start address of this ring
  // enq_pt: the enqueue pointer
  // dq_pt: the dequeue pointer
  uint32_t RoomOnRing(uint32_t trbs_num, TRBlock* xfer_ring, TRBlock* enq_pt, TRBlock* dq_pt);

  // Fills the normal transfer TRB
  // p_trb: the output TRB
  // xfer_len: size of data buffer in bytes
  // buf_addr: pointing to the start address of data buffer associated with this TRB
  // td_size: how many packets still need to be transferred for current TD
  // pcs: cycle bit to mark the enqueue pointer of the transfer ring
  // trb_type: type of this TRB
  // short_pkt: whether generate completion event when short packet occur
  // chain_bit: associate this TRB with the next TRB on the transfer ring
  // intr_on_compl: whether notify software of the completion of this TRB by generating an interrupt
  // setup_stage: flag for setup stage
  // usb_dir: the direction of data transfer
  // isoc: isochronous flag
  // frame_i_d: frame id when the isoc flag was setted
  // SIA: TD is not bound to specific service interval when this bit is asserted
  // AZP: flag for append zero-length packet
  void SetupNormalTrb(TRBlock* p_trb, uint32_t xfer_len, uint64_t buf_addr, uint8_t td_size,
                      uint8_t pcs, uint8_t trb_type, uint8_t short_pkt, uint8_t chain_bit,
                      uint8_t intr_on_compl, bool setup_stage, uint8_t usb_dir, bool isoc,
                      uint16_t frame_i_d, uint8_t SIA, uint8_t AZP);

  // Fills the transfer TRB for setup data stage
  // ep: EP0 endpoint
  // p_trb: the output TRB
  // pcs: cycle bit to mark the enqueue pointer of the transfer ring
  // transfer_length: size of data buffer in bytes
  // td_size: how many packets still need to be transferred for current TD
  // IOC: whether notify software of the completion of this TRB by generating an interrupt
  // AZP: flag for append zero-length packet
  // dir: the direction of data transfer
  // setup_tag: flag for setup stage
  void SetupDataStageTrb(Endpoint* ep, TRBlock* p_trb, uint8_t pcs, uint32_t transfer_length,
                         uint32_t td_size, uint8_t IOC, uint8_t AZP, uint8_t dir,
                         uint16_t setup_tag);

  // Queues the usb request to EP0 transfer ring
  // ep: EP0 endpoint
  // need_trbs_num: TRB number for the current usb request
  void UdcQueueCtrl(Endpoint* ep, uint32_t need_trbs_num);

  // Queues the usb request to EP(x) transfer ring other than EP0
  // ep: EP(x) endpoint
  // xfer_ring_size: size of the transfer ring
  // need_trbs_num: TRB number for the current usb request
  // buffer_length: usb request length
  void UdcQueueTrbs(Endpoint* ep, uint32_t xfer_ring_size, uint32_t need_trbs_num,
                    uint32_t buffer_length);

  // Triggers the doorbell register to start DMA
  // ep_num: physical endpoint number
  void KnockDoorbell(uint8_t ep_num);
  void BuildTransferTd(Endpoint* ep);
  void DisableEp(uint8_t ep_num);
  void HandleEp0TransferComplete();
  void CompletePendingRequest(Endpoint* ep);
  zx_status_t DmaBufferAlloc(BufferInfo* dma_buf, uint32_t buf_size);
  void DmaBufferFree(BufferInfo* dma_buf);

  // Allocates the dma buffer for event ring
  zx_status_t InitEventRing();

  // Allocates the dma buffer for device context
  zx_status_t InitDeviceContext();

  // Issues a command to controller
  // type: command type
  // para0: command parameter 0
  // para1: command parameter 1
  zx_status_t IssueCmd(enum CmdType type, uint32_t para0, uint32_t para1);
  zx_status_t InitEp0();
  void UdcStart();
  bool CableIsConnected();
  bool EventRingEmpty();
  void ClearPortPM();
  zx_status_t UdcReset();

  // Initials the event ring and device context
  zx_status_t ResetDataStruct();
  void UdcReInit();
  void UpdateEp0MaxPacketSize();
  void EnableSetup();

  // Handle port status change event TRB
  zx_status_t HandlePortStatus();
  zx_paddr_t TranTrbDmaToVirt(Endpoint* ep, zx_paddr_t phy);
  zx_paddr_t EventTrbVirtToDma(UdcEvent* event_ring, TRBlock* event);
  zx_status_t PrepareForSetup();
  void QueueSetupPkt(usb_setup_t* setup_pkt, uint16_t setup_tag);

  // Checks the event type and takes corresponding actions
  zx_status_t UdcHandleEvent(TRBlock* event);

  // Picks up the event TRB and updates the dequeue pointer
  zx_status_t ProcessEventRing();

  // Configures the device context according the descriptor
  void EpContextSetup(const usb_endpoint_descriptor_t* ep_desc,
                      const usb_ss_ep_comp_descriptor_t* ss_comp_desc);
  void HandleEp0Setup();

  Endpoint endpoints_[CRG_UDC_MAX_EPS];
  UdcEvent eventrings_[CRG_UDC_EVENT_RING_NUM];
  BufferInfo endpoint_context_;

  // control request request
  SetupPacket ctrl_req_queue_[CTRL_REQ_QUEUE_DEPTH];
  uint8_t ctrl_req_enq_idx_;

  // Used for synchronizing global state
  // and non ep specific hardware registers.
  // Endpoint.lock should be acquired first
  // when acquiring both locks.
  fbl::Mutex lock_;

  zx::bti bti_;
  // DMA buffer for endpoint zero requests
  ddk::IoBuffer ep0_buffer_;
  // Current endpoint zero request
  usb_setup_t cur_setup_ = {};
  uint16_t setup_tag_;
  SetupState setup_state_ = SetupState::kWaitForSetup;
  DeviceState device_state_ = DeviceState::kUsbStateNotattached;
  uint32_t device_speed_ = USB_SPEED_UNDEFINED;

  ddk::PDev pdev_;
  std::optional<ddk::UsbDciInterfaceProtocolClient> dci_intf_;
  std::optional<ddk::UsbPhyProtocolClient> usb_phy_;

  std::optional<fdf::MmioBuffer> mmio_;

  zx::interrupt irq_;
  thrd_t irq_thread_;

  // True if the irq thread may be joined.
  std::atomic_bool thread_joinable_ = false;

  // True if the irq thread should bail on next loop iteration.
  std::atomic_bool thread_terminate_ = false;

  dwc2_metadata_t metadata_;
  bool connected_ = false;
  bool configured_ = false;
  uint32_t dev_addr_ = 0;
  uint8_t set_addr_ = 0;
  uint32_t portsc_on_reconnecting_ = 0;
  uint32_t enabled_eps_num_ = 0;
  // Raw IRQ timestamp from kernel
  zx::time irq_timestamp_;
  // Timestamp we were dispatched at
  zx::time irq_dispatch_timestamp_;
  // Timestamp when we started waiting for the interrupt
  zx::time wait_start_time_;
  bool shutting_down_ __TA_GUARDED(lock_) = false;
};

}  // namespace crg_udc

#endif  // SRC_DEVICES_USB_DRIVERS_CRG_UDC_CRG_UDC_H_
