// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bbr.h"
#include <fstream>
#include <functional>
#include <queue>
#include "csv_writer.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test_timer.h"

using testing::AllOf;
using testing::Ge;
using testing::Le;

namespace overnet {
namespace bbr_test {

// Toggle to true to generate CSV files for each simulation run
enum class CsvOutput {
  None,
  Disk,
  Stdout,
};
static constexpr CsvOutput kCsvOutput = CsvOutput::None;

class Meter {
 public:
  explicit Meter(TimeDelta window) : window_(window) {}

  void Push(TimeStamp now, uint64_t sample) {
    Flush(now);
    samples_.push(Sample{now, sample});
    sum_ += sample;
  }

  Bandwidth Evaluate(TimeStamp now) {
    Flush(now);
    if (samples_.empty()) return Bandwidth::Zero();
    return Bandwidth::BytesPerTime(
        sum_, std::max(window_, now - samples_.front().when));
  }

  size_t Samples() const { return samples_.size(); }

 private:
  const TimeDelta window_;
  struct Sample {
    TimeStamp when;
    uint64_t sample;
  };
  std::queue<Sample> samples_;
  uint64_t sum_ = 0;

  void Flush(TimeStamp now) {
    while (samples_.size() > 3 && samples_.front().when + window_ < now) {
      sum_ -= samples_.front().sample;
      samples_.pop();
    }
  }
};

class BandwidthGate {
 public:
  BandwidthGate(Timer* timer) : timer_(timer) {}

  void SetBandwidth(Bandwidth bandwidth) { bandwidth_ = bandwidth; }

  void Push(uint64_t packet_size, StatusCallback ready) {
    if (pushing_) return;
    pushing_ = true;
    timer_->At(timer_->Now() + bandwidth_.SendTimeForBytes(packet_size),
               [this, ready = std::move(ready)]() mutable {
                 pushing_ = false;
                 ready(Status::Ok());
               });
  }

  bool pushing() const { return pushing_; }

 private:
  Timer* const timer_;
  Bandwidth bandwidth_ = Bandwidth::FromKilobitsPerSecond(1000);
  bool pushing_ = false;
};

class Simulator {
 public:
  Simulator(uint32_t mss, Optional<TimeDelta> srtt)
      : bbr_(&timer_, mss, srtt),
        outgoing_meter_(TimeDelta::FromMilliseconds(100)) {}

  void SetBottleneckBandwidth(Bandwidth bandwidth) {
    bottleneck_.SetBandwidth(bandwidth);
  }
  void SetRoundTripTime(TimeDelta rtt) {
    half_rtt_ = TimeDelta::FromMicroseconds(rtt.as_us() / 2);
  }
  void SetBandwidthWindow(TimeDelta window) { bottleneck_window_ = window; }
  void SetAckDelay(TimeDelta ack_delay) { ack_delay_ = ack_delay; }

  void AddTrafficBurst(int packets, int packet_size) {
    for (int i = 0; i < packets; i++) {
      SendPacket(packet_size, []() {});
    }
  }
  void AddContinuousTraffic(int packet_size, Bandwidth bandwidth,
                            TimeStamp end) {
    auto next_packet = timer_.Now() + bandwidth.SendTimeForBytes(packet_size);
    SendPacket(packet_size, [=]() {
      auto now = timer_.Now();
      if (now > end) return;
      timer_.At(next_packet,
                [=]() { AddContinuousTraffic(packet_size, bandwidth, end); });
    });
  }

  void Step() { timer_.StepUntilNextEvent(); }

  Timer* timer() { return &timer_; }
  BBR* bbr() { return &bbr_; }

  Bandwidth outgoing_bandwidth() {
    return outgoing_meter_.Evaluate(timer_.Now());
  }

  size_t outgoing_bandwidth_samples() const {
    return outgoing_meter_.Samples();
  }

  uint64_t packets_dropped() const { return packets_dropped_; }
  uint64_t packets_passed() const { return packets_passed_; }

  bool bottleneck_blocked() const { return bottleneck_.pushing(); }

 private:
  // Send a packet through the simulator, and call then() once it's sent.
  template <class F>
  void SendPacket(int packet_size, F then) {
    bbr_.RequestTransmit(
        BBR::OutgoingPacket{next_seq_++, uint64_t(packet_size)},
        StatusOrCallback<BBR::SentPacket>(
            ALLOCATED_CALLBACK, [=](const StatusOr<BBR::SentPacket>& status) {
              if (status.is_ok()) {
                then();
                SimulatePacket(*status.get());
              }
            }));
  }

  void SimulatePacket(BBR::SentPacket pkt) {
    auto now = timer_.Now();
    outgoing_meter_.Push(now, pkt.outgoing.size);
    // Push the packet onto the bottleneck link, and wait for it to pass through
    // or be dropped.
    bottleneck_.Push(
        pkt.outgoing.size,
        StatusCallback(ALLOCATED_CALLBACK, [this, pkt](const Status& status) {
          bool allow = status.is_ok();
          TimeStamp now = timer_.Now();
          // Count statistics.
          if (allow) {
            packets_passed_++;
          } else {
            packets_dropped_++;
          }
          // After 1/2-rtt the packet will return to sender, notify the sender
          // with an ack or nack.
          timer_.At(now + half_rtt_, [this, pkt, allow]() {
            (allow ? &ack_packets_ : &nack_packets_)->push_back(pkt);
            // Batch up acks and nacks a little bit to simulate real networks.
            if (!ack_packets_.empty() && !ack_scheduled_) {
              ack_scheduled_ = true;
              timer_.At(timer_.Now() + ack_delay_, [this]() {
                ack_scheduled_ = false;
                BBR::Ack ack{std::move(ack_packets_), std::move(nack_packets_)};
                std::sort(
                    ack.acked_packets.begin(), ack.acked_packets.end(),
                    [](const BBR::SentPacket& a, const BBR::SentPacket& b) {
                      return a.outgoing.sequence < b.outgoing.sequence;
                    });
                std::sort(
                    ack.nacked_packets.begin(), ack.nacked_packets.end(),
                    [](const BBR::SentPacket& a, const BBR::SentPacket& b) {
                      return a.outgoing.sequence < b.outgoing.sequence;
                    });
                ack_packets_.clear();
                nack_packets_.clear();
                bbr_.OnAck(ack);
              });
            }
          });
        }));
  }

  TestTimer timer_;
  BBR bbr_;

  BandwidthGate bottleneck_{&timer_};
  Meter outgoing_meter_;
  uint64_t packets_dropped_ = 0;
  uint64_t packets_passed_ = 0;

  uint64_t next_seq_ = 1;

  std::vector<BBR::SentPacket> ack_packets_;
  std::vector<BBR::SentPacket> nack_packets_;
  bool ack_scheduled_ = false;

  TimeDelta half_rtt_ = TimeDelta::FromMilliseconds(1);
  TimeDelta bottleneck_window_ = TimeDelta::FromMilliseconds(100);
  TimeDelta ack_delay_ = TimeDelta::FromMilliseconds(1);
};

struct Action {
  TimeDelta when;
  std::function<void(Simulator*)> what;
  std::function<void(std::ostream&)> explain;
};

struct SimulationArgs {
  Bandwidth bottleneck_bandwidth;
  TimeDelta rtt;
  uint32_t mss;
  Optional<TimeDelta> srtt;
  std::vector<Action> actions;
};

// Some handy actions
Action MeasureBandwidth(TimeDelta when, Bandwidth min, Bandwidth max) {
  return Action{when,
                [=](Simulator* sim) {
                  EXPECT_THAT(sim->outgoing_bandwidth(),
                              AllOf(Ge(min), Le(max)));
                },
                [=](std::ostream& out) { out << "measure@" << when; }};
}

Action ContinuousTraffic(TimeDelta start, TimeDelta stop, Bandwidth amt,
                         int packet_size) {
  return Action{start,
                [=](Simulator* sim) {
                  sim->AddContinuousTraffic(
                      packet_size, amt, sim->timer()->Now() + (stop - start));
                },
                [=](std::ostream& out) {
                  out << "output " << amt << ":" << packet_size << "@" << start
                      << " for " << (stop - start);
                }};
}

std::ostream& operator<<(std::ostream& out, const SimulationArgs& args) {
  out << "Sim {btlbw=" << args.bottleneck_bandwidth << "; rtt=" << args.rtt
      << "; mss=" << args.mss << "; srtt=" << args.srtt;
  for (const auto& a : args.actions) {
    out << "; ";
    a.explain(out);
  }
  out << "}";
  return out;
}

class SimulationTest : public ::testing::TestWithParam<SimulationArgs> {};

TEST_P(SimulationTest, SimulationSucceeds) {
  Simulator sim(GetParam().mss, GetParam().srtt);
  sim.SetBottleneckBandwidth(GetParam().bottleneck_bandwidth);
  sim.SetRoundTripTime(GetParam().rtt);
  TimeDelta last_action = TimeDelta::Zero();
  for (const auto& action : GetParam().actions) {
    last_action = std::max(last_action, action.when);
    sim.timer()->At(sim.timer()->Now() + action.when,
                    [fn = action.what, sim = &sim]() { fn(sim); });
  }
  bool done = false;
  sim.timer()->At(sim.timer()->Now() + last_action + TimeDelta::FromSeconds(10),
                  [&done]() { done = true; });
  std::unique_ptr<CsvWriter> writer;
  if (kCsvOutput != CsvOutput::None) {
    writer.reset(new CsvWriter());
  }
  while (!done) {
    if (writer) {
      writer->Put("now", sim.timer()->Now())
          .Put("bottleneck_blocked", sim.bottleneck_blocked())
          .Put("outgoing_bandwidth", sim.outgoing_bandwidth())
          .Put("outgoing_bandwidth_samples", sim.outgoing_bandwidth_samples())
          .Put("packets_dropped", sim.packets_dropped())
          .Put("packets_passed", sim.packets_passed());
      sim.bbr()->ReportState(writer.get());
      writer->EndRow();
    }
    sim.Step();
  }
  switch (kCsvOutput) {
    case CsvOutput::Disk: {
      std::string name;
      for (char c : std::string(::testing::UnitTest::GetInstance()
                                    ->current_test_info()
                                    ->name())) {
        if (c == '/')
          name += '.';
        else
          name += c;
      }
      name += ".csv";
      std::cout << "Writing simulation log to " << name << "\n";
      std::ofstream out(name.c_str());
      writer->Flush(out);
    } break;
    case CsvOutput::Stdout:
      writer->Flush(std::cout);
      break;
    case CsvOutput::None:
      break;
  }
}

std::vector<SimulationArgs> GenerateArguments() {
  std::vector<SimulationArgs> args;
  for (auto bottleneck_bw : {1, 10, 100, 1000, 10000}) {
    for (auto rtt : {1, 10, 100, 1000}) {
      for (auto generate_traffic : {1, 10, 100, 1000, 10000}) {
        const auto expect_bw = std::min(bottleneck_bw, generate_traffic);
        const uint64_t mss = 1500;
        args.push_back(SimulationArgs{
            Bandwidth::FromKilobitsPerSecond(bottleneck_bw),
            TimeDelta::FromMilliseconds(rtt),
            mss,
            Nothing,
            {ContinuousTraffic(
                 TimeDelta::Zero(), TimeDelta::FromSeconds(100),
                 Bandwidth::FromKilobitsPerSecond(generate_traffic),
                 std::max(
                     uint64_t(1),
                     std::min(mss, Bandwidth::FromKilobitsPerSecond(expect_bw)
                                       .BytesSentForTime(
                                           TimeDelta::FromMilliseconds(100))))),
             MeasureBandwidth(
                 TimeDelta::FromSeconds(90),
                 Bandwidth::FromBitsPerSecond(expect_bw * 500),
                 Bandwidth::FromBitsPerSecond(expect_bw * 2000))}});
      }
    }
  }
  return args;
}

INSTANTIATE_TEST_CASE_P(BBR, SimulationTest,
                        ::testing::ValuesIn(GenerateArguments()));

}  // namespace bbr_test
}  // namespace overnet
