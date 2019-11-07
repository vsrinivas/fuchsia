#include "base.h"

#include <memory>

#include "../../fake/fake-display.h"
#include "../controller.h"

namespace display {

zx_status_t Binder::DeviceGetProtocol(const zx_device_t* device, uint32_t proto_id,
                                      void* protocol) {
  auto out = reinterpret_cast<fake_ddk::Protocol*>(protocol);
  if (proto_id == ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL) {
    const auto& p = display_->dcimpl_proto();
    out->ops = p->ops;
    out->ctx = p->ctx;
    return ZX_OK;
  }
  for (const auto& proto : protocols_) {
    if (proto_id == proto.id) {
      out->ops = proto.proto.ops;
      out->ctx = proto.proto.ctx;
      return ZX_OK;
    }
  }
  return ZX_ERR_NOT_SUPPORTED;
}

zx_device_t* Binder::display() { return display_->zxdev(); }

void TestBase::SetUp() {
  fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[3], 3);
  protocols[0] = {ZX_PROTOCOL_COMPOSITE,
                  *reinterpret_cast<const fake_ddk::Protocol*>(composite_.proto())};
  protocols[1] = {ZX_PROTOCOL_PDEV, *reinterpret_cast<const fake_ddk::Protocol*>(pdev_.proto())};
  protocols[2] = {ZX_PROTOCOL_SYSMEM,
                  *reinterpret_cast<const fake_ddk::Protocol*>(sysmem_.proto())};
  ddk_.SetProtocols(std::move(protocols));
  display_ = new fake_display::FakeDisplay(fake_ddk::kFakeParent);
  ASSERT_OK(display_->Bind(false));
  ddk_.SetDisplay(display_);

  std::unique_ptr<display::Controller> c(new Controller(display_->zxdev()));
  // Save a copy for test cases.
  controller_ = c.get();
  ASSERT_OK(c->Bind(&c));
}

void TestBase::TearDown() {
  zxlogf(INFO, "display = %p\ncontroller = %p\n", display_->zxdev(), controller_->zxdev());
  display_->DdkAsyncRemove();
  EXPECT_TRUE(ddk_.Ok());
}

}  // namespace display
