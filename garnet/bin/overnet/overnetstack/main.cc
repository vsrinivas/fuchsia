// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/overnet/overnetstack/fuchsia_port.h"
#include "garnet/bin/overnet/overnetstack/mdns.h"
#include "garnet/bin/overnet/overnetstack/overnet_app.h"
#include "garnet/bin/overnet/overnetstack/service.h"
#include "garnet/bin/overnet/overnetstack/udp_nub.h"

namespace overnetstack {

class FuchsiaTimer final : public overnet::Timer {
 public:
  overnet::TimeStamp Now() override {
    return ToTimeStamp(async::Now(dispatcher_));
  }

 private:
  async_dispatcher_t* dispatcher_ = async_get_default_dispatcher();

  struct Task : public async_task_t {
    overnet::Timeout* timeout;
  };

  static void TaskHandler(async_dispatcher_t* async, async_task_t* task,
                          zx_status_t status) {
    FireTimeout(static_cast<Task*>(task)->timeout,
                overnet::Status::FromZx(status));
  }

  void InitTimeout(overnet::Timeout* timeout,
                   overnet::TimeStamp when) override {
    auto* async_timeout = TimeoutStorage<Task>(timeout);
    async_timeout->state = {ASYNC_STATE_INIT};
    async_timeout->handler = TaskHandler;
    async_timeout->deadline = FromTimeStamp(when).get();
    async_timeout->timeout = timeout;
    if (async_post_task(dispatcher_, async_timeout) != ZX_OK) {
      FireTimeout(timeout, overnet::Status::Cancelled());
    }
  }
  void CancelTimeout(overnet::Timeout* timeout,
                     overnet::Status status) override {
    if (async_cancel_task(dispatcher_, TimeoutStorage<Task>(timeout)) ==
        ZX_OK) {
      FireTimeout(timeout, overnet::Status::Cancelled());
    }
  }
};

class FuchsiaLog final : public overnet::TraceRenderer {
 public:
  void Render(overnet::TraceOutput output) override {
    auto severity = [sev = output.severity] {
      switch (sev) {
        case overnet::Severity::DEBUG:
          return fxl::LOG_INFO;
        case overnet::Severity::INFO:
          return fxl::LOG_INFO;
        case overnet::Severity::WARNING:
          return fxl::LOG_WARNING;
        case overnet::Severity::ERROR:
          return fxl::LOG_ERROR;
      }
    }();
    fxl::LogMessage(severity, output.file, output.line, nullptr).stream()
        << output.message;
  }
  void NoteParentChild(overnet::Op, overnet::Op) override {}
};

}  // namespace overnetstack

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  overnetstack::FuchsiaLog fuchsia_log;
  overnet::ScopedRenderer scoped_renderer(&fuchsia_log);
  overnet::ScopedSeverity scoped_severity(overnet::Severity::INFO);
  overnetstack::FuchsiaTimer fuchsia_timer;
  overnetstack::OvernetApp app(&fuchsia_timer);
  app.InstantiateActor<overnetstack::Service>();
  auto* udp_nub = app.InstantiateActor<overnetstack::UdpNub>();
  app.InstantiateActor<overnetstack::MdnsIntroducer>(udp_nub);
  app.InstantiateActor<overnetstack::MdnsAdvertisement>(udp_nub);
  auto status = app.Start().Then([&]() {
    loop.Run();
    return overnet::Status::Ok();
  });
  if (status.is_ok()) {
    return 0;
  } else {
    std::cerr << "Failed to start overnetstack: " << status << "\n";
    return static_cast<int>(status.code());
  }
}
