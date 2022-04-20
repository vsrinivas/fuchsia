// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_DWC3_DWC3_H_
#define SRC_DEVICES_USB_DRIVERS_DWC3_DWC3_H_

#include <lib/ddk/io-buffer.h>
#include <lib/device-protocol/pdev.h>
#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/status.h>
#include <zircon/hw/usb.h>

#include <array>

#include <ddktl/device.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <usb/request-cpp.h>

#include "dwc3-types.h"
#include "fuchsia/hardware/usb/dci/cpp/banjo.h"
#include "fuchsia/hardware/usb/descriptor/cpp/banjo.h"

namespace dwc3 {

class Dwc3;
using Dwc3Type = ddk::Device<Dwc3, ddk::Initializable, ddk::Unbindable>;

class Dwc3 : public Dwc3Type, public ddk::UsbDciProtocol<Dwc3, ddk::base_protocol> {
 public:
  explicit Dwc3(zx_device_t* parent) : Dwc3Type(parent) {}
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Device protocol implementation.
  void DdkInit(ddk::InitTxn txn);
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

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
  static inline const uint32_t kEventBufferSize = zx_system_get_page_size();

  // physical endpoint numbers.  We use 0 and 1 for EP0, and let the device-mode
  // driver use the rest.
  static inline constexpr uint8_t kEp0Out = 0;
  static inline constexpr uint8_t kEp0In = 1;
  static inline constexpr uint8_t kUserEndpointStartNum = 2;
  static inline constexpr size_t kEp0MaxPacketSize = 512;

  static inline constexpr zx::duration kHwResetTimeout{zx::msec(50)};

  using Request = usb::BorrowedRequest<void>;
  using RequestQueue = usb::BorrowedRequestQueue<void>;

  enum class IrqSignal : uint32_t {
    Invalid = 0,
    Exit = 1,
    Wakeup = 2,
  };

  struct Fifo {
    static inline const uint32_t kFifoSize = zx_system_get_page_size();

    zx_status_t Init(zx::bti& bti);
    void Release();

    zx_paddr_t GetTrbPhys(dwc3_trb_t* trb) const {
      ZX_DEBUG_ASSERT((trb >= first) && (trb <= last));
      return buffer.phys() + ((trb - first) * sizeof(*trb));
    }

    ddk::IoBuffer buffer;
    dwc3_trb_t* first{nullptr};    // first TRB in the fifo
    dwc3_trb_t* next{nullptr};     // next free TRB in the fifo
    dwc3_trb_t* current{nullptr};  // TRB for currently pending transaction
    dwc3_trb_t* last{nullptr};     // last TRB in the fifo (link TRB)
  };

  struct Endpoint {
    Endpoint() = default;
    explicit Endpoint(uint8_t ep_num) : ep_num(ep_num) {}

    Endpoint(const Endpoint&) = delete;
    Endpoint& operator=(const Endpoint&) = delete;
    Endpoint(Endpoint&&) = delete;
    Endpoint& operator=(Endpoint&&) = delete;

    static inline constexpr bool IsOutput(uint8_t ep_num) { return (ep_num & 0x1) == 0; }
    static inline constexpr bool IsInput(uint8_t ep_num) { return (ep_num & 0x1) == 1; }

    bool IsOutput() const { return IsOutput(ep_num); }
    bool IsInput() const { return IsInput(ep_num); }

    RequestQueue queued_reqs;             // requests waiting to be processed
    usb_request_t* current_req{nullptr};  // request currently being processed
    uint32_t rsrc_id{0};                  // resource ID for current_req

    const uint8_t ep_num{0};
    uint8_t type{0};  // control, bulk, interrupt or isochronous
    uint8_t interval{0};
    uint16_t max_packet_size{0};
    bool enabled{false};
    // TODO(voydanoff) USB 3 specific stuff here

    bool got_not_ready{false};
    bool stalled{false};
  };

  struct UserEndpoint {
    UserEndpoint() = default;
    UserEndpoint(const UserEndpoint&) = delete;
    UserEndpoint& operator=(const UserEndpoint&) = delete;
    UserEndpoint(UserEndpoint&&) = delete;
    UserEndpoint& operator=(UserEndpoint&&) = delete;

    // Used for synchronizing endpoint state and ep specific hardware registers
    // This should be acquired before Dwc3::lock_ if acquiring both locks.
    fbl::Mutex lock;

    TA_GUARDED(lock) Fifo fifo;
    TA_GUARDED(lock) Endpoint ep;
  };

  // A small helper class which basically allows us to have a collection of user
  // endpoints which is dynamically allocated at startup, but which will never
  // change in size.  std::array is not an option here, as it is sized at
  // compile time, while std::vector would force us to make user endpoints
  // movable objects (which we really don't want to do).  Basically, this is a
  // lot of typing to get a C-style array which knows its size and supports
  // range based iteration.
  class UserEndpointCollection {
   public:
    void Init(size_t count) {
      ZX_DEBUG_ASSERT(count <= (std::numeric_limits<uint8_t>::max() - kUserEndpointStartNum));
      ZX_DEBUG_ASSERT(count_ == 0);
      ZX_DEBUG_ASSERT(endpoints_.get() == nullptr);

      count_ = count;
      endpoints_.reset(new UserEndpoint[count_]);
      for (size_t i = 0; i < count_; ++i) {
        UserEndpoint& uep = endpoints_[i];
        fbl::AutoLock lock(&uep.lock);
        const_cast<uint8_t&>(uep.ep.ep_num) = static_cast<uint8_t>(i) + kUserEndpointStartNum;
      }
    }

    // Standard size and index-operator
    size_t size() const { return count_; }
    UserEndpoint& operator[](size_t ndx) {
      ZX_DEBUG_ASSERT(ndx < count_);
      return endpoints_[ndx];
    }

    // Support for range-based for loops.
    UserEndpoint* begin() { return endpoints_.get() + 0; }
    UserEndpoint* end() { return endpoints_.get() + count_; }
    const UserEndpoint* begin() const { return endpoints_.get() + 0; }
    const UserEndpoint* end() const { return endpoints_.get() + count_; }

   private:
    size_t count_{0};
    std::unique_ptr<UserEndpoint[]> endpoints_;
  };

  struct Ep0 {
    Ep0() : out(kEp0Out), in(kEp0In) {}

    Ep0(const Ep0&) = delete;
    Ep0& operator=(const Ep0&) = delete;
    Ep0(Ep0&&) = delete;
    Ep0& operator=(Ep0&&) = delete;

    enum class State {
      None,
      Setup,        // Queued setup phase
      DataOut,      // Queued data on EP0_OUT
      DataIn,       // Queued data on EP0_IN
      WaitNrdyOut,  // Waiting for not-ready on EP0_OUT
      WaitNrdyIn,   // Waiting for not-ready on EP0_IN
      Status,       // Waiting for status to complete
    };

    fbl::Mutex lock;

    TA_GUARDED(lock) Fifo shared_fifo;
    TA_GUARDED(lock) ddk::IoBuffer buffer;
    TA_GUARDED(lock) State state{Ep0::State::None};
    TA_GUARDED(lock) Endpoint out;
    TA_GUARDED(lock) Endpoint in;
    TA_GUARDED(lock) usb_setup_t cur_setup;  // current setup request
    TA_GUARDED(lock) usb_speed_t cur_speed = USB_SPEED_UNDEFINED;
  };

  constexpr bool is_ep0_num(uint8_t ep_num) { return ((ep_num == kEp0Out) || (ep_num == kEp0In)); }

  UserEndpoint* get_user_endpoint(uint8_t ep_num) {
    if (ep_num >= kUserEndpointStartNum) {
      const uint8_t ndx = ep_num - kUserEndpointStartNum;
      return (ndx < user_endpoints_.size()) ? &user_endpoints_[ndx] : nullptr;
    }
    return nullptr;
  }

  fdf::MmioBuffer* get_mmio() { return &*mmio_; }
  static uint8_t UsbAddressToEpNum(uint8_t addr) {
    return static_cast<uint8_t>(((addr & 0xF) << 1) | !!(addr & USB_DIR_IN));
  }

  zx_status_t AcquirePDevResources();
  zx_status_t Init();
  void ReleaseResources();

  // The IRQ thread and its two top level event decoders.
  int IrqThread();
  void HandleEvent(uint32_t event);
  void HandleEpEvent(uint32_t event);

  [[nodiscard]] zx_status_t SignalIrqThread(IrqSignal signal) {
    if (!irq_bound_to_port_) {
      return ZX_ERR_BAD_STATE;
    }

    zx_port_packet_t pkt{
        .key = 0,
        .type = ZX_PKT_TYPE_USER,
        .status = ZX_OK,
    };
    pkt.user.u32[0] = static_cast<std::underlying_type_t<decltype(signal)>>(signal);

    return irq_port_.queue(&pkt);
  }

  IrqSignal GetIrqSignal(const zx_port_packet_t& pkt) {
    if (pkt.type != ZX_PKT_TYPE_USER) {
      return IrqSignal::Invalid;
    }
    return static_cast<IrqSignal>(pkt.user.u32[0]);
  }

  // Handlers for global events posted to the event buffer by the controller HW.
  void HandleResetEvent() TA_EXCL(lock_);
  void HandleConnectionDoneEvent() TA_EXCL(lock_);
  void HandleDisconnectedEvent() TA_EXCL(lock_);

  // Handlers for end-point specific events posted to the event buffer by the controller HW.
  void HandleEpTransferCompleteEvent(uint8_t ep_num) TA_EXCL(lock_);
  void HandleEpTransferNotReadyEvent(uint8_t ep_num, uint32_t stage) TA_EXCL(lock_);
  void HandleEpTransferStartedEvent(uint8_t ep_num, uint32_t rsrc_id) TA_EXCL(lock_);

  [[nodiscard]] zx_status_t CheckHwVersion() TA_REQ(lock_);
  [[nodiscard]] zx_status_t ResetHw() TA_REQ(lock_);
  void StartEvents() TA_REQ(lock_);
  void SetDeviceAddress(uint32_t address) TA_REQ(lock_);

  // EP0 stuff
  zx_status_t Ep0Init() TA_EXCL(lock_);
  void Ep0Reset() TA_EXCL(lock_);
  void Ep0Start() TA_EXCL(lock_);
  void Ep0QueueSetupLocked() TA_REQ(ep0_.lock) TA_EXCL(lock_);
  void Ep0StartEndpoints() TA_REQ(ep0_.lock) TA_EXCL(lock_);
  zx::status<size_t> HandleEp0Setup(const usb_setup_t& setup, void* buffer, size_t length)
      TA_REQ(ep0_.lock) TA_EXCL(lock_);
  void HandleEp0TransferCompleteEvent(uint8_t ep_num) TA_EXCL(lock_, ep0_.lock);
  void HandleEp0TransferNotReadyEvent(uint8_t ep_num, uint32_t stage) TA_EXCL(lock_, ep0_.lock);

  // General EP stuff
  void EpEnable(const Endpoint& ep, bool enable) TA_EXCL(lock_);
  void EpSetConfig(Endpoint& ep, bool enable) TA_EXCL(lock_);
  zx_status_t EpSetStall(Endpoint& ep, bool stall) TA_EXCL(lock_);
  void EpStartTransfer(Endpoint& ep, Fifo& fifo, uint32_t type, zx_paddr_t buffer, size_t length,
                       bool send_zlp) TA_EXCL(lock_);
  void EpEndTransfers(Endpoint& ep, zx_status_t reason) TA_EXCL(lock_);
  void EpReadTrb(Endpoint& ep, Fifo& fifo, const dwc3_trb_t* src, dwc3_trb_t* dst) TA_EXCL(lock_);

  // Methods specific to user endpoints
  void UserEpQueueNext(UserEndpoint& uep) TA_REQ(uep.lock) TA_EXCL(lock_);
  zx_status_t UserEpCancelAll(UserEndpoint& uep) TA_EXCL(lock_, uep.lock);

  // Cancel all currently in flight requests, and return a list of requests
  // which were in-flight.  Note that these requests have not been completed
  // yet.  It is the responsibility of the caller to (eventually) take care of
  // this once the lock has been dropped.
  [[nodiscard]] RequestQueue UserEpCancelAllLocked(UserEndpoint& uep) TA_REQ(uep.lock)
      TA_EXCL(lock_);

  // Commands
  void CmdStartNewConfig(const Endpoint& ep, uint32_t rsrc_id) TA_EXCL(lock_);
  void CmdEpSetConfig(const Endpoint& ep, bool modify) TA_EXCL(lock_);
  void CmdEpTransferConfig(const Endpoint& ep) TA_EXCL(lock_);
  void CmdEpStartTransfer(const Endpoint& ep, zx_paddr_t trb_phys) TA_EXCL(lock_);
  void CmdEpEndTransfer(const Endpoint& ep) TA_EXCL(lock_);
  void CmdEpSetStall(const Endpoint& ep) TA_EXCL(lock_);
  void CmdEpClearStall(const Endpoint& ep) TA_EXCL(lock_);

  // Start to operate in peripheral mode.
  void StartPeripheralMode() TA_EXCL(lock_);
  void ResetConfiguration() TA_EXCL(lock_);

  fbl::Mutex lock_;
  fbl::Mutex dci_lock_;

  ddk::PDev pdev_;

  TA_GUARDED(dci_lock_) std::optional<ddk::UsbDciInterfaceProtocolClient> dci_intf_;

  std::optional<ddk::MmioBuffer> mmio_;

  zx::bti bti_;
  bool has_pinned_memory_{false};

  zx::interrupt irq_;
  zx::port irq_port_;
  bool irq_bound_to_port_{false};

  thrd_t irq_thread_;
  std::atomic<bool> irq_thread_started_{false};

  ddk::IoBuffer event_buffer_;
  Ep0 ep0_;
  UserEndpointCollection user_endpoints_;

  RequestQueue pending_completions_;

  // TODO(johngro): What lock protects this?  Right now, it is effectively
  // endpoints_[0].lock(), but how do we express this?
  bool configured_ = false;
};

}  // namespace dwc3

#endif  // SRC_DEVICES_USB_DRIVERS_DWC3_DWC3_H_
