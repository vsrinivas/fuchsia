// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Theory of operation:
// This file contains unit tests for xhci-enumeration.cc.
// In order to test this code, it is necessary to fake out
// everything that it interacts with (most of which is in usb-xhci.cc,
// while some of it is event ring related).
// Due to the use of TRBs to pass asynchronous state around (which are normally
// owned by the event ring), the test harness ends up owning all of the TRBs
// associated with a TRBContext. The test harness is responsible for the creation
// and destruction of TRBs, since there is no actual event ring present (normally these would
// reside inside of a DMA buffer that is shared with hardware.
// In the future -- we may want to remove this tight coupling, but this is difficult
// due to inability to pass un-instantiated templates between different object files in C++.
// This may later be solved by C++ modules, at which point we can have each callback return a
// unique template instantiation instead of passing TRBs around to everything (resulting in
// tight coupling between the event ring, UsbXhci class, the transfer ring, and the enumerator).

#include "xhci-enumeration.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>

#include <atomic>
#include <thread>

#include <fake-dma-buffer/fake-dma-buffer.h>
#include <zxtest/zxtest.h>

#include "usb-xhci.h"
#include "xhci-event-ring.h"

namespace usb_xhci {

constexpr size_t kMaxSlabs = -1;
constexpr bool kAllocInitial = true;

struct FakeTRB : TRB {
  enum class Op {
    DisableSlot,
    EnableSlot,
    SetMaxPacketSize,
    AddressDevice,
    OnlineDevice,
    ShutdownController,
    SetDeviceInformation,
    Timeout,
  } Op;
  uint32_t slot;
  uint8_t max_packet_size;
  uint16_t port;
  usb_speed_t speed;
  zx_status_t status;
  zx::time deadline;
  std::optional<HubInfo> hub_info;
  bool bsr;
  static FakeTRB* FromTRB(TRB* trb) { return static_cast<FakeTRB*>(trb); }
};

struct TestState {
  TestState() : trb_context_allocator_(kMaxSlabs, kAllocInitial) {}
  fbl::DoublyLinkedList<std::unique_ptr<TRBContext>> pending_operations;
  AllocatorType trb_context_allocator_;
  uint64_t token = 0;
  uint8_t slot = 1;
  usb_speed_t speeds[32] = {};

  ~TestState() {
    while (!pending_operations.is_empty()) {
      auto op = pending_operations.pop_front();
      if (op->completer.has_value()) {
        op->completer->complete_error(ZX_ERR_IO_NOT_PRESENT);
      }
    }
  }
};

void EventRing::ScheduleTask(fit::promise<TRB*, zx_status_t> promise) {
  {
    auto continuation = promise.then([=](fit::result<TRB*, zx_status_t>& result) {
      if (result.is_error()) {
        if (result.error() == ZX_ERR_BAD_STATE) {
          hci_->Shutdown(ZX_ERR_BAD_STATE);
        }
      }
      return result;
    });
    executor_.schedule_task(std::move(continuation));
  }
}

void EventRing::RunUntilIdle() { executor_.run(); }

zx_status_t Interrupter::Start(uint32_t interrupter, const RuntimeRegisterOffset& offset,
                               ddk::MmioView mmio_view, UsbXhci* hci) {
  hci_ = hci;
  return ZX_OK;
}

TRBPromise Interrupter::Timeout(zx::time deadline) {
  fit::bridge<TRB*, zx_status_t> bridge;
  auto state = reinterpret_cast<TestState*>(hci_->parent());
  auto context = state->trb_context_allocator_.New();
  FakeTRB* trb = new FakeTRB();
  trb->Op = FakeTRB::Op::Timeout;
  trb->deadline = deadline;
  context->trb = trb;
  context->completer = std::move(bridge.completer);
  state->pending_operations.push_back(std::move(context));
  return bridge.consumer.promise();
}

void UsbXhci::SetDeviceInformation(uint8_t slot, uint8_t port, const std::optional<HubInfo>& hub) {
  auto state = reinterpret_cast<TestState*>(parent_);
  auto context = state->trb_context_allocator_.New();
  FakeTRB* trb = new FakeTRB();
  trb->Op = FakeTRB::Op::SetDeviceInformation;
  trb->slot = slot;
  trb->port = port;
  trb->hub_info = hub;
  // NOTE: The TRB for the purposes of the test is owned by our test harness.
  // In a real environment, this would be owned by the transfer ring (it would be
  // a TRB that would be inside of a DMA buffer, since it is shared between the
  // device and the CPU)
  context->trb = trb;
  state->pending_operations.push_back(std::move(context));
}

usb_speed_t UsbXhci::GetDeviceSpeed(uint8_t slot) {
  auto state = reinterpret_cast<TestState*>(parent_);
  return state->speeds[slot - 1];
}

zx_status_t UsbXhci::DeviceOnline(uint32_t slot, uint16_t port, usb_speed_t speed) {
  auto state = reinterpret_cast<TestState*>(parent_);
  auto context = state->trb_context_allocator_.New();
  FakeTRB* trb = new FakeTRB();
  trb->Op = FakeTRB::Op::OnlineDevice;
  trb->slot = slot;
  trb->port = port;
  trb->speed = speed;
  // NOTE: The TRB for the purposes of the test is owned by our test harness.
  // In a real environment, this would be owned by the transfer ring (it would be
  // a TRB that would be inside of a DMA buffer, since it is shared between the
  // device and the CPU)
  context->trb = trb;
  state->pending_operations.push_back(std::move(context));
  return ZX_OK;
}

void UsbXhci::Shutdown(zx_status_t status) {
  auto state = reinterpret_cast<TestState*>(parent_);
  auto context = state->trb_context_allocator_.New();
  FakeTRB* trb = new FakeTRB();
  trb->Op = FakeTRB::Op::ShutdownController;
  trb->status = status;
  // NOTE: The TRB for the purposes of the test is owned by our test harness.
  // In a real environment, this would be owned by the transfer ring (it would be
  // a TRB that would be inside of a DMA buffer, since it is shared between the
  // device and the CPU)
  context->trb = trb;
  state->pending_operations.push_back(std::move(context));
}

TRBPromise UsbXhci::AddressDeviceCommand(uint8_t slot_id, uint8_t port_id,
                                         std::optional<HubInfo> hub_info, bool bsr) {
  fit::bridge<TRB*, zx_status_t> bridge;
  auto state = reinterpret_cast<TestState*>(parent_);
  auto context = state->trb_context_allocator_.New();
  FakeTRB* trb = new FakeTRB();
  trb->Op = FakeTRB::Op::AddressDevice;
  trb->slot = slot_id;
  trb->port = port_id;
  trb->hub_info = std::move(hub_info);
  trb->bsr = bsr;
  // NOTE: The TRB for the purposes of the test is owned by our test harness.
  // In a real environment, this would be owned by the transfer ring (it would be
  // a TRB that would be inside of a DMA buffer, since it is shared between the
  // device and the CPU)
  context->trb = trb;
  context->completer = std::move(bridge.completer);
  state->pending_operations.push_back(std::move(context));
  return bridge.consumer.promise();
}

TRBPromise UsbXhci::AddressDeviceCommand(uint8_t slot_id) {
  fit::bridge<TRB*, zx_status_t> bridge;
  auto state = reinterpret_cast<TestState*>(parent_);
  auto context = state->trb_context_allocator_.New();
  FakeTRB* trb = new FakeTRB();
  trb->Op = FakeTRB::Op::AddressDevice;
  trb->slot = slot_id;
  // NOTE: The TRB for the purposes of the test is owned by our test harness.
  // In a real environment, this would be owned by the transfer ring (it would be
  // a TRB that would be inside of a DMA buffer, since it is shared between the
  // device and the CPU)
  context->trb = trb;
  context->completer = std::move(bridge.completer);
  state->pending_operations.push_back(std::move(context));
  return bridge.consumer.promise();
}

TRBPromise UsbXhci::SetMaxPacketSizeCommand(uint8_t slot_id, uint8_t bMaxPacketSize0) {
  fit::bridge<TRB*, zx_status_t> bridge;
  auto state = reinterpret_cast<TestState*>(parent_);
  auto context = state->trb_context_allocator_.New();
  FakeTRB* trb = new FakeTRB();
  trb->Op = FakeTRB::Op::SetMaxPacketSize;
  trb->slot = slot_id;
  trb->max_packet_size = bMaxPacketSize0;
  // NOTE: The TRB for the purposes of the test is owned by our test harness.
  // In a real environment, this would be owned by the transfer ring (it would be
  // a TRB that would be inside of a DMA buffer, since it is shared between the
  // device and the CPU)
  context->trb = trb;
  context->completer = std::move(bridge.completer);
  state->pending_operations.push_back(std::move(context));
  return bridge.consumer.promise();
}

TRBPromise UsbXhci::EnableSlotCommand() {
  fit::bridge<TRB*, zx_status_t> bridge;
  auto state = reinterpret_cast<TestState*>(parent_);
  auto context = state->trb_context_allocator_.New();
  FakeTRB* trb = new FakeTRB();
  trb->Op = FakeTRB::Op::EnableSlot;
  trb->slot = state->slot;
  state->slot++;
  // NOTE: The TRB for the purposes of the test is owned by our test harness.
  // In a real environment, this would be owned by the transfer ring (it would be
  // a TRB that would be inside of a DMA buffer, since it is shared between the
  // device and the CPU)
  context->trb = trb;
  context->completer = std::move(bridge.completer);
  state->pending_operations.push_back(std::move(context));
  return bridge.consumer.promise();
}

TRBPromise UsbXhci::DisableSlotCommand(uint32_t slot) {
  fit::bridge<TRB*, zx_status_t> bridge;
  auto state = reinterpret_cast<TestState*>(parent_);
  auto context = state->trb_context_allocator_.New();
  FakeTRB* trb = new FakeTRB();
  trb->Op = FakeTRB::Op::DisableSlot;
  trb->slot = slot;
  // NOTE: The TRB for the purposes of the test is owned by our test harness.
  // In a real environment, this would be owned by the transfer ring (it would be
  // a TRB that would be inside of a DMA buffer, since it is shared between the
  // device and the CPU)
  context->trb = trb;
  context->completer = std::move(bridge.completer);
  state->pending_operations.push_back(std::move(context));
  return bridge.consumer.promise();
}

void UsbXhci::ResetPort(uint16_t port) {}

void UsbXhci::UsbHciSetBusInterface(const usb_bus_interface_protocol_t* bus_intf) {}

size_t UsbXhci::UsbHciGetMaxDeviceCount() { return 0; }

zx_status_t UsbXhci::UsbHciEnableEndpoint(uint32_t device_id,
                                          const usb_endpoint_descriptor_t* ep_desc,
                                          const usb_ss_ep_comp_descriptor_t* ss_com_desc,
                                          bool enable) {
  return ZX_ERR_NOT_SUPPORTED;
}

uint64_t UsbXhci::UsbHciGetCurrentFrame() { return 0; }

zx_status_t UsbXhci::UsbHciConfigureHub(uint32_t device_id, usb_speed_t speed,
                                        const usb_hub_descriptor_t* desc, bool multi_tt) {
  return ZX_ERR_NOT_SUPPORTED;
}
zx_status_t UsbXhci::UsbHciHubDeviceAdded(uint32_t device_id, uint32_t port, usb_speed_t speed) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t UsbXhci::UsbHciHubDeviceRemoved(uint32_t hub_id, uint32_t port) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t UsbXhci::UsbHciHubDeviceReset(uint32_t device_id, uint32_t port) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t UsbXhci::UsbHciResetEndpoint(uint32_t device_id, uint8_t ep_address) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t UsbXhci::UsbHciResetDevice(uint32_t hub_address, uint32_t device_id) {
  return ZX_ERR_NOT_SUPPORTED;
}

size_t UsbXhci::UsbHciGetMaxTransferSize(uint32_t device_id, uint8_t ep_address) { return 0; }

zx_status_t UsbXhci::UsbHciCancelAll(uint32_t device_id, uint8_t ep_address) {
  return ZX_ERR_NOT_SUPPORTED;
}

void UsbXhci::UsbHciRequestQueue(usb_request_t* usb_request,
                                 const usb_request_complete_t* complete_cb) {
  auto state = reinterpret_cast<TestState*>(parent_);
  auto context = state->trb_context_allocator_.New();
  context->request = Request(usb_request, *complete_cb, sizeof(usb_request_t));
  context->token = state->token;
  state->pending_operations.push_back(std::move(context));
}

int UsbXhci::InitThread(std::unique_ptr<dma_buffer::BufferFactory> factory) {
  interrupters_.reset(new Interrupter[1]);
  mmio_buffer_t invalid_mmio = {
      .vaddr = this,  // Add a dummy vaddr to pass the check in the MmioBuffer constructor.
      .offset = 0,
      .size = 0,
      .vmo = ZX_HANDLE_INVALID,
  };
  invalid_mmio.size = 4;
  interrupters_[0].Start(0, RuntimeRegisterOffset::Get().FromValue(0),
                         ddk::MmioView(invalid_mmio, 0, 1), this);
  device_state_ = std::make_unique<DeviceState[]>(32);
  return 0;
}

fit::promise<OwnedRequest, void> UsbXhci::UsbHciRequestQueue(OwnedRequest usb_request) {
  fit::bridge<OwnedRequest, void> bridge;
  usb_request_complete_t completion;
  completion.callback = [](void* ctx, usb_request_t* req) {
    auto completer = static_cast<fit::completer<OwnedRequest, void>*>(ctx);
    completer->complete_ok(OwnedRequest(req, sizeof(usb_request_t)));
    delete completer;
  };
  completion.ctx = new fit::completer<OwnedRequest, void>(std::move(bridge.completer));
  UsbHciRequestQueue(usb_request.take(), &completion);
  return bridge.consumer.promise().box();
}

size_t UsbXhci::UsbHciGetRequestSize() { return Request::RequestSize(sizeof(usb_request_t)); }

class EnumerationTests : public zxtest::Test {
 public:
  EnumerationTests() : controller_(reinterpret_cast<zx_device_t*>(&state_)) {
    controller_.InitThread(ddk_fake::CreateBufferFactory());
  }
  TestState& state() { return state_; }

  UsbXhci& controller() { return controller_; }

 private:
  TestState state_;
  UsbXhci controller_;
};

TEST_F(EnumerationTests, EnableSlotCommandPassesThroughFailureCode) {
  std::optional<HubInfo> hub_info;
  constexpr uint8_t kPort = 5;
  auto enumeration_task = EnumerateDevice(&controller(), kPort, std::move(hub_info));
  auto enable_slot_task = state().pending_operations.pop_front();
  auto enum_slot_trb = FakeTRB::FromTRB(enable_slot_task->trb);
  ASSERT_EQ(enum_slot_trb->Op, FakeTRB::Op::EnableSlot);
  enable_slot_task->completer->complete_error(ZX_ERR_UNAVAILABLE);
  ASSERT_EQ(controller().RunSynchronously(std::move(enumeration_task)), ZX_ERR_UNAVAILABLE);
}

TEST_F(EnumerationTests, EnableSlotCommandReturnsIOErrorOnFailure) {
  std::optional<HubInfo> hub_info;
  constexpr uint8_t kPort = 5;
  auto enumeration_task = EnumerateDevice(&controller(), kPort, std::move(hub_info));
  auto enable_slot_task = state().pending_operations.pop_front();
  auto enum_slot_trb = FakeTRB::FromTRB(enable_slot_task->trb);
  ASSERT_EQ(enum_slot_trb->Op, FakeTRB::Op::EnableSlot);
  reinterpret_cast<CommandCompletionEvent*>(enum_slot_trb)
      ->set_CompletionCode(CommandCompletionEvent::UndefinedError);
  enable_slot_task->completer->complete_ok(enum_slot_trb);
  ASSERT_EQ(controller().RunSynchronously(std::move(enumeration_task)), ZX_ERR_IO);
}

TEST_F(EnumerationTests, EnableSlotCommandSetsDeviceInformationOnSuccess) {
  constexpr uint8_t kPort = 5;
  constexpr uint8_t kHubDepth = 52;
  constexpr uint8_t kHubId = 28;
  constexpr usb_speed_t kSpeed = USB_SPEED_HIGH;
  constexpr bool kMultiTT = false;
  std::optional<HubInfo> hub_info;
  hub_info->hub_depth = kHubDepth;
  hub_info->hub_id = kHubId;
  hub_info->hub_speed = kSpeed;
  hub_info->multi_tt = kMultiTT;
  auto enumeration_task = EnumerateDevice(&controller(), kPort, std::move(hub_info));
  auto enable_slot_task = state().pending_operations.pop_front();
  auto enum_slot_trb = FakeTRB::FromTRB(enable_slot_task->trb);
  ASSERT_EQ(enum_slot_trb->Op, FakeTRB::Op::EnableSlot);
  reinterpret_cast<CommandCompletionEvent*>(enum_slot_trb)
      ->set_CompletionCode(CommandCompletionEvent::Success);
  reinterpret_cast<CommandCompletionEvent*>(enum_slot_trb)->set_SlotID(1);
  enable_slot_task->completer->complete_ok(enum_slot_trb);
  controller().ScheduleTask(std::move(enumeration_task));
  controller().RunUntilIdle();
  auto device_information = FakeTRB::FromTRB(state().pending_operations.pop_front()->trb);
  ASSERT_EQ(device_information->Op, FakeTRB::Op::SetDeviceInformation);
  ASSERT_EQ(device_information->hub_info->hub_depth, hub_info->hub_depth);
  ASSERT_EQ(device_information->hub_info->hub_id, hub_info->hub_id);
  ASSERT_EQ(device_information->hub_info->hub_speed, hub_info->hub_speed);
  ASSERT_EQ(device_information->hub_info->multi_tt, hub_info->multi_tt);
  ASSERT_EQ(device_information->port, kPort);
  ASSERT_EQ(device_information->slot, 1);
  controller().RunUntilIdle();
  auto address_device_op = FakeTRB::FromTRB(state().pending_operations.pop_front()->trb);
  ASSERT_EQ(address_device_op->Op, FakeTRB::Op::AddressDevice);
  controller().RunUntilIdle();
  auto disable_trb = FakeTRB::FromTRB(state().pending_operations.pop_front()->trb);
  ASSERT_EQ(disable_trb->Op, FakeTRB::Op::DisableSlot);
  ASSERT_EQ(disable_trb->slot, 1);
}

TEST_F(EnumerationTests, AddressDeviceCommandPassesThroughFailureCode) {
  // EnableSlot
  constexpr uint8_t kPort = 5;
  constexpr uint8_t kHubDepth = 52;
  constexpr uint8_t kHubId = 28;
  constexpr usb_speed_t kSpeed = USB_SPEED_HIGH;
  constexpr bool kMultiTT = false;
  std::optional<HubInfo> hub_info;
  hub_info->hub_depth = kHubDepth;
  hub_info->hub_id = kHubId;
  hub_info->hub_speed = kSpeed;
  hub_info->multi_tt = kMultiTT;
  zx_status_t completion_code = -1;
  TRB* completion_trb = nullptr;
  auto enumeration_task = EnumerateDevice(&controller(), kPort, std::move(hub_info))
                              .then([&](fit::result<TRB*, zx_status_t>& result) {
                                if (result.is_ok()) {
                                  completion_trb = result.value();
                                  completion_code = ZX_OK;
                                } else {
                                  completion_code = result.error();
                                }
                                return result;
                              });
  auto enable_slot_task = state().pending_operations.pop_front();
  auto enum_slot_trb = FakeTRB::FromTRB(enable_slot_task->trb);
  ASSERT_EQ(enum_slot_trb->Op, FakeTRB::Op::EnableSlot);
  reinterpret_cast<CommandCompletionEvent*>(enum_slot_trb)
      ->set_CompletionCode(CommandCompletionEvent::Success);
  reinterpret_cast<CommandCompletionEvent*>(enum_slot_trb)->set_SlotID(1);
  enable_slot_task->completer->complete_ok(enum_slot_trb);
  controller().ScheduleTask(std::move(enumeration_task));
  controller().RunUntilIdle();
  auto device_information = FakeTRB::FromTRB(state().pending_operations.pop_front()->trb);
  ASSERT_EQ(device_information->Op, FakeTRB::Op::SetDeviceInformation);
  ASSERT_EQ(device_information->hub_info->hub_depth, hub_info->hub_depth);
  ASSERT_EQ(device_information->hub_info->hub_id, hub_info->hub_id);
  ASSERT_EQ(device_information->hub_info->hub_speed, hub_info->hub_speed);
  ASSERT_EQ(device_information->hub_info->multi_tt, hub_info->multi_tt);
  ASSERT_EQ(device_information->port, kPort);
  ASSERT_EQ(device_information->slot, 1);
  controller().RunUntilIdle();

  // AddressDevice
  auto address_device = state().pending_operations.pop_front();
  auto address_device_op = FakeTRB::FromTRB(address_device->trb);
  ASSERT_EQ(address_device_op->Op, FakeTRB::Op::AddressDevice);
  ASSERT_EQ(address_device_op->slot, 1);
  ASSERT_EQ(address_device_op->port, kPort);
  ASSERT_EQ(address_device_op->hub_info->hub_depth, hub_info->hub_depth);
  ASSERT_EQ(address_device_op->hub_info->hub_id, hub_info->hub_id);
  ASSERT_EQ(address_device_op->hub_info->hub_speed, hub_info->hub_speed);
  ASSERT_EQ(address_device_op->hub_info->multi_tt, hub_info->multi_tt);
  address_device->completer->complete_error(ZX_ERR_IO_OVERRUN);
  controller().RunUntilIdle();
  auto disable_trb = FakeTRB::FromTRB(state().pending_operations.pop_front()->trb);
  ASSERT_EQ(disable_trb->Op, FakeTRB::Op::DisableSlot);
  ASSERT_EQ(disable_trb->slot, 1);
  ASSERT_EQ(completion_code, ZX_ERR_IO_OVERRUN);
}

TEST_F(EnumerationTests, AddressDeviceCommandReturnsErrorOnFailure) {
  // EnableSlot
  constexpr uint8_t kPort = 5;
  constexpr uint8_t kHubDepth = 52;
  constexpr uint8_t kHubId = 28;
  constexpr usb_speed_t kSpeed = USB_SPEED_HIGH;
  constexpr bool kMultiTT = false;
  std::optional<HubInfo> hub_info;
  hub_info->hub_depth = kHubDepth;
  hub_info->hub_id = kHubId;
  hub_info->hub_speed = kSpeed;
  hub_info->multi_tt = kMultiTT;
  zx_status_t completion_code = -1;
  TRB* completion_trb = nullptr;
  auto enumeration_task = EnumerateDevice(&controller(), kPort, std::move(hub_info))
                              .then([&](fit::result<TRB*, zx_status_t>& result) {
                                if (result.is_ok()) {
                                  completion_trb = result.value();
                                  completion_code = ZX_OK;
                                } else {
                                  completion_code = result.error();
                                }
                                return result;
                              });
  auto enable_slot_task = state().pending_operations.pop_front();
  auto enum_slot_trb = FakeTRB::FromTRB(enable_slot_task->trb);
  ASSERT_EQ(enum_slot_trb->Op, FakeTRB::Op::EnableSlot);
  reinterpret_cast<CommandCompletionEvent*>(enum_slot_trb)
      ->set_CompletionCode(CommandCompletionEvent::Success);
  reinterpret_cast<CommandCompletionEvent*>(enum_slot_trb)->set_SlotID(1);
  enable_slot_task->completer->complete_ok(enum_slot_trb);
  controller().ScheduleTask(std::move(enumeration_task));
  controller().RunUntilIdle();
  auto device_information = FakeTRB::FromTRB(state().pending_operations.pop_front()->trb);
  ASSERT_EQ(device_information->Op, FakeTRB::Op::SetDeviceInformation);
  ASSERT_EQ(device_information->hub_info->hub_depth, hub_info->hub_depth);
  ASSERT_EQ(device_information->hub_info->hub_id, hub_info->hub_id);
  ASSERT_EQ(device_information->hub_info->hub_speed, hub_info->hub_speed);
  ASSERT_EQ(device_information->hub_info->multi_tt, hub_info->multi_tt);
  ASSERT_EQ(device_information->port, kPort);
  ASSERT_EQ(device_information->slot, 1);
  controller().RunUntilIdle();

  // AddressDevice
  auto address_device = state().pending_operations.pop_front();
  auto address_device_op = FakeTRB::FromTRB(address_device->trb);
  ASSERT_EQ(address_device_op->Op, FakeTRB::Op::AddressDevice);
  ASSERT_EQ(address_device_op->slot, 1);
  ASSERT_EQ(address_device_op->port, kPort);
  ASSERT_EQ(address_device_op->hub_info->hub_depth, hub_info->hub_depth);
  ASSERT_EQ(address_device_op->hub_info->hub_id, hub_info->hub_id);
  ASSERT_EQ(address_device_op->hub_info->hub_speed, hub_info->hub_speed);
  ASSERT_EQ(address_device_op->hub_info->multi_tt, hub_info->multi_tt);
  reinterpret_cast<CommandCompletionEvent*>(address_device_op)
      ->set_CompletionCode(CommandCompletionEvent::Stopped);
  address_device->completer->complete_ok(address_device_op);
  controller().RunUntilIdle();
  auto disable_trb = FakeTRB::FromTRB(state().pending_operations.pop_front()->trb);
  ASSERT_EQ(disable_trb->Op, FakeTRB::Op::DisableSlot);
  ASSERT_EQ(disable_trb->slot, 1);
  ASSERT_EQ(completion_code, ZX_ERR_IO);
}

TEST_F(EnumerationTests, AddressDeviceCommandShouldOnlineDeviceUponCompletion) {
  // EnableSlot
  constexpr uint8_t kPort = 5;
  constexpr uint8_t kHubDepth = 52;
  constexpr uint8_t kHubId = 28;
  constexpr usb_speed_t kSpeed = USB_SPEED_HIGH;
  constexpr bool kMultiTT = false;
  state().speeds[0] = kSpeed;
  std::optional<HubInfo> hub_info;
  hub_info->hub_depth = kHubDepth;
  hub_info->hub_id = kHubId;
  hub_info->hub_speed = kSpeed;
  hub_info->multi_tt = kMultiTT;
  zx_status_t completion_code = -1;
  TRB* completion_trb = nullptr;
  auto enumeration_task = EnumerateDevice(&controller(), kPort, std::move(hub_info))
                              .then([&](fit::result<TRB*, zx_status_t>& result) {
                                if (result.is_ok()) {
                                  completion_trb = result.value();
                                  completion_code = ZX_OK;
                                } else {
                                  completion_code = result.error();
                                }
                                return result;
                              });
  auto enable_slot_task = state().pending_operations.pop_front();
  auto enum_slot_trb = FakeTRB::FromTRB(enable_slot_task->trb);
  ASSERT_EQ(enum_slot_trb->Op, FakeTRB::Op::EnableSlot);
  reinterpret_cast<CommandCompletionEvent*>(enum_slot_trb)
      ->set_CompletionCode(CommandCompletionEvent::Success);
  reinterpret_cast<CommandCompletionEvent*>(enum_slot_trb)->set_SlotID(1);
  enable_slot_task->completer->complete_ok(enum_slot_trb);
  controller().ScheduleTask(std::move(enumeration_task));
  controller().RunUntilIdle();
  auto device_information = FakeTRB::FromTRB(state().pending_operations.pop_front()->trb);
  ASSERT_EQ(device_information->Op, FakeTRB::Op::SetDeviceInformation);
  ASSERT_EQ(device_information->hub_info->hub_depth, hub_info->hub_depth);
  ASSERT_EQ(device_information->hub_info->hub_id, hub_info->hub_id);
  ASSERT_EQ(device_information->hub_info->hub_speed, hub_info->hub_speed);
  ASSERT_EQ(device_information->hub_info->multi_tt, hub_info->multi_tt);
  ASSERT_EQ(device_information->port, kPort);
  ASSERT_EQ(device_information->slot, 1);

  // AddressDevice
  auto address_device = state().pending_operations.pop_front();
  auto address_device_op = FakeTRB::FromTRB(address_device->trb);
  ASSERT_EQ(address_device_op->Op, FakeTRB::Op::AddressDevice);
  ASSERT_EQ(address_device_op->slot, 1);
  ASSERT_EQ(address_device_op->port, kPort);
  ASSERT_EQ(address_device_op->hub_info->hub_depth, hub_info->hub_depth);
  ASSERT_EQ(address_device_op->hub_info->hub_id, hub_info->hub_id);
  ASSERT_EQ(address_device_op->hub_info->hub_speed, hub_info->hub_speed);
  ASSERT_EQ(address_device_op->hub_info->multi_tt, hub_info->multi_tt);
  reinterpret_cast<CommandCompletionEvent*>(address_device_op)
      ->set_CompletionCode(CommandCompletionEvent::Success);
  address_device->completer->complete_ok(address_device_op);
  controller().RunUntilIdle();
  // Timeout
  auto timeout = state().pending_operations.pop_front();
  ASSERT_TRUE(FakeTRB::FromTRB(timeout->trb)->deadline.get());
  timeout->completer->complete_ok(address_device_op);
  controller().RunUntilIdle();
  // GetMaxPacketSize
  auto get_max_packet_size = state().pending_operations.pop_front();
  auto get_max_packet_size_request = std::move(get_max_packet_size->request);
  ASSERT_EQ(get_max_packet_size_request->request()->header.device_id, 0);
  ASSERT_EQ(get_max_packet_size_request->request()->header.ep_address, 0);
  ASSERT_EQ(get_max_packet_size_request->request()->header.length, 8);
  ASSERT_EQ(get_max_packet_size_request->request()->setup.bmRequestType,
            USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE);
  ASSERT_EQ(get_max_packet_size_request->request()->setup.wValue, USB_DT_DEVICE << 8);
  ASSERT_EQ(get_max_packet_size_request->request()->setup.wIndex, 0);
  ASSERT_EQ(get_max_packet_size_request->request()->setup.bRequest, USB_REQ_GET_DESCRIPTOR);
  ASSERT_EQ(get_max_packet_size_request->request()->setup.wLength, 8);
  ASSERT_TRUE(get_max_packet_size_request->request()->direct);
  usb_device_descriptor_t* descriptor;
  ASSERT_OK(get_max_packet_size_request->Mmap(reinterpret_cast<void**>(&descriptor)));
  descriptor->bDescriptorType = USB_DT_DEVICE;
  descriptor->bMaxPacketSize0 = 42;
  get_max_packet_size_request->Complete(ZX_OK, 8);
  controller().RunUntilIdle();

  // Online Device
  auto online_trb = FakeTRB::FromTRB(state().pending_operations.pop_front()->trb);
  ASSERT_EQ(online_trb->Op, FakeTRB::Op::OnlineDevice);
  ASSERT_EQ(online_trb->slot, 1);
  ASSERT_EQ(online_trb->port, kPort);
  ASSERT_EQ(online_trb->speed, USB_SPEED_HIGH);
  controller().RunUntilIdle();
  ASSERT_EQ(completion_code, ZX_OK);
  ASSERT_TRUE(state().pending_operations.is_empty());
}

TEST_F(EnumerationTests, AddressDeviceCommandShouldOnlineDeviceAfterSuccessfulRetry) {
  // EnableSlot
  constexpr uint8_t kPort = 5;
  constexpr uint8_t kHubDepth = 52;
  constexpr uint8_t kHubId = 28;
  constexpr usb_speed_t kSpeed = USB_SPEED_FULL;
  state().speeds[0] = kSpeed;
  state().speeds[1] = kSpeed;
  std::optional<HubInfo> hub_info;
  hub_info->hub_depth = kHubDepth;
  hub_info->hub_id = kHubId;
  hub_info->hub_speed = kSpeed;
  hub_info->multi_tt = false;
  zx_status_t completion_code = -1;
  TRB* completion_trb = nullptr;
  auto enumeration_task = EnumerateDevice(&controller(), kPort, std::move(hub_info))
                              .then([&](fit::result<TRB*, zx_status_t>& result) {
                                if (result.is_ok()) {
                                  completion_trb = result.value();
                                  completion_code = ZX_OK;
                                } else {
                                  completion_code = result.error();
                                }
                                return result;
                              });
  auto enable_slot_task = state().pending_operations.pop_front();
  auto enum_slot_trb = FakeTRB::FromTRB(enable_slot_task->trb);
  ASSERT_EQ(enum_slot_trb->Op, FakeTRB::Op::EnableSlot);
  reinterpret_cast<CommandCompletionEvent*>(enum_slot_trb)
      ->set_CompletionCode(CommandCompletionEvent::Success);
  reinterpret_cast<CommandCompletionEvent*>(enum_slot_trb)->set_SlotID(1);
  enable_slot_task->completer->complete_ok(enum_slot_trb);
  controller().ScheduleTask(std::move(enumeration_task));
  controller().RunUntilIdle();
  auto device_information = FakeTRB::FromTRB(state().pending_operations.pop_front()->trb);
  ASSERT_EQ(device_information->Op, FakeTRB::Op::SetDeviceInformation);
  ASSERT_EQ(device_information->hub_info->hub_depth, hub_info->hub_depth);
  ASSERT_EQ(device_information->hub_info->hub_id, hub_info->hub_id);
  ASSERT_EQ(device_information->hub_info->hub_speed, hub_info->hub_speed);
  ASSERT_EQ(device_information->hub_info->multi_tt, hub_info->multi_tt);
  ASSERT_EQ(device_information->port, kPort);
  ASSERT_EQ(device_information->slot, 1);

  // AddressDevice
  auto address_device = state().pending_operations.pop_front();
  auto address_device_op = FakeTRB::FromTRB(address_device->trb);
  ASSERT_EQ(address_device_op->Op, FakeTRB::Op::AddressDevice);
  ASSERT_EQ(address_device_op->slot, 1);
  ASSERT_EQ(address_device_op->port, kPort);
  ASSERT_EQ(address_device_op->hub_info->hub_depth, hub_info->hub_depth);
  ASSERT_EQ(address_device_op->hub_info->hub_id, hub_info->hub_id);
  ASSERT_EQ(address_device_op->hub_info->hub_speed, hub_info->hub_speed);
  ASSERT_EQ(address_device_op->hub_info->multi_tt, hub_info->multi_tt);
  reinterpret_cast<CommandCompletionEvent*>(address_device_op)
      ->set_CompletionCode(CommandCompletionEvent::UsbTransactionError);
  address_device->completer->complete_ok(address_device_op);
  controller().RunUntilIdle();

  // DisableSlot
  auto disable_op = state().pending_operations.pop_front();
  auto disable_trb = FakeTRB::FromTRB(disable_op->trb);
  ASSERT_EQ(disable_trb->Op, FakeTRB::Op::DisableSlot);
  ASSERT_EQ(disable_trb->slot, 1);
  reinterpret_cast<CommandCompletionEvent*>(disable_trb)
      ->set_CompletionCode(CommandCompletionEvent::UsbTransactionError);
  disable_op->completer->complete_ok(disable_trb);
  controller().RunUntilIdle();

  // EnableSlot
  enable_slot_task = state().pending_operations.pop_front();
  enum_slot_trb = FakeTRB::FromTRB(enable_slot_task->trb);
  ASSERT_EQ(enum_slot_trb->Op, FakeTRB::Op::EnableSlot);
  reinterpret_cast<CommandCompletionEvent*>(enum_slot_trb)
      ->set_CompletionCode(CommandCompletionEvent::Success);
  reinterpret_cast<CommandCompletionEvent*>(enum_slot_trb)->set_SlotID(2);
  enable_slot_task->completer->complete_ok(enum_slot_trb);
  controller().RunUntilIdle();

  // Set device information
  device_information = FakeTRB::FromTRB(state().pending_operations.pop_front()->trb);
  ASSERT_EQ(device_information->Op, FakeTRB::Op::SetDeviceInformation);
  ASSERT_EQ(device_information->hub_info->hub_depth, hub_info->hub_depth);
  ASSERT_EQ(device_information->hub_info->hub_id, hub_info->hub_id);
  ASSERT_EQ(device_information->hub_info->hub_speed, hub_info->hub_speed);
  ASSERT_EQ(device_information->hub_info->multi_tt, hub_info->multi_tt);
  ASSERT_EQ(device_information->port, kPort);
  ASSERT_EQ(device_information->slot, 2);
  controller().RunUntilIdle();

  // AddressDevice with BSR = 1
  address_device = state().pending_operations.pop_front();
  address_device_op = FakeTRB::FromTRB(address_device->trb);
  ASSERT_EQ(address_device_op->Op, FakeTRB::Op::AddressDevice);
  ASSERT_TRUE(address_device_op->bsr);
  ASSERT_EQ(address_device_op->slot, 2);
  ASSERT_EQ(address_device_op->port, kPort);
  ASSERT_EQ(address_device_op->hub_info->hub_depth, hub_info->hub_depth);
  ASSERT_EQ(address_device_op->hub_info->hub_id, hub_info->hub_id);
  ASSERT_EQ(address_device_op->hub_info->hub_speed, hub_info->hub_speed);
  ASSERT_EQ(address_device_op->hub_info->multi_tt, hub_info->multi_tt);
  reinterpret_cast<CommandCompletionEvent*>(address_device_op)
      ->set_CompletionCode(CommandCompletionEvent::Success);
  address_device->completer->complete_ok(address_device_op);
  controller().RunUntilIdle();

  // GetMaxPacketSize
  auto get_max_packet_size = state().pending_operations.pop_front();
  auto get_max_packet_size_request = std::move(get_max_packet_size->request);
  ASSERT_EQ(get_max_packet_size_request->request()->header.device_id, 1);
  ASSERT_EQ(get_max_packet_size_request->request()->header.ep_address, 0);
  ASSERT_EQ(get_max_packet_size_request->request()->header.length, 8);
  ASSERT_EQ(get_max_packet_size_request->request()->setup.bmRequestType,
            USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE);
  ASSERT_EQ(get_max_packet_size_request->request()->setup.wValue, USB_DT_DEVICE << 8);
  ASSERT_EQ(get_max_packet_size_request->request()->setup.wIndex, 0);
  ASSERT_EQ(get_max_packet_size_request->request()->setup.bRequest, USB_REQ_GET_DESCRIPTOR);
  ASSERT_EQ(get_max_packet_size_request->request()->setup.wLength, 8);
  ASSERT_TRUE(get_max_packet_size_request->request()->direct);
  usb_device_descriptor_t* descriptor;
  ASSERT_OK(get_max_packet_size_request->Mmap(reinterpret_cast<void**>(&descriptor)));
  descriptor->bDescriptorType = USB_DT_DEVICE;
  descriptor->bMaxPacketSize0 = 42;
  get_max_packet_size_request->Complete(ZX_OK, 8);
  controller().RunUntilIdle();

  // SetMaxPacketSize
  auto set_max_packet_size = state().pending_operations.pop_front();
  auto set_max_packet_size_trb = FakeTRB::FromTRB(set_max_packet_size->trb);
  ASSERT_EQ(set_max_packet_size_trb->Op, FakeTRB::Op::SetMaxPacketSize);
  ASSERT_EQ(set_max_packet_size_trb->slot, 2);
  ASSERT_EQ(set_max_packet_size_trb->max_packet_size, 42);
  reinterpret_cast<CommandCompletionEvent*>(set_max_packet_size_trb)
      ->set_CompletionCode(CommandCompletionEvent::Success);
  set_max_packet_size->completer->complete_ok(set_max_packet_size_trb);
  controller().RunUntilIdle();

  // AddressDevice with BSR = 0
  address_device = state().pending_operations.pop_front();
  address_device_op = FakeTRB::FromTRB(address_device->trb);
  ASSERT_EQ(address_device_op->Op, FakeTRB::Op::AddressDevice);
  ASSERT_FALSE(address_device_op->bsr);
  ASSERT_EQ(address_device_op->slot, 2);
  reinterpret_cast<CommandCompletionEvent*>(address_device_op)
      ->set_CompletionCode(CommandCompletionEvent::Success);
  address_device->completer->complete_ok(address_device_op);
  controller().RunUntilIdle();

  // Timeout
  auto timeout = state().pending_operations.pop_front();
  ASSERT_TRUE(FakeTRB::FromTRB(timeout->trb)->deadline.get());
  timeout->completer->complete_ok(address_device_op);
  controller().RunUntilIdle();

  // GetMaxPacketSize
  get_max_packet_size = state().pending_operations.pop_front();
  get_max_packet_size_request = std::move(get_max_packet_size->request);
  ASSERT_EQ(get_max_packet_size_request->request()->header.device_id, 1);
  ASSERT_EQ(get_max_packet_size_request->request()->header.ep_address, 0);
  ASSERT_EQ(get_max_packet_size_request->request()->header.length, 8);
  ASSERT_EQ(get_max_packet_size_request->request()->setup.bmRequestType,
            USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE);
  ASSERT_EQ(get_max_packet_size_request->request()->setup.wValue, USB_DT_DEVICE << 8);
  ASSERT_EQ(get_max_packet_size_request->request()->setup.wIndex, 0);
  ASSERT_EQ(get_max_packet_size_request->request()->setup.bRequest, USB_REQ_GET_DESCRIPTOR);
  ASSERT_EQ(get_max_packet_size_request->request()->setup.wLength, 8);
  ASSERT_TRUE(get_max_packet_size_request->request()->direct);
  ASSERT_OK(get_max_packet_size_request->Mmap(reinterpret_cast<void**>(&descriptor)));
  descriptor->bDescriptorType = USB_DT_DEVICE;
  descriptor->bMaxPacketSize0 = 32;
  get_max_packet_size_request->Complete(ZX_OK, 8);
  controller().RunUntilIdle();

  // SetMaxPacketSize (full-speed device requires setting this again)
  set_max_packet_size = state().pending_operations.pop_front();
  set_max_packet_size_trb = FakeTRB::FromTRB(set_max_packet_size->trb);
  ASSERT_EQ(set_max_packet_size_trb->Op, FakeTRB::Op::SetMaxPacketSize);
  ASSERT_EQ(set_max_packet_size_trb->slot, 2);
  ASSERT_EQ(set_max_packet_size_trb->max_packet_size, 32);
  reinterpret_cast<CommandCompletionEvent*>(set_max_packet_size_trb)
      ->set_CompletionCode(CommandCompletionEvent::Success);
  set_max_packet_size->completer->complete_ok(set_max_packet_size_trb);
  controller().RunUntilIdle();

  // Online Device
  auto online_trb = FakeTRB::FromTRB(state().pending_operations.pop_front()->trb);
  ASSERT_EQ(online_trb->Op, FakeTRB::Op::OnlineDevice);
  ASSERT_EQ(online_trb->slot, 2);
  ASSERT_EQ(online_trb->port, 5);
  ASSERT_EQ(online_trb->speed, USB_SPEED_FULL);
  controller().RunUntilIdle();
  ASSERT_EQ(completion_code, ZX_OK);
  ASSERT_TRUE(state().pending_operations.is_empty());
}

}  // namespace usb_xhci
