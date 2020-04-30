// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_XHCI_REWRITE_USB_XHCI_H_
#define SRC_DEVICES_USB_DRIVERS_XHCI_REWRITE_USB_XHCI_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/executor.h>
#include <lib/device-protocol/pci.h>
#include <lib/device-protocol/pdev.h>
#include <lib/fit/function.h>
#include <lib/fit/single_threaded_executor.h>
#include <lib/mmio/mmio.h>
#include <lib/sync/completion.h>
#include <lib/zx/profile.h>
#include <threads.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <thread>

#include <ddktl/device.h>
#include <ddktl/protocol/composite.h>
#include <ddktl/protocol/pci.h>
#include <ddktl/protocol/platform/device.h>
#include <ddktl/protocol/usb/bus.h>
#include <ddktl/protocol/usb/hci.h>
#include <ddktl/protocol/usb/phy.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include "lib/dma-buffer/buffer.h"
#include "registers.h"
#include "synchronous_executor.h"
#include "xhci-context.h"
#include "xhci-device-state.h"
#include "xhci-enumeration.h"
#include "xhci-event-ring.h"
#include "xhci-hub.h"
#include "xhci-interrupter.h"
#include "xhci-port-state.h"
#include "xhci-transfer-ring.h"

namespace usb_xhci {

// Obtains the slot index for a specified endpoint.
__UNUSED static uint8_t XhciEndpointIndex(uint8_t ep_address) {
  if (ep_address == 0)
    return 0;
  uint8_t index = static_cast<uint8_t>(2 * (ep_address & ~USB_ENDPOINT_DIR_MASK));
  if ((ep_address & USB_ENDPOINT_DIR_MASK) == USB_ENDPOINT_OUT)
    index--;
  return index;
}

__UNUSED static uint32_t Log2(uint32_t value) { return 31 - __builtin_clz(value); }

static inline void InvalidatePageCache(void* addr, uint32_t options) {
  uintptr_t page = reinterpret_cast<uintptr_t>(addr);
  page = fbl::round_down(page, static_cast<uintptr_t>(PAGE_SIZE));
  zx_cache_flush(reinterpret_cast<void*>(page), PAGE_SIZE, options);
}

// This is the main class for the USB XHCI host controller driver.
// Refer to 3.1 for general architectural information on xHCI.
class UsbXhci;
using UsbXhciType = ddk::Device<UsbXhci, ddk::Suspendable, ddk::UnbindableNew>;
class UsbXhci : public UsbXhciType, public ddk::UsbHciProtocol<UsbXhci, ddk::base_protocol> {
 public:
  explicit UsbXhci(zx_device_t* parent)
      : UsbXhciType(parent),
        pci_(parent),
        pdev_(parent),
        composite_(parent),
        ddk_interaction_loop_(&kAsyncLoopConfigNeverAttachToThread),
        ddk_interaction_executor_(ddk_interaction_loop_.dispatcher()) {}

  // Constructor for unit testing (to allow interception of MMIO read/write)
  explicit UsbXhci(zx_device_t* parent, ddk::MmioBuffer buffer)
      : UsbXhciType(parent),
        ddk_interaction_loop_(&kAsyncLoopConfigNeverAttachToThread),
        ddk_interaction_executor_(ddk_interaction_loop_.dispatcher()) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Forces an immediate shutdown of the HCI
  // This should only be called for critical errors that cannot
  // be recovered from.
  void Shutdown(zx_status_t status);
  // Device protocol implementation.
  void DdkSuspend(ddk::SuspendTxn txn);
  void DdkUnbindNew(ddk::UnbindTxn txn);

  void DdkRelease();

  // Queues a control request
  void UsbHciControlRequestQueue(Request request);

  // Queues a normal request
  void UsbHciNormalRequestQueue(Request request);

  // USB HCI protocol implementation.
  void UsbHciRequestQueue(usb_request_t* usb_request, const usb_request_complete_t* complete_cb);

  // Queues a USB request (compatibility shim for usb::CallbackRequest in unit test)
  void RequestQueue(usb_request_t* usb_request, const usb_request_complete_t* complete_cb) {
    UsbHciRequestQueue(usb_request, complete_cb);
  }

  // Queues a request and returns a promise
  fit::promise<OwnedRequest, void> UsbHciRequestQueue(OwnedRequest usb_request);

  void UsbHciSetBusInterface(const usb_bus_interface_protocol_t* bus_intf);

  // Retrieves the max number of device slots supported by this host controller
  size_t UsbHciGetMaxDeviceCount();

  zx_status_t UsbHciEnableEndpoint(uint32_t device_id, const usb_endpoint_descriptor_t* ep_desc,
                                   const usb_ss_ep_comp_descriptor_t* ss_com_desc, bool enable);

  TRBPromise UsbHciEnableEndpoint(uint32_t device_id, const usb_endpoint_descriptor_t* ep_desc,
                                  const usb_ss_ep_comp_descriptor_t* ss_com_desc);

  TRBPromise UsbHciDisableEndpoint(uint32_t device_id, const usb_endpoint_descriptor_t* ep_desc,
                                   const usb_ss_ep_comp_descriptor_t* ss_com_desc);

  uint64_t UsbHciGetCurrentFrame();

  zx_status_t UsbHciConfigureHub(uint32_t device_id, usb_speed_t speed,
                                 const usb_hub_descriptor_t* desc, bool multi_tt);

  zx_status_t UsbHciHubDeviceAdded(uint32_t device_id, uint32_t port, usb_speed_t speed);

  zx_status_t UsbHciHubDeviceRemoved(uint32_t device_id, uint32_t port);

  zx_status_t UsbHciHubDeviceReset(uint32_t device_id, uint32_t port);

  zx_status_t UsbHciResetEndpoint(uint32_t device_id, uint8_t ep_address);

  bool Running() { return running_; }

  zx_status_t UsbHciResetDevice(uint32_t hub_address, uint32_t device_id);

  size_t UsbHciGetMaxTransferSize(uint32_t device_id, uint8_t ep_address);

  zx_status_t UsbHciCancelAll(uint32_t device_id, uint8_t ep_address);

  TRBPromise UsbHciCancelAllAsync(uint32_t device_id, uint8_t ep_address);

  size_t UsbHciGetRequestSize();

  // Offlines a device slot, removing its device node from the topology.
  TRBPromise DeviceOffline(uint32_t slot, TRB* continuation);

  // Onlines a device, publishing a device node in the DDK.
  zx_status_t DeviceOnline(uint32_t slot, uint16_t port, usb_speed_t speed);

  // Returns whether or not a device is connected to the root hub.
  // Always returns true for devices attached via a hub.
  bool IsDeviceConnected(uint8_t slot) {
    auto& state = device_state_[slot - 1];
    fbl::AutoLock _(&state.transaction_lock());
    return !state.IsDisconnecting();
  }

  // Disables a slot
  TRBPromise DisableSlotCommand(uint32_t slot_id);

  TRBPromise EnableSlotCommand();

  TRBPromise AddressDeviceCommand(uint8_t slot_id, uint8_t port_id, std::optional<HubInfo> hub_info,
                                  bool bsr);

  TRBPromise AddressDeviceCommand(uint8_t slot_id);

  TRBPromise SetMaxPacketSizeCommand(uint8_t slot_id, uint8_t bMaxPacketSize0);

  usb_speed_t GetDeviceSpeed(uint8_t slot_id);

  uint8_t GetPortSpeed(uint8_t port_id) const;

  // Returns the CSZ bit from HCCPARAMS1
  bool CSZ() const { return hcc_.CSZ(); }

  // Returns the value in the CAPLENGTH register
  uint8_t CapLength() const { return cap_length_; }

  uint8_t DeviceIdToSlotId(uint8_t device_id) const { return static_cast<uint8_t>(device_id + 1); }

  void SetDeviceInformation(uint8_t slot, uint8_t port, const std::optional<HubInfo>& hub);

  // MfIndex wrapper handler. The previous driver used this to increment
  // the mfindex wrap value. This caused race conditions that resulted
  // in incorrect values for the mfindex wrap value.
  // This function is left empty as a placeholder
  // for future use of the MFIndex wrap event. It is unclear at the moment
  // what, if anything this callback should be used for.
  void MfIndexWrapped() {}

  zx::profile& get_profile() { return profile_; }

  template <typename T>
  zx_status_t PostCallback(T callback) {
    ddk_interaction_executor_.schedule_task(fit::make_ok_promise().then(
        [this, cb = std::move(callback)](fit::result<void, void>& result) mutable { cb(bus_); }));
    return ZX_OK;
  }

  uint8_t get_port_count() { return static_cast<uint8_t>(params_.MaxPorts()); }

  TRBPromise UsbHciHubDeviceAddedAsync(uint32_t device_id, uint32_t port, usb_speed_t speed);

  TRBPromise ConfigureHubAsync(uint32_t device_id, usb_speed_t speed,
                               const usb_hub_descriptor_t* desc, bool multi_tt);

  // Resets a port. Not to be confused with ResetDevice.
  void ResetPort(uint16_t port);

  // Waits for xHCI bringup to complete
  void WaitForBringup() { sync_completion_wait(&bringup_, ZX_TIME_INFINITE); }

  CommandRing* get_command_ring() { return &command_ring_; }

  DeviceState* get_device_state() { return device_state_.get(); }

  PortState* get_port_state() { return port_state_.get(); }
  // Indicates whether or not the controller supports cache coherency
  // for transfers.
  bool HasCoherentCache() const { return has_coherent_cache_; }
  // Indicates whether or not the controller has a cache coherent state.
  // Currently, this is the same as HasCoherentCache, but the spec
  // leaves open the possibility that a controller may have a coherent cache,
  // but not a coherent state.
  bool HasCoherentState() const { return HasCoherentCache(); }
  // Returns whether or not we are running in Qemu. Quirks need to be applied
  // where the emulated controller violates the xHCI specification.
  bool IsQemu() { return qemu_quirk_; }

  // Converts a TRB promise to a USB request promise
  fit::promise<OwnedRequest, void> TRBToUSBRequestPromise(TRBPromise promise, OwnedRequest request);

  // Converts a USB request promise to a TRB promise. The returned TRB pointer
  // will be nullptr.
  TRBPromise USBRequestToTRBPromise(fit::promise<OwnedRequest, void> promise);

  TRBPromise ResultToTRBPromise(const fit::result<TRB*, zx_status_t>& result) const {
    return fit::make_result_promise(result).box();
  }

  fit::promise<OwnedRequest, void> ResultToUSBRequestPromise(
      fit::result<OwnedRequest, void> result) {
    return fit::make_result_promise(std::move(result)).box();
  }

  // Schedules a promise for execution on the executor
  void ScheduleTask(TRBPromise promise) {
    interrupters_[0].ring().ScheduleTask(std::move(promise));
  }

  // Schedules the promise for execution and synchronously waits for it to complete
  zx_status_t TRBWait(fit::promise<TRB*, zx_status_t> promise) {
    sync_completion_t completion;
    zx_status_t completion_code;
    auto continuation = promise.then([&](fit::result<TRB*, zx_status_t>& result) {
      if (result.is_ok()) {
        completion_code = ZX_OK;
        sync_completion_signal(&completion);
      } else {
        completion_code = result.error();
        sync_completion_signal(&completion);
      }
      return result;
    });
    ScheduleTask(continuation.box());
    RunUntilIdle();
    sync_completion_wait(&completion, ZX_TIME_INFINITE);
    return completion_code;
  }

  // Returns an empty promise
  TRBPromise EmptyPromise() {
    fit::bridge<TRB*, zx_status_t> bridge;
    auto promise = bridge.consumer.promise().then(
        [](fit::result<TRB*, zx_status_t>& result) { return result; });
    bridge.completer.complete_ok(nullptr);
    return promise.box();
  }

  // Creates a promise that resolves after a timeout
  TRBPromise Timeout(zx::time deadline) { return interrupters_[0].Timeout(deadline); }

  // Provides a barrier for promises.
  // After this method is invoked,
  // all pending promises will be flushed.
  void RunUntilIdle() { interrupters_[0].ring().RunUntilIdle(); }

  // Initialization thread method. This is invoked from a separate detached thread
  // when xHCI binds
  zx_status_t InitThread();

  // Resets the xHCI controller. This should only be called during initialization.
  void ResetController();

  // Initializes PCI
  zx_status_t InitPci();

  // Initializes MMIO
  zx_status_t InitMmio();

  // Performs the handoff from the BIOS to the xHCI driver
  void BiosHandoff();

  // Performs platform-specific initialization functions
  void InitQuirks();

  zx_status_t HciFinalize();

  const zx::bti& bti() const { return bti_; }

  size_t get_page_size() const { return page_size_; }

  bool get_is_32_bit_controller() const { return is_32bit_; }

  // Asynchronously submits a command to the command queue.
  TRBPromise SubmitCommand(const TRB& command, std::unique_ptr<TRBContext> trb_context);

  // Retrieves the current test harness
  void* get_test_harness() const { return test_harness_; }

  // Sets the test harness
  void set_test_harness(void* harness) { test_harness_ = harness; }

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(UsbXhci);

  // Tracks the state of a USB request
  // This state is passed around to the various transfer request
  // queueing methods, and its lifetime should not outlast
  // the lifetime of the transaction. This struct should be
  // stack-allocated.
  // None of the values in this field should be accessed
  // after the USB transaction has been sent to hardware.
  struct UsbRequestState {
    // Invokes the completion callback if the request was marked as completed.
    // Returns true if the completer was called, false otherwise.
    bool Complete();

    // Request status
    zx_status_t status;

    // Number of bytes transferred
    size_t bytes_transferred = 0;

    // Whether or not the request is complete
    bool complete = false;

    // Size of the slot
    size_t slot_size;

    // Max burst size (value of the max burst size register + 1, since it is zero-based)
    uint32_t burst_size;

    // Max packet size
    uint32_t max_packet_size;

    // True if the current transfer is isochronous
    bool is_isochronous_transfer;

    // First TRB in the transfer
    // This is owned by the transfer ring.
    TRB* first_trb = nullptr;

    // Value to set the cycle bit on the first TRB to
    bool first_cycle;

    // TransferRing transaction state
    TransferRing::State transaction;

    // The transfer ring to post transactions to
    // This is owned by UsbXhci and is valid for
    // the duration of this transaction.
    TransferRing* transfer_ring;

    // Index of the transfer ring
    uint8_t index;

    // Transfer context
    std::unique_ptr<TRBContext> context;

    // The number of packets in the transfer
    size_t packet_count = 0;

    // The slot ID of the transfer
    uint8_t slot;

    // Total length of the transfer
    uint32_t total_len = 0;

    // The setup TRB
    // This is owned by the transfer ring.
    TRB* setup;

    // The interrupter to use
    uint8_t interrupter = 0;

    // Pointer to the status TRB
    // This is owned by the transfer ring.
    TRB* status_trb_ptr = nullptr;

    // Cycle bit of the setup TRB during the allocation phase
    bool setup_cycle;
    // Last TRB in the transfer
    // This is owned by the transfer ring.
    TRB* last_trb;
  };

  // Waits for a time interval when it is suitable to schedule an isochronous transfer
  void WaitForIsochronousReady(UsbRequestState* state);

  // Starts a normal transfer
  void StartNormalTransaction(UsbRequestState* state);

  // Continues a normal transfer
  void ContinueNormalTransaction(UsbRequestState* state);

  // Commits a normal transfer
  void CommitNormalTransaction(UsbRequestState* state);

  // Performs the allocation phase of the control request
  // (allocates TRBs for the request)
  void ControlRequestAllocationPhase(UsbRequestState* state);

  // Performs the status phase of a control request
  static void ControlRequestStatusPhase(UsbRequestState* state);

  // Performs the data transfer phase of a control request
  void ControlRequestDataPhase(UsbRequestState* state);

  // Performs the setup phase of a control request
  static void ControlRequestSetupPhase(UsbRequestState* state);

  // Starts the transfer of a control request
  void ControlRequestCommit(UsbRequestState* state);

  // Global scheduler lock. This should be held when adding or removing
  // interrupters, and; eventually dynamically assigning transfer rings
  // to interrupters.
  fbl::Mutex scheduler_lock_;

  // This is a high-priority profile used for increasing the priority
  // of the interrupt thread. This is currently necessary to mitigate
  // fxb/34507, and can be removed once the scheduling problem is fixed.
  zx::profile profile_;
  // Performs the initialization sequence defined in section
  // 4.2 of the xHCI specification.
  zx_status_t Init();

  // PCI protocol client (if x86)
  ddk::Pci pci_;

  // PDev (if ARM)
  ddk::PDev pdev_;

  // Composite device protocol client used for communicating with the USB PHY
  // on certain ARM boards which support USB OTG.
  ddk::CompositeProtocolClient composite_;

  // MMIO buffer for communicating with the physical hardware
  // Must be optional to allow for asynchronous initialization,
  // since an MmioBuffer has no default constructor.
  std::optional<ddk::MmioBuffer> mmio_;

  // The number of IRQs supported by the HCI
  uint32_t irq_count_;

  // Array of interrupters, which service interrupts from the HCI
  std::unique_ptr<Interrupter[]> interrupters_;

  // Pointer to the start of the device context base address array
  // See xHCI section 6.1 for more information.
  uint64_t* dcbaa_;

  // IO buffer for the device context base address array
  std::optional<dma_buffer::PagedBuffer> dcbaa_buffer_;

  // BTI for retrieving physical memory addresses from IO buffers.
  zx::bti bti_;

  // xHCI scratchpad buffers (see xHCI section 4.20)
  std::unique_ptr<std::optional<dma_buffer::ContiguousBuffer>[]> scratchpad_buffers_;

  // IO buffer for the scratchpad buffer array
  std::optional<dma_buffer::PagedBuffer> scratchpad_buffer_array_;

  // Page size of the HCI
  size_t page_size_;

  // xHCI command ring (see xHCI section 4.6.1)
  CommandRing command_ring_;

  // Whether or not the host controller is 32 bit
  bool is_32bit_ = false;

  // Whether or not the HCI's cache is coherent with the CPU
  bool has_coherent_cache_ = false;

  // Offset to the doorbells. See xHCI section 5.3.7
  DoorbellOffset doorbell_offset_;

  // The number of enabled interrupters
  uint32_t active_interrupters_ __TA_GUARDED(scheduler_lock_);

  // The value in the CAPLENGTH register (see xHCI section 5.3.1)
  uint8_t cap_length_;

  // The last recorded MFINDEX value
  std::atomic<uint32_t> last_mfindex_ = 0;

  // Runtime register offset (see xHCI section 5.3.8)
  RuntimeRegisterOffset runtime_offset_;

  // Status information on connected devices
  std::unique_ptr<DeviceState[]> device_state_;

  // Status information for each port in the system
  std::unique_ptr<PortState[]> port_state_;

  // Completion which is signalled when the bus interface is bound
  sync_completion_t bus_completion;

  // Completion which is signalled when xHCI enters an operational state
  sync_completion_t bringup_;

  // HCSPARAMS1 register (see xHCI section 5.3.3)
  HCSPARAMS1 params_;

  // HCCPARAMS1 register (see xHCI section 5.3.6)
  HCCPARAMS1 hcc_;

  // Number of slots supported by the HCI
  size_t max_slots_;

  // Whether or not we are running on Qemu
  bool qemu_quirk_ = false;

  // Number of times the MFINDEX has wrapped
  std::atomic_uint64_t wrap_count_ = 0;

  // USB bus protocol client
  ddk::UsbBusInterfaceProtocolClient bus_;

  async::Loop ddk_interaction_loop_;

  // Pending DDK callbacks that need to be ran on the dedicated DDK interaction thread
  async::Executor ddk_interaction_executor_;

  // Thread for interacting with the Devhost thread (main event loop)
  std::optional<thrd_t> ddk_interaction_thread_;

  // Whether or not the HCI instance is currently active
  std::atomic_bool running_ = true;

  // PHY protocol
  ddk::UsbPhyProtocolClient phy_;

  // Pointer to the test harness when being called from a unit test
  // This is an opaque pointer that is managed by the test.
  void* test_harness_;

  // Completion event which is signalled when driver initialization finishes
  sync_completion_t init_complete_;

  std::optional<thrd_t> init_thread_;
};

}  // namespace usb_xhci

#endif  // SRC_DEVICES_USB_DRIVERS_XHCI_REWRITE_USB_XHCI_H_
