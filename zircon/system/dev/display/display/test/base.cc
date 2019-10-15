#include "base.h"

#include <memory>

#include "../../fake/fake-display.h"
#include "../controller.h"

namespace display {

void TestBase::SetUp() {
  fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[3], 3);
  protocols[0] = {ZX_PROTOCOL_COMPOSITE,
                  *reinterpret_cast<const fake_ddk::Protocol*>(composite_.proto())};
  protocols[1] = {ZX_PROTOCOL_PDEV, *reinterpret_cast<const fake_ddk::Protocol*>(pdev_.proto())};
  protocols[2] = {ZX_PROTOCOL_SYSMEM,
                  *reinterpret_cast<const fake_ddk::Protocol*>(sysmem_.proto())};
  ddk_.SetProtocols(std::move(protocols));
  auto display = new fake_display::FakeDisplay(fake_ddk::kFakeParent);
  ASSERT_OK(display->Bind(false));
  ddk_.SetDisplay(display);

  std::unique_ptr<display::Controller> c(new Controller(dc_parent()));
  // Save a copy for test cases.
  controller_ = c.get();
  ASSERT_OK(c->Bind(&c));
}

void TestBase::TearDown() {
  ddk_.DeviceAsyncRemove(fake_ddk::kFakeDevice);
  ddk_.WaitUntilRemove();
  EXPECT_TRUE(ddk_.Ok());
}

}  // namespace display
