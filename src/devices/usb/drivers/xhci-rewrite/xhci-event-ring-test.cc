// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "xhci-event-ring.h"

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
constexpr size_t kErstMax = 42;
constexpr size_t kShortTransferLength0 = 97;
constexpr size_t kShortTransferLength1 = 102;
constexpr size_t kFinalTransferLength = 87;
constexpr size_t kTransferLengthInclusive = 8162;
constexpr size_t kShortTransferLength = 800;
constexpr size_t kFakeTrb = 0x3924ff0913;
constexpr size_t kFakeTrbVirt = 0x8411487132;

class EventRingHarness : public zxtest::Test {
 public:
  EventRingHarness()
      : trb_context_allocator_(-1, true), hci_(reinterpret_cast<zx_device_t*>(this)) {}
  void SetUp() override {
    constexpr auto kRuntimeRegisterOffset = 6;
    constexpr auto kErdp = 2062;
    region_.emplace(regs_, sizeof(uint32_t), fbl::count_of(regs_));
    buffer_.emplace(region_->GetMmioBuffer());
    regs_[kRuntimeRegisterOffset].SetReadCallback([=]() { return 0x2000; });
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

  void Interrupt() { ring_->HandleIRQ(); }

  void AddTRB(const TRB& trb) {
    auto ptr = ddk_fake::PhysToVirt<TRB*>(erdp_);
    *ptr = trb;
    Control::FromTRB(ptr).set_Cycle(1).ToTrb(ptr);
    erdp_ += sizeof(TRB);
  }

  TRB* trb() { return ddk_fake::PhysToVirt<TRB*>(erdp_); }

  using TestRequest = usb::CallbackRequest<sizeof(max_align_t)>;
  template <typename Callback>
  zx_status_t AllocateRequest(std::optional<TestRequest>* request, uint32_t device_id,
                              uint64_t data_size, uint8_t endpoint, Callback callback) {
    return TestRequest::Alloc(request, data_size, endpoint, hci_.UsbHciGetRequestSize(),
                              std::move(callback));
  }

  std::unique_ptr<TRBContext> AllocateContext() { return trb_context_allocator_.New(); }

  void RequestQueue(usb_request_t* usb_request, const usb_request_complete_t* complete_cb) {
    pending_req_ = Request(usb_request, *complete_cb, sizeof(usb_request_t));
  }

  Request Borrow(TestRequest request) {
    request.Queue(*this);
    return std::move(*pending_req_);
  }

  zx_status_t InitRing(EventRing* ring) {
    ring_ = ring;
    auto regoffset = RuntimeRegisterOffset::Get().ReadFrom(&buffer_.value());
    zx_status_t status = ring->Init(ZX_PAGE_SIZE, kFakeBti, &buffer_.value(), false, kErstMax,
                                    ERSTSZ::Get(regoffset, 0).ReadFrom(&buffer_.value()),
                                    ERDP::Get(regoffset, 0).ReadFrom(&buffer_.value()),
                                    IMAN::Get(regoffset, 0).ReadFrom(&buffer_.value()),
                                    CapLength::Get().ReadFrom(&buffer_.value()).Length(),
                                    HCSPARAMS1::Get().ReadFrom(&buffer_.value()), &command_ring_,
                                    DoorbellOffset::Get().ReadFrom(&buffer_.value()), &hci_,
                                    HCCPARAMS1::Get().ReadFrom(&buffer_.value()), dcbaa_);
    if (status != ZX_OK) {
      return status;
    }
    status = ring_->AddSegmentIfNone();
    if (status != ZX_OK) {
      return status;
    }
    ERDP reg;
    reg.set_Pointer(ddk_fake::PhysToVirt<zx_paddr_t*>(ring_->erst())[0]);
    regs_[2062].Write(reg.reg_value());
    return ZX_OK;
  }
  void SetShortPacketHandler(
      fit::function<void(usb_xhci::TRB*, size_t*, usb_xhci::TRB**, size_t)> handler) {
    short_packet_handler_ = std::move(handler);
  }

  void HandleShortPacket(TRB* short_trb, size_t* transferred, TRB** first_trb,
                         size_t short_length) {
    (*short_packet_handler_)(short_trb, transferred, first_trb, short_length);
  }

  zx_status_t CompleteTRB(TRB* trb, std::unique_ptr<TRBContext>* context) {
    if (trb != expected_completion_) {
      return ZX_ERR_IO;
    }
    *context = pending_contexts_.pop_front();
    return ZX_OK;
  }

  void SetCompletion(TRB* expected) { expected_completion_ = expected; }

  void AddContext(std::unique_ptr<TRBContext> context) {
    pending_contexts_.push_back(std::move(context));
  }

 private:
  std::optional<Request> pending_req_;
  std::optional<fit::function<void(usb_xhci::TRB*, size_t*, usb_xhci::TRB**, size_t)>>
      short_packet_handler_;
  using AllocatorTraits = fbl::InstancedSlabAllocatorTraits<std::unique_ptr<TRBContext>, 4096U>;
  using AllocatorType = fbl::SlabAllocator<AllocatorTraits>;
  AllocatorType trb_context_allocator_;
  TRB* expected_completion_ = nullptr;
  fbl::DoublyLinkedList<std::unique_ptr<TRBContext>> pending_contexts_;
  std::optional<ddk::MmioBuffer> buffer_;
  EventRing* ring_;
  UsbXhci hci_;
  CommandRing command_ring_;
  uint64_t dcbaa_[128];
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
      device_state_[i].GetTransferRing(c).Init(ZX_PAGE_SIZE, kFakeBti, nullptr, false, nullptr,
                                               *this);
    }
  }
  return static_cast<EventRingHarness*>(get_test_harness())->InitRing(&interrupters_[0].ring());
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

zx_status_t TransferRing::Init(size_t page_size, const zx::bti& bti, EventRing* ring, bool is_32bit,
                               ddk::MmioBuffer* mmio, const UsbXhci& hci) {
  fbl::AutoLock _(&mutex_);
  hci_ = &hci;
  return ZX_OK;
}

void UsbXhci::Shutdown(zx_status_t status) {}

zx_status_t TransferRing::HandleShortPacket(TRB* short_trb, size_t* transferred, TRB** first_trb,
                                            size_t short_length) {
  static_cast<EventRingHarness*>(hci_->get_test_harness())
      ->HandleShortPacket(short_trb, transferred, first_trb, short_length);
  return ZX_OK;
}

fbl::DoublyLinkedList<std::unique_ptr<TRBContext>> TransferRing::TakePendingTRBsUntil(TRB* end) {
  fbl::DoublyLinkedList<std::unique_ptr<TRBContext>> empty;
  return empty;
}

fbl::DoublyLinkedList<std::unique_ptr<TRBContext>> TransferRing::TakePendingTRBs() {
  fbl::DoublyLinkedList<std::unique_ptr<TRBContext>> empty;
  return empty;
}

TRBPromise UsbXhci::DeviceOffline(uint32_t slot, TRB* continuation) {
  return ResultToTRBPromise(fit::error(ZX_ERR_NOT_SUPPORTED));
}

TRBPromise EnumerateDevice(UsbXhci* hci, uint8_t port, std::optional<HubInfo> hub_info) {
  return hci->ResultToTRBPromise(fit::error(ZX_ERR_NOT_SUPPORTED));
}

TRB* TransferRing::PhysToVirt(zx_paddr_t paddr) {
  return (paddr == kFakeTrb) ? reinterpret_cast<TRB*>(kFakeTrbVirt) : nullptr;
}

zx_status_t TransferRing::CompleteTRB(TRB* trb, std::unique_ptr<TRBContext>* context) {
  return static_cast<EventRingHarness*>(hci_->get_test_harness())->CompleteTRB(trb, context);
}

TEST_F(EventRingHarness, ShortTransferTest) {
  TRB* start = trb();
  TRB trb;
  trb.ptr = kFakeTrb;
  Control::FromTRB(&trb).set_Type(Control::TransferEvent).ToTrb(&trb);
  TransferEvent* evt = static_cast<TransferEvent*>(&trb);
  evt->set_SlotID(1);
  evt->set_EndpointID(2);
  evt->set_CompletionCode(CommandCompletionEvent::ShortPacket);
  evt->set_TransferLength(kShortTransferLength0);
  AddTRB(trb);
  evt->set_TransferLength(kShortTransferLength1);
  AddTRB(trb);
  evt->set_CompletionCode(CommandCompletionEvent::Success);
  evt->set_TransferLength(kFinalTransferLength);
  AddTRB(trb);
  size_t index = 0;
  TRB* trb_list[2];
  size_t shorts[2];
  SetShortPacketHandler(
      [&](TRB* short_trb, size_t* transferred, usb_xhci::TRB** first_trb, size_t short_length) {
        *first_trb = nullptr;
        trb_list[index] = short_trb;
        shorts[index] = short_length;
        index++;
      });
  auto ctx = AllocateContext();
  size_t transfer_len;
  zx_status_t transfer_status;
  ctx->trb = start;
  std::optional<TestRequest> request;
  AllocateRequest(&request, 1, ZX_PAGE_SIZE * 3, 5, [&](TestRequest request) {
    transfer_status = request.request()->response.status;
    transfer_len = request.request()->response.actual;
  });
  ctx->transfer_len_including_short_trb = kTransferLengthInclusive;
  ctx->short_length = kShortTransferLength;
  ctx->request = Borrow(std::move(*request));
  AddContext(std::move(ctx));
  SetCompletion(reinterpret_cast<TRB*>(kFakeTrbVirt));
  Interrupt();
  ASSERT_OK(transfer_status);
  ASSERT_EQ(transfer_len, kTransferLengthInclusive - kShortTransferLength);
  ASSERT_EQ(shorts[0], kShortTransferLength0);
  ASSERT_EQ(shorts[1], kShortTransferLength1);
  ASSERT_EQ(trb_list[0], reinterpret_cast<TRB*>(kFakeTrbVirt));
  ASSERT_EQ(trb_list[1], reinterpret_cast<TRB*>(kFakeTrbVirt));
}

}  // namespace usb_xhci
