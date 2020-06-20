// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "xhci-transfer-ring.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fit/bridge.h>
#include <lib/fit/promise.h>

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
      : trb_context_allocator_(-1, true), hci_(reinterpret_cast<zx_device_t*>(this)) {}
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
    hci_.set_test_harness(this);
    ASSERT_OK(hci_.InitThread(ddk_fake::CreateBufferFactory()));
  }

  void TearDown() override {}

  using TestRequest = usb::CallbackRequest<sizeof(max_align_t)>;
  template <typename Callback>
  zx_status_t AllocateRequest(std::optional<TestRequest>* request, uint32_t device_id,
                              uint64_t data_size, uint8_t endpoint, Callback callback) {
    return TestRequest::Alloc(request, data_size, endpoint, hci_.UsbHciGetRequestSize(),
                              std::move(callback));
  }

  void RequestQueue(usb_request_t* usb_request, const usb_request_complete_t* complete_cb) {
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
  std::optional<ddk::MmioBuffer> buffer_;
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

zx_status_t UsbXhci::InitThread(std::unique_ptr<dma_buffer::BufferFactory> factory) {
  fbl::AllocChecker ac;
  interrupters_.reset(new (&ac) Interrupter[1]);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  buffer_factory_ = std::move(factory);
  max_slots_ = 32;
  device_state_.reset(new (&ac) DeviceState[max_slots_]);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  for (size_t i = 0; i < max_slots_; i++) {
    fbl::AutoLock l(&device_state_[i].transaction_lock());
    for (size_t c = 0; c < max_slots_; c++) {
      zx_status_t status = device_state_[i].GetTransferRing(c).Init(
          ZX_PAGE_SIZE, kFakeBti,
          &static_cast<TransferRingHarness*>(get_test_harness())->event_ring(), false, nullptr,
          *this);
      if (status != ZX_OK) {
        return status;
      }
    }
  }
  fbl::AutoLock l(&device_state_[0].transaction_lock());
  static_cast<TransferRingHarness*>(get_test_harness())
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
                                 const usb_request_complete_t* complete_cb) {}

zx_status_t EventRing::AddSegmentIfNone() { return ZX_OK; }

void UsbXhci::Shutdown(zx_status_t status) {}

void EventRing::RemovePressure() {}

TRBPromise UsbXhci::DeviceOffline(uint32_t slot, TRB* continuation) {
  return ResultToTRBPromise(fit::error(ZX_ERR_NOT_SUPPORTED));
}

TRBPromise EnumerateDevice(UsbXhci* hci, uint8_t port, std::optional<HubInfo> hub_info) {
  return hci->ResultToTRBPromise(fit::error(ZX_ERR_NOT_SUPPORTED));
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
  info.trbs = fbl::Span(&a, 1);
  ASSERT_EQ(info.first().data(), &a);
  info.nop = fbl::Span(&b, 1);
  ASSERT_EQ(info.first().data(), &b);
}

}  // namespace usb_xhci
