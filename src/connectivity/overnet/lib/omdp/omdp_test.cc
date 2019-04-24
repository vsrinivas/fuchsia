// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/omdp/omdp.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/connectivity/overnet/lib/environment/trace_cout.h"
#include "src/connectivity/overnet/lib/protocol/fidl.h"
#include "src/connectivity/overnet/lib/testing/flags.h"
#include "src/connectivity/overnet/lib/testing/test_timer.h"

using fuchsia::overnet::omdp::Beacon;
using testing::_;
using testing::AtLeast;
using testing::Invoke;
using testing::Return;
using testing::StrictMock;

namespace overnet {
namespace omdp_nub_test {

class MockOmdpBase {
 public:
  TestTimer* timer() { return &timer_; }
  auto rng() {
    return [this] { return rng_(); };
  }

 private:
  TestTimer timer_;
  std::mt19937 rng_{0};
  TraceCout trace_{&timer_};
  ScopedRenderer trace_render{&trace_};
  ScopedSeverity scoped_severity_{FLAGS_verbose ? Severity::DEBUG
                                                : Severity::INFO};
};

class MockOmdp : public MockOmdpBase, public Omdp {
 public:
  MockOmdp() : Omdp(1, timer(), rng()) {}

  MOCK_METHOD1(Broadcast, void(Slice));
  MOCK_METHOD2(OnNewNode, void(uint64_t, IpAddr));
};

TEST(Omdp, NoOp) { StrictMock<MockOmdp> nub; }

auto bad_input_test = [](auto process_call) {
  StrictMock<MockOmdp> nub;
  EXPECT_CALL(nub, Broadcast(_)).Times(AtLeast(0));
  Status status = process_call(&nub);
  EXPECT_TRUE(status.is_error()) << status;
  // Next process should get caught by block list
  status = process_call(&nub);
  EXPECT_EQ(StatusCode::FAILED_PRECONDITION, status.code()) << status;
  nub.timer()->Step(
      TimeDelta::FromSeconds(Omdp::kBlockTimeSeconds + 1).as_us());
  // After block timeout, final catch should be caught by parsing
  status = process_call(&nub);
  EXPECT_TRUE(status.is_error()) << status;
};

auto good_input_test = [](auto process_call) {
  StrictMock<MockOmdp> nub;
  EXPECT_CALL(nub, Broadcast(_)).Times(AtLeast(0));
  Status status = process_call(&nub);
  EXPECT_TRUE(status.is_ok()) << status;
  status = process_call(&nub);
  EXPECT_TRUE(status.is_ok()) << status;
};

TEST(Omdp, BadParse) {
  bad_input_test([](Omdp* nub) {
    return nub->Process(IpAddr(1, 1, 1, 1, 1),
                        Slice::FromContainer({1, 2, 3, 4}));
  });
}

TEST(Omdp, Self) {
  good_input_test([](Omdp* nub) {
    Beacon beacon{1};
    return nub->Process(IpAddr(1, 1, 1, 1, 1), *Encode(&beacon));
  });
}

TEST(Omdp, Other) {
  good_input_test([](MockOmdp* nub) {
    EXPECT_CALL(*nub, OnNewNode(2, IpAddr(1, 1, 1, 1, 1)));
    Beacon beacon{2};
    return nub->Process(IpAddr(1, 1, 1, 1, 1), *Encode(&beacon));
  });
}

class OmdpNetwork {
 public:
  OmdpNetwork(size_t nubs) {
    for (size_t i = 0; i < nubs; i++) {
      nubs_.emplace_back(new Nub(this, i));
    }
    for (size_t i = 0; i < nubs; i++) {
      nubs_[i]->Start();
    }
  }

  uint64_t broadcasts() const { return broadcasts_; }

  TestTimer* timer() { return &timer_; }

 private:
  class NubBase {
   protected:
    NubBase(size_t i) : rng_(i) {}
    std::mt19937 rng_;
  };

  class Nub final : protected NubBase, public Omdp {
   public:
    Nub(OmdpNetwork* net, size_t i)
        : NubBase(i),
          Omdp(i, &net->timer_, [this]() { return NubBase::rng_(); }),
          net_(net) {}

    void Start() { ScheduleBroadcast(); }

    void OnNewNode(uint64_t node, IpAddr addr) override {}

    void Broadcast(Slice slice) override {
      net_->Broadcast(std::move(slice), this);
    }

   private:
    OmdpNetwork* const net_;
  };

  void Broadcast(Slice slice, Nub* from) {
    broadcasts_++;
    OVERNET_TRACE(DEBUG) << "BROADCAST: " << slice;
    for (const auto& nub : nubs_) {
      if (nub.get() == from) {
        continue;
      }
      auto status = nub->Process(IpAddr(1, 1, 1, 1, from->own_id()), slice);
      EXPECT_TRUE(status.is_ok()) << status;
    }
  }

  TestTimer timer_;
  TraceCout trace_{&timer_};
  ScopedRenderer trace_render{&trace_};
  ScopedSeverity scoped_severity_{FLAGS_verbose ? Severity::DEBUG
                                                : Severity::INFO};
  std::vector<std::unique_ptr<Nub>> nubs_;
  uint64_t broadcasts_ = 0;
};

class OmdpNetworkTest : public ::testing::TestWithParam<size_t> {};

TEST_P(OmdpNetworkTest, NoOp) { OmdpNetwork net(GetParam()); }

TEST_P(OmdpNetworkTest, RateControl) {
  OmdpNetwork net(GetParam());
  // TraceCout trace(net.timer());
  // ScopedRenderer scoped_renderer(&trace);
  while (net.timer()->Now().after_epoch() < TimeDelta::FromMinutes(2)) {
    OVERNET_TRACE(DEBUG) << "Broadcasts=" << net.broadcasts();
    ASSERT_TRUE(net.timer()->StepUntilNextEvent());
  }
  const auto broadcasts_at_start = net.broadcasts();
  const auto time_at_start = net.timer()->Now();
  while (net.timer()->Now() < time_at_start + TimeDelta::FromMinutes(2)) {
    OVERNET_TRACE(DEBUG) << "Broadcasts=" << net.broadcasts();
    ASSERT_TRUE(net.timer()->StepUntilNextEvent());
  }
  const auto broadcasts_at_end = net.broadcasts();
  const auto time_at_end = net.timer()->Now();
  const auto rate = (1000000.0 * (broadcasts_at_end - broadcasts_at_start) /
                     (time_at_end - time_at_start).as_us());
  std::cout << "Broadcast rate=" << rate << "\n";
  EXPECT_LE(rate, 5);
}

INSTANTIATE_TEST_SUITE_P(OmdpNet, OmdpNetworkTest,
                         testing::Values(1, 3, 10, 31, 100, 316, 1000, 3162,
                                         10000));

}  // namespace omdp_nub_test
}  // namespace overnet
