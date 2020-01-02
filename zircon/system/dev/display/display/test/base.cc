#include "base.h"

#include <lib/async/cpp/task.h>
#include <lib/zx/clock.h>

#include <memory>

#include "../../fake/fake-display.h"
#include "../controller.h"

namespace display {

namespace {

bool RunGivenLoopWithTimeout(async::Loop* loop, zx::duration timeout) {
  // This cannot be a local variable because the delayed task below can execute
  // after this function returns.
  auto canceled = std::make_shared<bool>(false);
  bool timed_out = false;
  async::PostDelayedTask(
      loop->dispatcher(),
      [loop, canceled, &timed_out] {
        if (*canceled) {
          return;
        }
        timed_out = true;
        loop->Quit();
      },
      timeout);
  loop->Run();
  loop->ResetQuit();
  // Another task can call Quit() on the message loop, which exits the
  // message loop before the delayed task executes, in which case |timed_out| is
  // still false here because the delayed task hasn't run yet.
  // Since the message loop isn't destroyed then (as it usually would after
  // Quit()), and presumably can be reused after this function returns we
  // still need to prevent the delayed task to quit it again at some later time
  // using the canceled pointer.
  if (!timed_out) {
    *canceled = true;
  }
  return timed_out;
}

bool RunGivenLoopWithTimeoutOrUntil(async::Loop* loop, fit::function<bool()> condition,
                                    zx::duration timeout, zx::duration step) {
  const zx::time timeout_deadline = zx::deadline_after(timeout);

  while (zx::clock::get_monotonic() < timeout_deadline && loop->GetState() == ASYNC_LOOP_RUNNABLE) {
    if (condition()) {
      loop->ResetQuit();
      return true;
    }

    if (step == zx::duration::infinite()) {
      // Performs a single unit of work, possibly blocking until there is work
      // to do or the timeout deadline arrives.
      loop->Run(timeout_deadline, true);
    } else {
      // Performs work until the step deadline arrives.
      RunGivenLoopWithTimeout(loop, step);
    }
  }

  loop->ResetQuit();
  return condition();
}

}  // namespace

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
  // loop_.StartThread("display::TestBase::loop_", &loop_thrd_);
  loop_thrd_ = thrd_current();
  fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[4], 4);
  protocols[0] = {ZX_PROTOCOL_COMPOSITE,
                  *reinterpret_cast<const fake_ddk::Protocol*>(composite_.proto())};
  protocols[1] = {ZX_PROTOCOL_PBUS, *reinterpret_cast<const fake_ddk::Protocol*>(pbus_.proto())};
  protocols[2] = {ZX_PROTOCOL_PDEV, *reinterpret_cast<const fake_ddk::Protocol*>(pdev_.proto())};
  sysmem_ctx_ = std::make_unique<sysmem_driver::Driver>();
  sysmem_ctx_->dispatcher = loop_.dispatcher();
  sysmem_ctx_->dispatcher_thrd = loop_thrd_;
  sysmem_ = std::make_unique<sysmem_driver::Device>(fake_ddk::kFakeParent, sysmem_ctx_.get());
  protocols[3] = {ZX_PROTOCOL_SYSMEM,
                  *reinterpret_cast<const fake_ddk::Protocol*>(sysmem_->proto())};
  ddk_.SetProtocols(std::move(protocols));
  EXPECT_OK(sysmem_->Bind());
  display_ = new fake_display::FakeDisplay(fake_ddk::kFakeParent);
  ASSERT_OK(display_->Bind(/*start_vsync=*/false));
  ddk_.SetDisplay(display_);

  std::unique_ptr<display::Controller> c(new Controller(display_->zxdev()));
  // Save a copy for test cases.
  controller_ = c.get();
  ASSERT_OK(c->Bind(&c));
}

void TestBase::TearDown() {
  controller_->DdkAsyncRemove();
  display_->DdkAsyncRemove();
  ddk_.DeviceAsyncRemove(const_cast<zx_device_t*>(sysmem_->device()));
  loop_.RunUntilIdle();
  async::PostTask(loop_.dispatcher(),
                  [sysmem = sysmem_.release(), sysmem_ctx = sysmem_ctx_.release()]() {
                    delete sysmem;
                    delete sysmem_ctx;
                  });
  loop_.RunUntilIdle();
  loop_.Shutdown();
  loop_.JoinThreads();
  EXPECT_TRUE(ddk_.Ok());
}

bool TestBase::RunLoopWithTimeoutOrUntil(fit::function<bool()> condition, zx::duration timeout,
                                         zx::duration step) {
  return RunGivenLoopWithTimeoutOrUntil(&loop_, std::move(condition), timeout, step);
}

zx::unowned_channel TestBase::sysmem_fidl() { return ddk_.fidl_loop(sysmem_->device()); }

zx::unowned_channel TestBase::display_fidl() { return ddk_.fidl_loop(controller_->zxdev()); }

}  // namespace display
