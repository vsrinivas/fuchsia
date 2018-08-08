// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/overnet/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/task.h>
#include "fuchsia_port.h"
#include "garnet/lib/overnet/router_endpoint.h"
#include "lib/component/cpp/startup_context.h"
#include "mdns.h"

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
    FireTimeout(static_cast<Task*>(task)->timeout, ToOvernetStatus(status));
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

overnet::NodeId GenerateNodeId() {
  uint64_t out;
  zx_cprng_draw(&out, sizeof(out));
  return overnet::NodeId(out);
}

class OvernetApp final : public fuchsia::overnet::Overnet {
 public:
  OvernetApp() : context_(component::StartupContext::CreateFromStartupInfo()) {
    context_->outgoing().AddPublicService(bindings_.GetHandler(this));
  }

  overnet::Status Start() {
    return udp_nub_.Start().Then([this]() {
      mdns_advert_ =
          std::make_unique<MdnsAdvertisement>(context_.get(), &udp_nub_);
      RunMdnsIntroducer(context_.get(), &udp_nub_);
      return overnet::Status::Ok();
    });
  }

  //////////////////////////////////////////////////////////////////////////////////////////////////
  // Method implementations

  void ListPeers(ListPeersCallback callback) override {
    using Peer = fuchsia::overnet::Peer;
    std::vector<Peer> response;
    endpoint_.ForEachPeer([&response](overnet::NodeId node) {
      response.emplace_back(Peer{node.get()});
    });
    callback(fidl::VectorPtr<Peer>(std::move(response)));
  }

 private:
  FuchsiaTimer timer_;
  std::unique_ptr<component::StartupContext> context_;
  fidl::BindingSet<fuchsia::overnet::Overnet> bindings_;
  overnet::RouterEndpoint endpoint_{&timer_, GenerateNodeId(), true};
  UdpNub udp_nub_{&endpoint_};
  std::unique_ptr<MdnsAdvertisement> mdns_advert_;
};

}  // namespace overnetstack

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  overnetstack::OvernetApp app;
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
