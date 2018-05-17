// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>
#include "bandwidth.h"
#include "callback.h"
#include "optional.h"
#include "timer.h"
#include "windowed_filter.h"

namespace overnet {

class BBR {
 public:
  struct OutgoingPacket {
    uint64_t sequence;
    uint64_t size;
  };

  struct SentPacket {
    OutgoingPacket outgoing;
    uint64_t delivered_bytes_at_send;
    bool in_fast_recovery;
    bool is_app_limited;
    TimeStamp send_time;
    TimeStamp delivered_time_at_send;
  };

  struct Ack {
    std::vector<SentPacket> acked_packets;
    std::vector<SentPacket> nacked_packets;
  };

  static constexpr uint32_t kMaxMSS = 1024 * 1024;

  BBR(Timer* timer, uint32_t mss, Optional<TimeDelta> srtt);

  void RequestTransmit(OutgoingPacket packet,
                       StatusOrCallback<SentPacket> transmit);
  void OnAck(const Ack& ack);

  uint64_t mss() const { return mss_; }

  Bandwidth bottleneck_bandwidth() const {
    return bottleneck_bandwidth_filter_.best_estimate();
  }

  // Reporter should have a Put(name, value) method.
  // ... much like CsvWriter, but we don't include that here so that we can keep
  // that code testonly
  template <class Reporter>
  void ReportState(Reporter* reporter) {
    reporter->Put("now", timer_->Now())
        .Put("state", StateString(state_))
        .Put("recovery", RecoveryString(recovery_))
        .Put("bottlneck_bandwidth",
             bottleneck_bandwidth_filter_.best_estimate())
        .Put("bottlneck_bandwidth_2",
             bottleneck_bandwidth_filter_.second_best_estimate())
        .Put("bottlneck_bandwidth_3",
             bottleneck_bandwidth_filter_.third_best_estimate())
        .Put("cwnd", cwnd_)
        .Put("rtprop", rtprop_)
        .Put("rtprop_stamp", rtprop_stamp_)
        .Put("last_send_time", last_send_time_)
        .Put("pacing_rate", pacing_rate_)
        .Put("pacing_gain", pacing_gain_)
        .Put("cwnd_gain", cwnd_gain_)
        .Put("next_round_delivered_bytes", next_round_delivered_bytes_)
        .Put("round_start", round_start_)
        .Put("filled_pipe", filled_pipe_)
        .Put("rtprop_expired", rtprop_expired_)
        .Put("packet_conservation", packet_conservation_)
        .Put("idle_start", idle_start_)
        .Put("probe_rtt_round_done", probe_rtt_round_done_)
        .Put("full_bw_count", full_bw_count_)
        .Put("cycle_index", cycle_index_)
        .Put("packets_in_flight", packets_in_flight_)
        .Put("round_count", round_count_)
        .Put("delivered_bytes", delivered_bytes_)
        .Put("delivered_seq", delivered_seq_)
        .Put("delivered_time", delivered_time_)
        .Put("target_cwnd", target_cwnd_)
        .Put("prior_cwnd", prior_cwnd_)
        .Put("last_sent_packet", last_sent_packet_)
        .Put("exit_recovery_at_seq", exit_recovery_at_seq_)
        .Put("app_limited_seq", app_limited_seq_)
        .Put("full_bw", full_bw_)
        .Put("cycle_stamp", cycle_stamp_)
        .Put("probe_rtt_done_stamp", probe_rtt_done_stamp_)
        .Put("first_sent_time", first_sent_time_)
        .Put("prior_inflight", prior_inflight_);
  }

 private:
  struct Gain {
    uint16_t numerator;
    uint16_t denominator;

    constexpr uint64_t operator*(uint64_t x) const {
      return numerator * x / denominator;
    }
    constexpr Bandwidth operator*(Bandwidth x) const {
      return Bandwidth::FromBitsPerSecond(*this * x.bits_per_second());
    }
    constexpr Gain Reciprocal() const { return Gain{denominator, numerator}; }
    constexpr bool GreaterThanOne() const { return numerator > denominator; }
    constexpr bool IsOne() const { return numerator == denominator; }

    friend std::ostream& operator<<(std::ostream& out, Gain g) {
      if (g.denominator == 1) return out << g.numerator;
      if (g.IsOne()) return out << "1";
      return out << g.numerator << "/" << g.denominator;
    }
  };

  struct RateSample {
    Bandwidth delivery_rate;
    TimeDelta rtt;
    bool is_app_limited;
  };

  void ValidateState();

  void EnterStartup();
  void EnterDrain();
  void EnterProbeBW(TimeStamp now, const Ack& ack);
  void EnterProbeRTT();
  void ExitProbeRTT(TimeStamp now, const Ack& ack);

  void UpdateModelAndState(TimeStamp now, const Ack& ack);
  void UpdateControlParameters(const Ack& ack);

  void UpdateBtlBw(const Ack& ack, const RateSample& rs);
  void CheckCyclePhase(TimeStamp now, const Ack& ack);
  void CheckFullPipe(const Ack& ack, const RateSample& rs);
  void CheckDrain(TimeStamp now, const Ack& ack);
  void CheckProbeRTT(TimeStamp now, const Ack& ack);
  void UpdateRTprop(TimeStamp now, const Ack& ack, const RateSample& rs);

  void SetPacingRate() { SetPacingRateWithGain(pacing_gain_); }
  void SetCwnd(const Ack& ack);

  void HandleRestartFromIdle();
  void HandleProbeRTT(TimeStamp now, const Ack& ack);

  void UpdateRound(const Ack& ack);
  void UpdateTargetCwnd() {
    target_cwnd_ = std::max(uint64_t(3), Inflight(cwnd_gain_) / mss_);
  }

  void ModulateCwndForRecovery(const Ack& ack);
  void ModulateCwndForProbeRTT();

  void SetPacingRateWithGain(Gain gain);

  void SetFastRecovery(const Ack& ack);

  void AdvanceCyclePhase(TimeStamp now, const Ack& ack);

  uint64_t SendQuantum() const;
  uint64_t Inflight(Gain gain) const;
  bool IsNextCyclePhase(TimeStamp now, const Ack& ack) const;
  Bandwidth PacingRate() const;

  RateSample SampleBandwidth(TimeStamp now, const Ack& ack);

  void SaveCwnd();
  void RestoreCwnd();

  void ScheduleQueuedPacket();

  constexpr static Gain HighGain() { return Gain{2885, 1000}; }
  constexpr static Gain UnitGain() { return Gain{1, 1}; }

  static constexpr int kProbeBWGainCycleLength = 8;
  static const Gain kProbeBWGainCycle[kProbeBWGainCycleLength];

  Timer* timer_;
  TimeDelta rtprop_;
  TimeStamp rtprop_stamp_;
  TimeStamp last_send_time_ = TimeStamp::Epoch();

  enum class State {
    Startup,
    Drain,
    ProbeBW,
    ProbeRTT,
  };

  static const char* StateString(State s) {
    switch (s) {
      case State::Startup:
        return "Startup";
      case State::Drain:
        return "Drain";
      case State::ProbeBW:
        return "ProbeBW";
      case State::ProbeRTT:
        return "ProbeRTT";
    }
    return "<<unknown>>";
  }

  enum class Recovery {
    None,
    Fast,
  };

  static const char* RecoveryString(Recovery r) {
    switch (r) {
      case Recovery::None:
        return "None";
      case Recovery::Fast:
        return "Fast";
    }
    return "<<unknown>>";
  }

  struct QueuedPacket {
    QueuedPacket(OutgoingPacket p, StatusOrCallback<SentPacket> c)
        : packet(p), transmit(std::move(c)) {}
    OutgoingPacket packet;
    StatusOrCallback<SentPacket> transmit;
    Optional<Timeout> timeout;
  };

  Optional<QueuedPacket> queued_packet_;

  const uint32_t mss_;
  WindowedFilter<uint64_t, Bandwidth, MaxFilter> bottleneck_bandwidth_filter_{
      10, 0, Bandwidth::Zero()};
  Gain pacing_gain_ = HighGain();
  Gain cwnd_gain_ = HighGain();
  State state_ = State::Startup;
  Recovery recovery_ = Recovery::None;
  uint64_t cwnd_ = 3;
  Optional<Bandwidth> pacing_rate_;
  uint64_t next_round_delivered_bytes_ = 0;
  bool round_start_ = false;
  bool filled_pipe_ = false;
  bool rtprop_expired_;
  bool packet_conservation_ = false;
  bool idle_start_ = false;
  bool probe_rtt_round_done_ = false;
  bool queued_packet_paused_;
  uint8_t full_bw_count_ = 0;
  uint8_t cycle_index_;
  uint64_t packets_in_flight_ = 0;
  uint64_t round_count_ = 0;
  uint64_t delivered_bytes_ = 0;
  uint64_t delivered_seq_ = 0;
  TimeStamp delivered_time_ = TimeStamp::Epoch();
  uint64_t target_cwnd_;
  uint64_t prior_cwnd_;
  uint64_t last_sent_packet_ = 0;
  uint64_t exit_recovery_at_seq_ = 0;
  uint64_t app_limited_seq_ = 0;
  Bandwidth full_bw_ = Bandwidth::Zero();
  TimeStamp cycle_stamp_ = TimeStamp::Epoch();
  TimeStamp probe_rtt_done_stamp_ = TimeStamp::Epoch();
  TimeStamp first_sent_time_ = TimeStamp::Epoch();
  uint64_t prior_inflight_;
};

}  // namespace overnet
