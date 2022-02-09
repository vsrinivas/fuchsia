// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_XHCI_USB_XHCI_H_
#define SRC_DEVICES_USB_DRIVERS_XHCI_USB_XHCI_H_

#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <fuchsia/hardware/usb/bus/cpp/banjo.h>
#include <fuchsia/hardware/usb/hci/cpp/banjo.h>
#include <fuchsia/hardware/usb/phy/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/executor.h>
#include <lib/device-protocol/pci.h>
#include <lib/device-protocol/pdev.h>
#include <lib/fit/function.h>
#include <lib/fpromise/single_threaded_executor.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/mmio/mmio.h>
#include <lib/sync/completion.h>
#include <lib/synchronous-executor/executor.h>
#include <lib/zx/profile.h>
#include <threads.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <thread>

#include <ddktl/device.h>
#include <fbl/array.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include "lib/dma-buffer/buffer.h"
#include "xhci-device-state.h"
#include "xhci-enumeration.h"
#include "xhci-event-ring.h"
#include "xhci-hub.h"
#include "xhci-interrupter.h"
#include "xhci-port-state.h"
#include "xhci-transfer-ring.h"
#include "zircon/system/ulib/inspect/include/lib/inspect/cpp/vmo/types.h"

namespace usb_xhci {

inline void InvalidatePageCache(void* addr, uint32_t options) {
  uintptr_t page = reinterpret_cast<uintptr_t>(addr);
  page = fbl::round_down(page, static_cast<uintptr_t>(zx_system_get_page_size()));
  zx_cache_flush(reinterpret_cast<void*>(page), zx_system_get_page_size(), options);
}

// Inspect values for the xHCI driver.
struct Inspect {
  inspect::Inspector inspector;
  inspect::Node root;
  inspect::UintProperty hci_version;
  inspect::UintProperty max_device_slots;
  inspect::UintProperty max_interrupters;
  inspect::UintProperty max_ports;
  inspect::BoolProperty has_64_bit_addressing;
  inspect::UintProperty context_size_bytes;

  void Init(uint16_t hci_version, HCSPARAMS1& hcs1, HCCPARAMS1& hcc1);
};

// This is the main class for the USB XHCI host controller driver.
// Refer to 3.1 for general architectural information on xHCI.
class UsbXhci;
using UsbXhciType = ddk::Device<UsbXhci, ddk::Initializable, ddk::Suspendable, ddk::Unbindable>;
class UsbXhci : public UsbXhciType, public ddk::UsbHciProtocol<UsbXhci, ddk::base_protocol> {
 public:
  explicit UsbXhci(zx_device_t* parent, std::unique_ptr<dma_buffer::BufferFactory> buffer_factory)
      : UsbXhciType(parent),
#ifdef ENABLE_DFV2
        // TODO(fxbug.dev/93333): Remove this when DFv2 has stabilised.
        pci_(parent),
#else
        pci_(parent, "pci"),
#endif
        pdev_(parent),
        buffer_factory_(std::move(buffer_factory)),
        ddk_interaction_loop_(&kAsyncLoopConfigNeverAttachToThread),
        ddk_interaction_executor_(ddk_interaction_loop_.dispatcher()) {
  }

  // Constructor for unit testing (to allow interception of MMIO read/write)
  explicit UsbXhci(zx_device_t* parent, ddk::MmioBuffer buffer)
      : UsbXhciType(parent),
        ddk_interaction_loop_(&kAsyncLoopConfigNeverAttachToThread),
        ddk_interaction_executor_(ddk_interaction_loop_.dispatcher()) {}

  // Called by the DDK bind operation.
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Forces an immediate shutdown of the HCI
  // This should only be called for critical errors that cannot
  // be recovered from.
  void Shutdown(zx_status_t status);
  // Device protocol implementation.
  void DdkInit(ddk::InitTxn txn);
  void DdkSuspend(ddk::SuspendTxn txn);
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  // USB HCI protocol implementation.
  // Control TRBs must be run on the primary interrupter. Section 4.9.4.3: secondary interrupters
  // cannot handle them..
  void UsbHciRequestQueue(usb_request_t* usb_request,
                          const usb_request_complete_callback_t* complete_cb);
  void UsbHciSetBusInterface(const usb_bus_interface_protocol_t* bus_intf);
  // Retrieves the max number of device slots supported by this host controller
  size_t UsbHciGetMaxDeviceCount();
  zx_status_t UsbHciEnableEndpoint(uint32_t device_id, const usb_endpoint_descriptor_t* ep_desc,
                                   const usb_ss_ep_comp_descriptor_t* ss_com_desc, bool enable);
  uint64_t UsbHciGetCurrentFrame();
  zx_status_t UsbHciConfigureHub(uint32_t device_id, usb_speed_t speed,
                                 const usb_hub_descriptor_t* desc, bool multi_tt);
  zx_status_t UsbHciHubDeviceAdded(uint32_t device_id, uint32_t port, usb_speed_t speed);
  zx_status_t UsbHciHubDeviceRemoved(uint32_t device_id, uint32_t port);
  zx_status_t UsbHciHubDeviceReset(uint32_t device_id, uint32_t port);
  zx_status_t UsbHciResetEndpoint(uint32_t device_id, uint8_t ep_address);
  zx_status_t UsbHciResetDevice(uint32_t hub_address, uint32_t device_id);
  size_t UsbHciGetMaxTransferSize(uint32_t device_id, uint8_t ep_address);
  zx_status_t UsbHciCancelAll(uint32_t device_id, uint8_t ep_address);
  size_t UsbHciGetRequestSize();

  // Queues a USB request (compatibility shim for usb::CallbackRequest in unit test)
  void RequestQueue(usb_request_t* usb_request,
                    const usb_request_complete_callback_t* complete_cb) {
    UsbHciRequestQueue(usb_request, complete_cb);
  }

  // Queues a request and returns a promise
  fpromise::promise<OwnedRequest, void> UsbHciRequestQueue(OwnedRequest usb_request);

  TRBPromise UsbHciEnableEndpoint(uint32_t device_id, const usb_endpoint_descriptor_t* ep_desc,
                                  const usb_ss_ep_comp_descriptor_t* ss_com_desc);
  TRBPromise UsbHciDisableEndpoint(uint32_t device_id, const usb_endpoint_descriptor_t* ep_desc,
                                   const usb_ss_ep_comp_descriptor_t* ss_com_desc);
  TRBPromise UsbHciResetEndpointAsync(uint32_t device_id, uint8_t ep_address);

  bool Running() const { return running_; }

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

  usb_speed_t GetPortSpeed(uint8_t port_id) const;

  size_t slot_size_bytes() const { return slot_size_bytes_; }

  // Returns the value in the CAPLENGTH register
  uint8_t CapLength() const { return cap_length_; }

  static uint8_t DeviceIdToSlotId(uint8_t device_id) { return static_cast<uint8_t>(device_id + 1); }

  static uint8_t SlotIdToDeviceId(uint8_t slot_id) { return static_cast<uint8_t>(slot_id - 1); }

  void SetDeviceInformation(uint8_t slot, uint8_t port, const std::optional<HubInfo>& hub);

  // MfIndex wrapper handler. The previous driver used this to increment
  // the mfindex wrap value. This caused race conditions that resulted
  // in incorrect values for the mfindex wrap value.
  // This function is left empty as a placeholder
  // for future use of the MFIndex wrap event. It is unclear at the moment
  // what, if anything this callback should be used for.
  void MfIndexWrapped() {}

  zx::profile& get_profile() { return profile_; }

  uint8_t GetPortCount() { return static_cast<uint8_t>(params_.MaxPorts()); }

  // Resets a port. Not to be confused with ResetDevice.
  void ResetPort(uint16_t port);

  // Waits for xHCI bringup to complete
  void WaitForBringup() { sync_completion_wait(&bringup_, ZX_TIME_INFINITE); }

  CommandRing* GetCommandRing() { return &command_ring_; }

  DeviceState* GetDeviceState() { return device_state_.get(); }
  DeviceState* GetDeviceState(uint32_t device_id) {
    auto* state = &device_state_[device_id];
    {
      fbl::AutoLock _(&state->transaction_lock());
      if (state->IsDisconnecting()) {
        return nullptr;
      }
    }
    return state;
  }

  PortState* GetPortState() { return port_state_.get(); }
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

  // Schedules a promise for execution on the executor
  void ScheduleTask(uint16_t target_interrupter, TRBPromise promise) {
    interrupter(target_interrupter).ring().ScheduleTask(std::move(promise));
  }

  // Schedules the promise for execution and synchronously waits for it to complete
  zx_status_t RunSynchronously(uint16_t target_interrupter,
                               fpromise::promise<TRB*, zx_status_t> promise) {
    sync_completion_t completion;
    zx_status_t completion_code;
    auto continuation = promise.then([&](fpromise::result<TRB*, zx_status_t>& result) {
      if (result.is_ok()) {
        completion_code = ZX_OK;
        sync_completion_signal(&completion);
      } else {
        completion_code = result.error();
        sync_completion_signal(&completion);
      }
      return result;
    });
    ScheduleTask(target_interrupter, continuation.box());
    RunUntilIdle(target_interrupter);
    sync_completion_wait(&completion, ZX_TIME_INFINITE);
    return completion_code;
  }

  // Creates a promise that resolves after a timeout
  TRBPromise Timeout(uint16_t target_interrupter, zx::time deadline);

  // Provides a barrier for promises.
  // After this method is invoked, all pending promises on all interrupters will be flushed.
  void RunUntilIdle() {
    for (auto& it : interrupters_) {
      if (it.active()) {
        it.ring().RunUntilIdle();
      }
    }
  }

  // Provides a barrier for promises.
  // After this method is invoked, all pending promises on the target interrupter will be flushed.
  void RunUntilIdle(uint16_t target_interrupter) {
    interrupter(target_interrupter).ring().RunUntilIdle();
  }

  // interrupter(uint32_t i): returns the interrupter with the corresponding index
  Interrupter& interrupter(uint16_t i) { return interrupters_[i]; }

  // Initialization thread method. This is invoked from a separate detached thread
  // when xHCI binds.
  // Returns thrd_success on success, or a thread error from <threads.h> on failure.
  int InitThread();

  const zx::bti& bti() const { return bti_; }

  size_t GetPageSize() const { return page_size_; }

  bool Is32BitController() const { return is_32bit_; }

  // Asynchronously submits a command to the command queue.
  TRBPromise SubmitCommand(const TRB& command, std::unique_ptr<TRBContext> trb_context);

  // Retrieves the current test harness
  void* GetTestHarness() const { return test_harness_; }

  // Sets the test harness
  void SetTestHarness(void* harness) { test_harness_ = harness; }

  dma_buffer::BufferFactory& buffer_factory() const { return *buffer_factory_; }

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(UsbXhci);

  struct UsbRequestState;

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

  template <typename T>
  void PostCallback(T&& callback) {
    ddk_interaction_executor_.schedule_task(fpromise::make_ok_promise().then(
        [this, cb = std::forward<T>(callback)](fpromise::result<void, void>& result) mutable {
          cb(bus_);
        }));
  }

  TRBPromise ConfigureHubAsync(uint32_t device_id, usb_speed_t speed,
                               const usb_hub_descriptor_t* desc, bool multi_tt);

  // UsbHci Helper Functions
  // Queues a control request
  void UsbHciControlRequestQueue(Request request);
  // Queues a normal request
  void UsbHciNormalRequestQueue(Request request);
  TRBPromise UsbHciCancelAllAsync(uint32_t device_id, uint8_t ep_address);
  TRBPromise UsbHciHubDeviceAddedAsync(uint32_t device_id, uint32_t port, usb_speed_t speed);

  // InterrupterMapping: finds an interrupter. Currently finds the interrupter with the least
  // pressure.
  uint16_t InterrupterMapping();

  // Global scheduler lock. This should be held when adding or removing
  // interrupters, and; eventually dynamically assigning transfer rings
  // to interrupters.
  fbl::Mutex scheduler_lock_;

  // This is a high-priority profile used for increasing the priority
  // of the interrupt thread. This is currently necessary to mitigate
  // fxbug.dev/34507, and can be removed once the scheduling problem is fixed.
  zx::profile profile_;
  // Performs the initialization sequence defined in section
  // 4.2 of the xHCI specification.
  zx_status_t Init();

  // PCI protocol client (if x86)
  ddk::Pci pci_;

  // PDev (if ARM)
  ddk::PDev pdev_;

  // MMIO buffer for communicating with the physical hardware
  // Must be optional to allow for asynchronous initialization,
  // since an MmioBuffer has no default constructor.
  std::optional<ddk::MmioBuffer> mmio_;

  // The number of IRQs supported by the HCI
  uint16_t irq_count_;

  // Array of interrupters, which service interrupts from the HCI
  fbl::Array<Interrupter> interrupters_;

  // Pointer to the start of the device context base address array
  // See xHCI section 6.1 for more information.
  uint64_t* dcbaa_;

  // IO buffer for the device context base address array
  std::unique_ptr<dma_buffer::PagedBuffer> dcbaa_buffer_;

  // BTI for retrieving physical memory addresses from IO buffers.
  zx::bti bti_;

  // xHCI scratchpad buffers (see xHCI section 4.20)
  fbl::Array<std::unique_ptr<dma_buffer::ContiguousBuffer>> scratchpad_buffers_;

  // IO buffer for the scratchpad buffer array
  std::unique_ptr<dma_buffer::PagedBuffer> scratchpad_buffer_array_;

  std::unique_ptr<dma_buffer::BufferFactory> buffer_factory_;

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

  // The value in the CAPLENGTH register (see xHCI section 5.3.1)
  uint8_t cap_length_;

  // The last recorded MFINDEX value
  std::atomic<uint32_t> last_mfindex_ = 0;

  // Runtime register offset (see xHCI section 5.3.8)
  RuntimeRegisterOffset runtime_offset_;

  // Status information on connected devices
  fbl::Array<DeviceState> device_state_;

  // Status information for each port in the system
  fbl::Array<PortState> port_state_;

  // HCSPARAMS1 register (see xHCI section 5.3.3)
  HCSPARAMS1 params_;

  // HCCPARAMS1 register (see xHCI section 5.3.6)
  HCCPARAMS1 hcc_;

  // Number of slots supported by the HCI
  size_t max_slots_;

  // The size of a slot entry in bytes
  size_t slot_size_bytes_;

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

  // InitThread Helper Functions and Variables
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
  // Complete initialization of host controller.
  // Called after controller is first reset on startup.
  zx_status_t HciFinalize();
  // Completion event which is signalled when driver initialization finishes
  sync_completion_t init_complete_;
  // Completion which is signalled when the bus interface is bound
  sync_completion_t bus_completion;
  // Completion which is signalled when xHCI enters an operational state
  sync_completion_t bringup_;
  std::optional<thrd_t> init_thread_;
  std::optional<ddk::InitTxn> init_txn_;

  Inspect inspect_;
};

}  // namespace usb_xhci

#endif  // SRC_DEVICES_USB_DRIVERS_XHCI_USB_XHCI_H_
