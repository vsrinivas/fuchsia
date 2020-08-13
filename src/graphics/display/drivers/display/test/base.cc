// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base.h"

#include <lib/async/cpp/task.h>
#include <lib/zx/clock.h>

#include <memory>

#include "../../fake/fake-display.h"
#include "../controller.h"
#include "lib/fake_ddk/fake_ddk.h"

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
  if (proto_id == ZX_PROTOCOL_DISPLAY_CLAMP_RGB_IMPL) {
    const auto& p = display_->clamp_rgbimpl_proto();
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
  loop_.StartThread("display::TestBase::loop_", &loop_thrd_);
  fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[4], 4);
  protocols[0] = {ZX_PROTOCOL_COMPOSITE,
                  *reinterpret_cast<const fake_ddk::Protocol*>(composite_.proto())};
  protocols[1] = {ZX_PROTOCOL_PBUS, *reinterpret_cast<const fake_ddk::Protocol*>(pbus_.proto())};
  protocols[2] = {ZX_PROTOCOL_PDEV, *reinterpret_cast<const fake_ddk::Protocol*>(pdev_.proto())};
  sysmem_ctx_ = std::make_unique<sysmem_driver::Driver>();
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
  // FIDL loops must be destroyed first to avoid races between cleanup tasks and loop_.
  ddk_.ShutdownFIDL();

  controller_->DdkAsyncRemove();
  display_->DdkAsyncRemove();
  ddk_.DeviceAsyncRemove(const_cast<zx_device_t*>(sysmem_->device()));
  async::PostTask(loop_.dispatcher(),
                  [this, sysmem = sysmem_.release(), sysmem_ctx = sysmem_ctx_.release()]() {
                    delete sysmem;
                    delete sysmem_ctx;
                    loop_.Quit();
                  });
  // Wait for loop_.Quit() to execute.
  loop_.JoinThreads();
  EXPECT_TRUE(ddk_.Ok());
}

bool TestBase::RunLoopWithTimeoutOrUntil(fit::function<bool()>&& condition, zx::duration timeout,
                                         zx::duration step) {
  ZX_ASSERT(step != zx::duration::infinite());
  const zx::time timeout_deadline = zx::deadline_after(timeout) + step;

  // We can't create a task on the loop that will block, so this task reschedules itself every
  // |step| until |timeout|.
  auto done = new sync_completion_t;
  auto task = new async::Task();
  auto result =
      std::make_shared<std::atomic<bool>>(false);  // Used by this thread and the looping task.
  task->set_handler([c = std::move(condition), result, done, step](
                        async_dispatcher_t* dispatcher, async::Task* self, zx_status_t status) {
    if (sync_completion_signaled(done)) {
      // The client either timed out or noticed the condition signaled.
      delete done;
      delete self;
      return;
    }
    if (c()) {
      result->store(true);
    }
    zx::nanosleep(zx::deadline_after(step));
    if (self->Post(dispatcher) != ZX_OK) {
      zxlogf(INFO, "Deleted task due to dispatcher shutdown");
      delete done;
      delete self;
    }
  });
  if (task->Post(loop_.dispatcher()) != ZX_OK) {
    delete done;
    delete task;
    return false;
  }
  while (zx::clock::get_monotonic() < timeout_deadline) {
    if (result->load()) {
      sync_completion_signal(done);
      return true;
    }
    zx::nanosleep(zx::deadline_after(step));
  }

  sync_completion_signal(done);
  return result->load();
}

zx::unowned_channel TestBase::sysmem_fidl() { return ddk_.fidl_loop(sysmem_->device()); }

zx::unowned_channel TestBase::display_fidl() { return ddk_.fidl_loop(controller_->zxdev()); }

}  // namespace display
