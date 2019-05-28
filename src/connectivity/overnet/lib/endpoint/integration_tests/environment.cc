// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/endpoint/integration_tests/environment.h"

namespace overnet {
namespace endpoint_integration_tests {

class StatsDumper final : public StatsVisitor {
 public:
  StatsDumper(const char* indent) : indent_(indent) {}
  void Counter(const char* name, uint64_t value) override {
    OVERNET_TRACE(INFO) << indent_ << name << " = " << value;
  }

 private:
  const char* const indent_;
};

template <class T>
void DumpStats(const char* indent, const T* stats) {
  StatsDumper dumper(indent);
  stats->Accept(&dumper);
}

void DumpStats(const char* label, RouterEndpoint* endpoint) {
  OVERNET_TRACE(INFO) << "STATS DUMP FOR: '" << label << "' -- "
                      << endpoint->node_id();
  endpoint->ForEachLink([endpoint](NodeId target, const Link* link) {
    OVERNET_TRACE(INFO) << "  LINK: " << endpoint->node_id() << "->" << target;
    DumpStats("    ", link->GetStats());
  });
}

static LinkStats AccumulateLinkStats(RouterEndpoint* endpoint) {
  LinkStats out;
  endpoint->ForEachLink([&out](NodeId target, const Link* link) {
    out.Merge(*link->GetStats());
  });
  return out;
}

Env::Env(Optional<Severity> logging) {
  if (logging.has_value()) {
    logging_.Reset(&test_timer_, *logging);
  }
}

uint64_t Env::OutgoingPacketsFromSource() {
  return AccumulateLinkStats(endpoint1()).outgoing_packet_count;
}

uint64_t Env::IncomingPacketsAtDestination() {
  return AccumulateLinkStats(endpoint2()).incoming_packet_count;
}

void Env::AwaitConnected() {
  OVERNET_TRACE(INFO) << "Test waiting for connection between "
                      << endpoint1()->node_id() << " and "
                      << endpoint2()->node_id();
  while (!endpoint1()->HasRouteTo(endpoint2()->node_id()) ||
         !endpoint2()->HasRouteTo(endpoint1()->node_id())) {
    endpoint1()->BlockUntilNoBackgroundUpdatesProcessing();
    endpoint2()->BlockUntilNoBackgroundUpdatesProcessing();
    test_timer_.StepUntilNextEvent();
  }
  OVERNET_TRACE(INFO) << "Test connected";
}

void Env::FlushTodo(std::function<bool()> until, TimeDelta timeout) {
  FlushTodo(until, test_timer_.Now() + timeout);
}

void Env::FlushTodo(std::function<bool()> until, TimeStamp deadline) {
  bool stepped = false;
  while (test_timer_.Now() < deadline) {
    if (until())
      break;
    if (!test_timer_.StepUntilNextEvent())
      break;
    stepped = true;
  }
  if (!stepped) {
    test_timer_.Step(TimeDelta::FromMilliseconds(1).as_us());
  }
  ZX_ASSERT(test_timer_.Now() < deadline);
}

TwoNode::TwoNode(Optional<Severity> logging, const NamedSimulator* simulator,
                 uint64_t node_id_1, uint64_t node_id_2)
    : Env(logging), simulator_(simulator->simulator.get()) {
  endpoint1_ = new RouterEndpoint(timer(), NodeId(node_id_1), false);
  endpoint2_ = new RouterEndpoint(timer(), NodeId(node_id_2), false);
  simulator_->MakeLinks(endpoint1_, endpoint2_, 1, 2);
}

void TwoNode::DumpAllStats() {
  DumpStats("1", endpoint1_);
  DumpStats("2", endpoint2_);
}

TwoNode::~TwoNode() {
  bool done1 = false;
  bool done2 = false;
  endpoint1_->Close(Callback<void>(ALLOCATED_CALLBACK, [&done1, this]() {
    delete endpoint1_;
    done1 = true;
  }));
  endpoint2_->Close(Callback<void>(ALLOCATED_CALLBACK, [&done2, this]() {
    delete endpoint2_;
    done2 = true;
  }));
  FlushTodo([&done1, &done2] { return done1 && done2; });
  ZX_ASSERT(done1);
  ZX_ASSERT(done2);
}

ThreeNode::ThreeNode(Optional<Severity> logging,
                     const NamedSimulator* simulator_1_h,
                     const NamedSimulator* simulator_h_2, uint64_t node_id_1,
                     uint64_t node_id_h, uint64_t node_id_2)
    : Env(logging),
      simulator_1_h_(simulator_1_h->simulator.get()),
      simulator_h_2_(simulator_h_2->simulator.get()) {
  endpoint1_ = new RouterEndpoint(timer(), NodeId(node_id_1), false);
  endpointH_ = new RouterEndpoint(timer(), NodeId(node_id_h), false);
  endpoint2_ = new RouterEndpoint(timer(), NodeId(node_id_2), false);
  simulator_1_h_->MakeLinks(endpoint1_, endpointH_, 1, 2);
  simulator_h_2_->MakeLinks(endpointH_, endpoint2_, 3, 4);
}

void ThreeNode::DumpAllStats() {
  DumpStats("1", endpoint1_);
  DumpStats("H", endpointH_);
  DumpStats("2", endpoint2_);
}

ThreeNode::~ThreeNode() {
  bool done = false;
  endpointH_->Close(Callback<void>(ALLOCATED_CALLBACK, [this, &done]() {
    endpoint1_->Close(Callback<void>(ALLOCATED_CALLBACK, [this, &done]() {
      endpoint2_->Close(Callback<void>(ALLOCATED_CALLBACK, [this, &done]() {
        delete endpoint1_;
        delete endpoint2_;
        delete endpointH_;
        done = true;
      }));
    }));
  }));
  FlushTodo([&done] { return done; });
  ZX_ASSERT(done);
}

}  // namespace endpoint_integration_tests
}  // namespace overnet
