// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <src/lib/fxl/log_settings.h>
#include "src/connectivity/overnet/overnetstack/fuchsia_port.h"
#include "src/connectivity/overnet/overnetstack/mdns.h"
#include "src/connectivity/overnet/overnetstack/overnet_app.h"
#include "src/connectivity/overnet/overnetstack/service.h"
#include "src/connectivity/overnet/overnetstack/udp_nub.h"

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
    OVERNET_TRACE(DEBUG) << "FIRE TIMEOUT";
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
  FuchsiaLog(overnet::Timer* timer) : timer_(timer) {}
  void Render(overnet::TraceOutput output) override {
    auto severity = [sev = output.severity] {
      switch (sev) {
        case overnet::Severity::DEBUG:
          return -2;
        case overnet::Severity::TRACE:
          return -1;
        case overnet::Severity::INFO:
          return fxl::LOG_INFO;
        case overnet::Severity::WARNING:
          return fxl::LOG_WARNING;
        case overnet::Severity::ERROR:
          return fxl::LOG_ERROR;
      }
    }();
    fxl::LogMessage message(severity, output.file, output.line, nullptr);
    message.stream() << timer_->Now() << " " << output.message;
    bool annotated = false;
    auto maybe_begin_annotation = [&] {
      if (annotated) {
        return;
      }
      annotated = true;
      message.stream() << " //";
    };
    output.scopes.Visit([&](overnet::Module module, void* ptr) {
      maybe_begin_annotation();
      message.stream() << ' ' << module << ':' << ptr;
    });
  }
  void NoteParentChild(overnet::Op, overnet::Op) override {}

 private:
  overnet::Timer* const timer_;
};

}  // namespace overnetstack

int main(int argc, const char** argv) {
  {
    auto settings = fxl::GetLogSettings();
    settings.min_log_level = -1;
    fxl::SetLogSettings(settings);
  }
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  overnetstack::FuchsiaTimer fuchsia_timer;
  overnetstack::FuchsiaLog fuchsia_log(&fuchsia_timer);
  overnet::ScopedRenderer scoped_renderer(&fuchsia_log);
  overnet::ScopedSeverity scoped_severity(overnet::Severity::INFO);
  overnetstack::OvernetApp app(&fuchsia_timer);
  app.InstantiateActor<overnetstack::Service>();
  auto* udp_nub = app.InstantiateActor<overnetstack::UdpNub>();
  app.InstantiateActor<overnetstack::MdnsIntroducer>(udp_nub);
  app.InstantiateActor<overnetstack::MdnsAdvertisement>(udp_nub);
  auto status = app.Start().Then([&]() {
    switch (auto status = loop.Run()) {
      case ZX_OK:
      case ZX_ERR_CANCELED:
        return overnet::Status::Ok();
      default:
        return overnet::Status::FromZx(status).WithContext("RunLoop");
    }
  });
  if (status.is_ok()) {
    return 0;
  } else {
    OVERNET_TRACE(ERROR) << "Failed to start overnetstack: " << status << "\n";
    return static_cast<int>(status.code());
  }
}
