// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "xhci-transfer-ring.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/promise.h>
#include <zircon/syscalls.h>

#include <atomic>
#include <thread>

#include <fake-dma-buffer/fake-dma-buffer.h>
#include <fake-mmio-reg/fake-mmio-reg.h>
#include <fbl/algorithm.h>
#include <zxtest/zxtest.h>

#include "usb-xhci.h"
#include "xhci-event-ring.h"

namespace usb_xhci {

const zx::bti kFakeBti(42);

class TransferRingHarness : public zxtest::Test {
 public:
  TransferRingHarness()
      : trb_context_allocator_(-1, true),
        hci_(reinterpret_cast<zx_device_t*>(this), ddk_fake::CreateBufferFactory()) {}
  void SetUp() override {
    constexpr auto kOffset = 6;
    constexpr auto kErdp = 2062;

    region_.emplace(regs_, sizeof(uint32_t), std::size(regs_));
    buffer_.emplace(region_->GetMmioBuffer());
    regs_[kOffset].SetReadCallback([=]() { return 0x2000; });
    regs_[kErdp].SetReadCallback([=]() { return erdp_; });
    regs_[kErdp].SetWriteCallback([=](uint64_t value) {
      ERDP reg;
      reg.set_reg_value(value);
      erdp_ = reg.Pointer();
    });
    hci_.SetTestHarness(this);
    ASSERT_OK(hci_.InitThread());
  }

  void TearDown() override {}

  using TestRequest = usb::CallbackRequest<sizeof(max_align_t)>;
  template <typename Callback>
  zx_status_t AllocateRequest(std::optional<TestRequest>* request, uint32_t device_id,
                              uint64_t data_size, uint8_t endpoint, Callback callback) {
    return TestRequest::Alloc(request, data_size, endpoint, hci_.UsbHciGetRequestSize(),
                              std::move(callback));
  }

  void RequestQueue(usb_request_t* usb_request,
                    const usb_request_complete_callback_t* complete_cb) {
    pending_req_ = Request(usb_request, *complete_cb, sizeof(usb_request_t));
  }

  Request Borrow(TestRequest request) {
    request.Queue(*this);
    return std::move(*pending_req_);
  }

  TransferRing* ring() { return ring_; }

  void SetRing(TransferRing* ring) { ring_ = ring; }

  std::unique_ptr<TRBContext> AllocateContext() { return trb_context_allocator_.New(); }

  EventRing& event_ring() { return event_ring_; }

 private:
  using AllocatorTraits = fbl::InstancedSlabAllocatorTraits<std::unique_ptr<TRBContext>, 4096U>;
  using AllocatorType = fbl::SlabAllocator<AllocatorTraits>;
  AllocatorType trb_context_allocator_;

  std::optional<Request> pending_req_;
  fbl::DoublyLinkedList<std::unique_ptr<TRBContext>> pending_contexts_;
  std::optional<fdf::MmioBuffer> buffer_;
  UsbXhci hci_;
  TransferRing* ring_;
  EventRing event_ring_;
  CommandRing command_ring_;
  uint64_t erdp_;
  ddk_fake::FakeMmioReg regs_[4096];
  std::optional<ddk_fake::FakeMmioRegRegion> region_;
};

void UsbXhci::UsbHciSetBusInterface(const usb_bus_interface_protocol_t* bus_intf) {}

size_t UsbXhci::UsbHciGetMaxDeviceCount() { return 0; }

zx_status_t UsbXhci::UsbHciEnableEndpoint(uint32_t device_id,
                                          const usb_endpoint_descriptor_t* ep_desc,
                                          const usb_ss_ep_comp_descriptor_t* ss_com_desc,
                                          bool enable) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t EventRing::AddTRB() { return ZX_OK; }

zx_status_t UsbXhci::InitThread() {
  fbl::AllocChecker ac;
  interrupters_ = fbl::MakeArray<Interrupter>(&ac, 1);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  max_slots_ = 32;
  device_state_ = fbl::MakeArray<DeviceState>(&ac, max_slots_);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  for (size_t i = 0; i < max_slots_; i++) {
    fbl::AutoLock l(&device_state_[i].transaction_lock());
    for (size_t c = 0; c < max_slots_; c++) {
      zx_status_t status = device_state_[i].GetTransferRing(c).Init(
          zx_system_get_page_size(), kFakeBti,
          &static_cast<TransferRingHarness*>(GetTestHarness())->event_ring(), false, nullptr,
          *this);
      if (status != ZX_OK) {
        return status;
      }
    }
  }
  fbl::AutoLock l(&device_state_[0].transaction_lock());
  static_cast<TransferRingHarness*>(GetTestHarness())
      ->SetRing(&device_state_[0].GetTransferRing(0));
  return ZX_OK;
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

size_t UsbXhci::UsbHciGetRequestSize() { return Request::RequestSize(sizeof(usb_request_t)); }

void UsbXhci::UsbHciRequestQueue(usb_request_t* usb_request,
                                 const usb_request_complete_callback_t* complete_cb) {}

zx_status_t EventRing::AddSegmentIfNone() { return ZX_OK; }

void UsbXhci::Shutdown(zx_status_t status) {}

void EventRing::RemovePressure() {}

TRBPromise UsbXhci::DeviceOffline(uint32_t slot, TRB* continuation) {
  return fpromise::make_error_promise(ZX_ERR_NOT_SUPPORTED);
}

TRBPromise EnumerateDevice(UsbXhci* hci, uint8_t port, std::optional<HubInfo> hub_info) {
  return fpromise::make_error_promise(ZX_ERR_NOT_SUPPORTED);
}

TEST_F(TransferRingHarness, EmptyShortTransferTest) {
  auto ring = this->ring();
  ASSERT_EQ(ring->HandleShortPacket(nullptr, nullptr, nullptr, 0), ZX_ERR_IO);
}

TEST_F(TransferRingHarness, CorruptedTransferRingShortTransferTest) {
  auto ring = this->ring();
  Normal trb;
  {
    auto context = ring->AllocateContext();
    ASSERT_OK(ring->AddTRB(trb, std::move(context)));
  }
  ASSERT_EQ(ring->HandleShortPacket(nullptr, nullptr, nullptr, 0), ZX_ERR_IO);
  ring->TakePendingTRBs();
}

TEST_F(TransferRingHarness, MultiPageShortTransferTest) {
  auto ring = this->ring();
  TRB* last;
  TRB* first = nullptr;
  {
    auto context = ring->AllocateContext();
    for (size_t i = 0; i < 510; i++) {
      TRB* ptr;
      ASSERT_OK(ring->AllocateTRB(&ptr, nullptr));
      if (first == nullptr) {
        first = ptr;
      }
      Control::FromTRB(ptr).set_Type(Control::Normal).ToTrb(ptr);
      static_cast<Normal*>(ptr)->set_LENGTH(static_cast<uint32_t>(i) * 20);
      last = ptr;
    }
    ASSERT_OK(ring->AssignContext(last, std::move(context), first));
    last--;
  }
  size_t transferred = 0;
  TRB* first_trb;
  ASSERT_OK(ring->HandleShortPacket(last, &transferred, &first_trb, 0));
  ASSERT_EQ(transferred, 2585720);
  ring->TakePendingTRBs();
}

TEST_F(TransferRingHarness, SetStall) {
  auto ring = this->ring();
  ASSERT_FALSE(ring->stalled());
  ring->set_stall(true);
  ASSERT_TRUE(ring->stalled());
  ring->set_stall(false);
  ASSERT_FALSE(ring->stalled());
}

TEST_F(TransferRingHarness, AllocateContiguousFailsIfNotEnoughContiguousPhysicalMemoryExists) {
  constexpr auto kOverAllocateAmount = 9001;
  auto ring = this->ring();
  ASSERT_EQ(ring->AllocateContiguous(kOverAllocateAmount).error_value(), ZX_ERR_NO_MEMORY);
}

TEST_F(TransferRingHarness, AllocateContiguousAllocatesContiguousBlocks) {
  auto ring = this->ring();
  constexpr auto kContiguousCount = 42;
  constexpr auto kIterationCount = 512;
  for (size_t i = 0; i < kIterationCount; i++) {
    auto result = ring->AllocateContiguous(kContiguousCount);
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(result->trbs.size(), kContiguousCount);
    auto trb_start = result->trbs.data();
    for (size_t c = 0; c < kContiguousCount; c++) {
      ASSERT_NE(Control::FromTRB(trb_start + c).Type(), Control::Link);
    }
  }
}

TEST_F(TransferRingHarness, CanHandleConsecutiveLinks) {
  auto ring = this->ring();
  const size_t trb_per_segment = zx_system_get_page_size() / sizeof(TRB);
  // 1 TRB is the link TRB, and TransferRing::AllocInternal() will allocate if there's not 2
  // available TRBs.
  const size_t trb_per_segment_no_alloc = trb_per_segment - 3;
  std::deque<TRB*> pending_trbs;
  // Fill up a segment.
  for (size_t i = 0; i < trb_per_segment_no_alloc; i++) {
    auto context = ring->AllocateContext();
    TRBContext* ref = context.get();
    ASSERT_OK(ring->AddTRB(TRB(), std::move(context)));

    pending_trbs.emplace_back(ref->trb);
  }

  // Move the dequeue pointer forward two steps, to TRB 2.
  for (size_t i = 0; i < 3; i++) {
    std::unique_ptr<TRBContext> ctx;
    ring->CompleteTRB(pending_trbs.front(), &ctx);
    pending_trbs.pop_front();
  }

  // Finish filling up the segment. This will allocate a new link TRB (TRB 0) and we'll start
  // filling up a new segment.
  for (size_t i = 0; i < 4; i++) {
    auto context = ring->AllocateContext();
    TRBContext* ref = context.get();
    ASSERT_OK(ring->AddTRB(TRB(), std::move(context)));

    pending_trbs.emplace_back(ref->trb);
  }

  // Move the dequeue pointer forward again - to TRB 3.
  for (size_t i = 0; i < 1; i++) {
    std::unique_ptr<TRBContext> ctx;
    ring->CompleteTRB(pending_trbs.front(), &ctx);
    pending_trbs.pop_front();
  }

  // This will allocate a new link TRB at 1.
  for (size_t i = 0; i < trb_per_segment_no_alloc; i++) {
    auto context = ring->AllocateContext();
    TRBContext* ref = context.get();
    ASSERT_OK(ring->AddTRB(TRB(), std::move(context)));

    pending_trbs.emplace_back(ref->trb);
  }

  // At this stage, we have 3 TRB segments.
  // Segment 0 looks like this:
  // 0 // link to seg#1,0
  // 1 // link to seg#2,0
  // ...
  // 255 // link to seg#0,0
  //
  // Segment 1 looks like this:
  // 0
  // ...
  // 255 // link to seg#0,1
  //
  // Segment 2 looks like this:
  // 0
  // ...
  // 255 // link to seg#1,2
  //
  // Notice that there are two consecutive links here - one between seg#1,255 -> seg#0,1 and then
  // seg#0,1 -> seg#2,0.

  // Clean up all the pending TRBs.
  while (!pending_trbs.empty()) {
    std::unique_ptr<TRBContext> ctx;
    ASSERT_OK(ring->CompleteTRB(pending_trbs.front(), &ctx));
    pending_trbs.pop_front();
  }

  // Move through, allocating and deallocating TRBs.
  // This will eventually hit the consecutive links.
  for (size_t i = 0; i < 3 * trb_per_segment; i++) {
    auto context = ring->AllocateContext();
    TRBContext* ref = context.get();
    ASSERT_OK(ring->AddTRB(TRB(), std::move(context)));
    std::unique_ptr<TRBContext> ctx;
    ASSERT_OK(ring->CompleteTRB(ref->trb, &ctx));
  }
}

TEST_F(TransferRingHarness, Peek) {
  auto ring = this->ring();
  TRB* trb;
  ring->AllocateTRB(&trb, nullptr);
  auto result = ring->PeekCommandRingControlRegister(0);
  ASSERT_EQ(trb + 1, ring->PhysToVirt(result->PTR()));
  ASSERT_TRUE(result->RCS());
}

TEST_F(TransferRingHarness, First) {
  ContiguousTRBInfo info;
  TRB a;
  TRB b;
  info.trbs = cpp20::span(&a, 1);
  ASSERT_EQ(info.first().data(), &a);
  info.nop = cpp20::span(&b, 1);
  ASSERT_EQ(info.first().data(), &b);
}

}  // namespace usb_xhci
