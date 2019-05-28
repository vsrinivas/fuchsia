// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/connectivity/overnet/lib/endpoint/router_endpoint.h"
#include "src/connectivity/overnet/lib/environment/trace_cout.h"
#include "src/connectivity/overnet/lib/testing/test_timer.h"

namespace overnet {
namespace endpoint_integration_tests {

class Env {
 public:
  explicit Env(Optional<Severity> logging);
  virtual ~Env() {}

  virtual RouterEndpoint* endpoint1() = 0;
  virtual RouterEndpoint* endpoint2() = 0;

  virtual void DumpAllStats() = 0;

  uint64_t OutgoingPacketsFromSource();
  uint64_t IncomingPacketsAtDestination();
  void AwaitConnected();

  void FlushTodo(std::function<bool()> until,
                 TimeDelta timeout = TimeDelta::FromMinutes(10));
  void FlushTodo(std::function<bool()> until, TimeStamp deadline);

  Timer* timer() { return &test_timer_; }

 private:
  TestTimer test_timer_;

  struct Logging {
    Logging(Timer* timer, Severity severity)
        : trace_cout_(timer), scoped_severity_(severity) {}
    TraceCout trace_cout_;
    ScopedRenderer scoped_renderer_{&trace_cout_};
    ScopedSeverity scoped_severity_;
  };
  Optional<Logging> logging_;
};

class Simulator {
 public:
  virtual ~Simulator() = default;
  virtual void MakeLinks(RouterEndpoint* a, RouterEndpoint* b, uint64_t id1,
                         uint64_t id2) const = 0;
};

struct NamedSimulator {
  std::string name;
  std::unique_ptr<Simulator> simulator;
};

class TwoNode final : public Env {
 public:
  TwoNode(Optional<Severity> logging, const NamedSimulator* simulator,
          uint64_t node_id_1, uint64_t node_id_2);

  void DumpAllStats() override;
  virtual ~TwoNode();

  RouterEndpoint* endpoint1() override { return endpoint1_; }
  RouterEndpoint* endpoint2() override { return endpoint2_; }

 private:
  const Simulator* const simulator_;
  RouterEndpoint* endpoint1_;
  RouterEndpoint* endpoint2_;
};

class ThreeNode final : public Env {
 public:
  ThreeNode(Optional<Severity> logging, const NamedSimulator* simulator_1_h,
            const NamedSimulator* simulator_h_2, uint64_t node_id_1,
            uint64_t node_id_h, uint64_t node_id_2);

  void DumpAllStats() override;
  virtual ~ThreeNode();

  RouterEndpoint* endpoint1() override { return endpoint1_; }
  RouterEndpoint* endpoint2() override { return endpoint2_; }

 private:
  const Simulator* const simulator_1_h_;
  const Simulator* const simulator_h_2_;
  RouterEndpoint* endpoint1_;
  RouterEndpoint* endpointH_;
  RouterEndpoint* endpoint2_;
};

class MakeEnvInterface {
 public:
  virtual const char* name() const = 0;
  virtual std::shared_ptr<Env> Make(Optional<Severity> logging) const = 0;
};

using MakeEnv = std::shared_ptr<MakeEnvInterface>;

template <class Impl>
class InProcessLink final : public Link {
 public:
  template <class... Arg>
  InProcessLink(Arg&&... args) : impl_(new Impl(std::forward<Arg>(args)...)) {}

  std::shared_ptr<Impl> get() { return impl_; }

  void Close(Callback<void> quiesced) override {
    impl_->Close(Callback<void>(
        ALLOCATED_CALLBACK,
        [this, quiesced = std::move(quiesced)]() mutable { impl_.reset(); }));
  }
  void Forward(Message message) override { impl_->Forward(std::move(message)); }
  fuchsia::overnet::protocol::LinkStatus GetLinkStatus() override {
    return impl_->GetLinkStatus();
  }
  const LinkStats* GetStats() const override { return impl_->GetStats(); }

 private:
  std::shared_ptr<Impl> impl_;
};

}  // namespace endpoint_integration_tests
}  // namespace overnet
