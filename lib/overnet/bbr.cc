// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bbr.h"
#include <iostream>

namespace overnet {

static constexpr uint64_t kMinPipeCwndSegments = 4;
static constexpr auto kRTpropFilterLength = TimeDelta::FromSeconds(10);
static constexpr auto kProbeRTTDuration = TimeDelta::FromMilliseconds(200);
static constexpr TimeDelta kMinRTT = TimeDelta::FromMicroseconds(1);

const BBR::Gain BBR::kProbeBWGainCycle[kProbeBWGainCycleLength] = {
    {5, 4},     {3, 4},     UnitGain(), UnitGain(),
    UnitGain(), UnitGain(), UnitGain(), UnitGain(),
};

BBR::BBR(Timer* timer, uint32_t mss, Optional<TimeDelta> srtt)
    : timer_(timer),
      rtprop_(srtt.ValueOr(TimeDelta::PositiveInf())),
      rtprop_stamp_(timer->Now()),
      mss_(mss) {
  UpdateTargetCwnd();
  ValidateState();
}

void BBR::ValidateState() { assert(cwnd_ != 0); }

void BBR::EnterStartup() {
  state_ = State::Startup;
  pacing_gain_ = HighGain();
  cwnd_gain_ = HighGain();
}

void BBR::EnterDrain() {
  state_ = State::Drain;
  pacing_gain_ = HighGain().Reciprocal();
  cwnd_gain_ = HighGain();
}

void BBR::EnterProbeBW(TimeStamp now, const Ack& ack) {
  state_ = State::ProbeBW;
  pacing_gain_ = UnitGain();
  cwnd_gain_ = Gain{2, 1};
  cycle_index_ = 1 + rand() % (kProbeBWGainCycleLength - 1);
  AdvanceCyclePhase(now, ack);
}

void BBR::AdvanceCyclePhase(TimeStamp now, const Ack& ack) {
  cycle_stamp_ = now;
  cycle_index_ = (cycle_index_ + 1) % kProbeBWGainCycleLength;
  pacing_gain_ = kProbeBWGainCycle[cycle_index_];
}

void BBR::CheckCyclePhase(TimeStamp now, const Ack& ack) {
  if (state_ == State::ProbeBW && IsNextCyclePhase(now, ack)) {
    AdvanceCyclePhase(now, ack);
  }
}

bool BBR::IsNextCyclePhase(TimeStamp now, const Ack& ack) const {
  const bool is_full_length = now - cycle_stamp_ > rtprop_;
  if (pacing_gain_.IsOne()) {
    return is_full_length;
  }
  if (pacing_gain_.GreaterThanOne()) {
    return is_full_length && (ack.nacked_packets.size() > 0 ||
                              prior_inflight_ >= Inflight(pacing_gain_));
  }
  // pacing_gain_ < 1
  return is_full_length || prior_inflight_ <= Inflight(UnitGain());
}

void BBR::HandleRestartFromIdle() {
  if (packets_in_flight_ == 0 && app_limited_seq_ != 0) {
    idle_start_ = true;
    if (state_ == State::ProbeBW) {
      SetPacingRateWithGain(UnitGain());
    }
  }
}

void BBR::CheckFullPipe(const Ack& ack, const RateSample& rs) {
  if (filled_pipe_ || !round_start_ || rs.is_app_limited) {
    // No need to check for a full pipe now.
    return;
  }
  // Is bottleneck bandwidth still growing?
  if (bottleneck_bandwidth_filter_.best_estimate() >= Gain{5, 4} * full_bw_) {
    full_bw_ = bottleneck_bandwidth_filter_.best_estimate();
    full_bw_count_ = 0;
    return;
  }
  full_bw_count_++;
  if (full_bw_count_ >= 3) {
    filled_pipe_ = true;
  }
}

void BBR::CheckDrain(TimeStamp now, const Ack& ack) {
  if (state_ == State::Startup && filled_pipe_) {
    EnterDrain();
  }
  if (state_ == State::Drain &&
      packets_in_flight_ <= Inflight(UnitGain()) / mss_) {
    EnterProbeBW(now, ack);
  }
}

void BBR::CheckProbeRTT(TimeStamp now, const Ack& ack) {
  if (state_ != State::ProbeRTT && rtprop_expired_ && !idle_start_) {
    EnterProbeRTT();
    SaveCwnd();
    probe_rtt_done_stamp_ = TimeStamp::Epoch();
  }
  if (state_ == State::ProbeRTT) {
    HandleProbeRTT(now, ack);
  }
  idle_start_ = false;
}

void BBR::EnterProbeRTT() {
  state_ = State::ProbeRTT;
  pacing_gain_ = UnitGain();
  cwnd_gain_ = UnitGain();
}

void BBR::HandleProbeRTT(TimeStamp now, const Ack& ack) {
  app_limited_seq_ = delivered_seq_ + std::max(packets_in_flight_, uint64_t(1));
  if (probe_rtt_done_stamp_ == TimeStamp::Epoch() &&
      packets_in_flight_ <= kMinPipeCwndSegments) {
    probe_rtt_done_stamp_ = now + kProbeRTTDuration;
    probe_rtt_round_done_ = false;
    next_round_delivered_bytes_ = delivered_bytes_;
  } else if (probe_rtt_done_stamp_ != TimeStamp::Epoch()) {
    if (round_start_) {
      probe_rtt_round_done_ = true;
    }
    if (probe_rtt_round_done_ && now > probe_rtt_done_stamp_) {
      rtprop_stamp_ = now;
      RestoreCwnd();
      ExitProbeRTT(now, ack);
    }
  }
}

void BBR::ExitProbeRTT(TimeStamp now, const Ack& ack) {
  if (filled_pipe_) {
    EnterProbeBW(now, ack);
  } else {
    EnterStartup();
  }
}

void BBR::OnAck(const Ack& ack) {
  ValidateState();
  const auto now = timer_->Now();
  prior_inflight_ = Inflight(UnitGain());
  assert(packets_in_flight_ >=
         ack.acked_packets.size() + ack.nacked_packets.size());
  packets_in_flight_ -= ack.acked_packets.size();
  packets_in_flight_ -= ack.nacked_packets.size();
  UpdateModelAndState(now, ack);
  UpdateControlParameters(ack);
  if (packets_in_flight_ < cwnd_ && queued_packet_) {
    ScheduleQueuedPacket();
  }
  ValidateState();
}

void BBR::RequestTransmit(OutgoingPacket packet,
                          StatusOrCallback<SentPacket> transmit) {
  ValidateState();
  if (queued_packet_) return;
  assert(packet.sequence > last_sent_packet_);
  last_sent_packet_ = packet.sequence;
  queued_packet_.Reset(packet, std::move(transmit));
  queued_packet_paused_ = packets_in_flight_ >= cwnd_;
  if (queued_packet_paused_) return;
  ScheduleQueuedPacket();
  ValidateState();
}

void BBR::ScheduleQueuedPacket() {
  ValidateState();
  if (queued_packet_->timeout) return;

  const auto now = timer_->Now();
  TimeStamp send_time = last_send_time_ + PacingRate().SendTimeForBytes(
                                              queued_packet_->packet.size);
  if (send_time < now) {
    send_time = now;
  } else if (queued_packet_paused_) {
    app_limited_seq_ =
        delivered_seq_ + std::max(packets_in_flight_, uint64_t(1));
  }
  std::swap(last_send_time_, send_time);
  queued_packet_->timeout.Reset(
      timer_, std::max(now, send_time), [this](const Status& status) {
        if (status.is_ok()) {
          SentPacket p{queued_packet_->packet,
                       delivered_bytes_,
                       recovery_ == Recovery::Fast,
                       app_limited_seq_ != 0,
                       timer_->Now(),
                       delivered_time_};
          HandleRestartFromIdle();
          packets_in_flight_++;
          auto transmit = std::move(queued_packet_->transmit);
          queued_packet_.Reset();
          ValidateState();
          transmit(p);
        }
      });
}  // namespace overnet

void BBR::UpdateModelAndState(TimeStamp now, const Ack& ack) {
  const RateSample rs = SampleBandwidth(now, ack);
  UpdateBtlBw(ack, rs);
  CheckCyclePhase(now, ack);
  CheckFullPipe(ack, rs);
  CheckDrain(now, ack);
  UpdateRTprop(now, ack, rs);
  CheckProbeRTT(now, ack);
}

BBR::RateSample BBR::SampleBandwidth(TimeStamp now, const Ack& ack) {
  if (ack.acked_packets.empty()) {
    return RateSample{Bandwidth::Zero(), TimeDelta::NegativeInf(), false};
  }

  for (const SentPacket& p : ack.acked_packets) {
    delivered_bytes_ += p.outgoing.size;
    assert(now >= p.send_time);
  }

  const SentPacket& back = ack.acked_packets.back();
  delivered_seq_ = back.outgoing.sequence;
  delivered_time_ = now;
  const TimeDelta interval = delivered_time_ - back.delivered_time_at_send;
  first_sent_time_ = back.send_time;

  // Clear app-limited field if bubble is Ack'd.
  if (app_limited_seq_ != 0 && delivered_seq_ > app_limited_seq_) {
    app_limited_seq_ = 0;
  }

  const uint64_t delivered = delivered_bytes_ - back.delivered_bytes_at_send;

  if (interval < kMinRTT) {
    return RateSample{Bandwidth::Zero(), TimeDelta::NegativeInf(), false};
  }
  return RateSample{
      Bandwidth::BytesPerTime(delivered, interval),
      now - back.send_time,
      back.is_app_limited,
  };
}

void BBR::UpdateControlParameters(const Ack& ack) {
  SetPacingRate();
  // SetSendQuantum();
  SetCwnd(ack);
}

void BBR::UpdateBtlBw(const Ack& ack, const RateSample& rs) {
  UpdateRound(ack);
  if (rs.delivery_rate >= bottleneck_bandwidth_filter_.best_estimate() ||
      !rs.is_app_limited) {
    bottleneck_bandwidth_filter_.Update(round_count_, rs.delivery_rate);
  }
}

void BBR::UpdateRound(const Ack& ack) {
  if (ack.acked_packets.empty()) return;
  const auto& last_packet_acked = ack.acked_packets.back();
  if (last_packet_acked.delivered_bytes_at_send >=
      next_round_delivered_bytes_) {
    next_round_delivered_bytes_ = delivered_bytes_;
    round_count_++;
    round_start_ = true;
  } else {
    round_start_ = false;
  }
}

void BBR::UpdateRTprop(TimeStamp now, const Ack& ack, const RateSample& rs) {
  rtprop_expired_ = (now > rtprop_stamp_ + kRTpropFilterLength);
  if (rs.rtt >= TimeDelta::Zero() && (rs.rtt < rtprop_ || rtprop_expired_)) {
    rtprop_ = rs.rtt;
    rtprop_stamp_ = now;
  }
}

void BBR::SetCwnd(const Ack& ack) {
  UpdateTargetCwnd();
  switch (recovery_) {
    case Recovery::None:
      if (ack.nacked_packets.size() > 0) {
        SetFastRecovery(ack);
      }
      break;
    case Recovery::Fast:
      ModulateCwndForRecovery(ack);
      break;
  }
  if (!packet_conservation_) {
    if (filled_pipe_) {
      cwnd_ = std::min(cwnd_ + ack.acked_packets.size(), target_cwnd_);
    } else if (cwnd_ < target_cwnd_ || delivered_bytes_ < 3 * mss_) {
      cwnd_ = cwnd_ + ack.acked_packets.size();
    }
    cwnd_ = std::max(cwnd_, kMinPipeCwndSegments);
  }
  if (state_ == State::ProbeRTT) {
    ModulateCwndForProbeRTT();
  }
}

void BBR::ModulateCwndForRecovery(const Ack& ack) {
  if (ack.nacked_packets.size() > 0) {
    exit_recovery_at_seq_ = last_sent_packet_;
    if (cwnd_ > ack.nacked_packets.size() + 1) {
      cwnd_ -= ack.nacked_packets.size();
    } else {
      cwnd_ = 1;
    }
  } else {
    packet_conservation_ = false;
    RestoreCwnd();
    recovery_ = Recovery::None;
  }
  if (packet_conservation_) {
    // Reset packet conservation after one Fast recovery RTT
    for (const auto& p : ack.acked_packets) {
      if (p.in_fast_recovery) {
        packet_conservation_ = false;
      }
    }
  }
  if (packet_conservation_) {
    cwnd_ = std::max(cwnd_, packets_in_flight_ + ack.acked_packets.size());
  }
}

void BBR::ModulateCwndForProbeRTT() {
  cwnd_ = std::min(cwnd_, kMinPipeCwndSegments);
}

void BBR::SetPacingRateWithGain(Gain gain) {
  auto rate = gain * bottleneck_bandwidth_filter_.best_estimate();
  if (filled_pipe_ || !pacing_rate_ || rate > *pacing_rate_) {
    pacing_rate_ = rate;
  }
}

void BBR::SetFastRecovery(const Ack& ack) {
  assert(recovery_ == Recovery::None);
  SaveCwnd();
  cwnd_ = packets_in_flight_ + std::max(ack.acked_packets.size(), 1ul);
  packet_conservation_ = true;
  recovery_ = Recovery::Fast;
  exit_recovery_at_seq_ = last_sent_packet_;
}

uint64_t BBR::SendQuantum() const {
  if (PacingRate() < Bandwidth::FromKilobitsPerSecond(1200)) {
    return mss_;
  } else if (PacingRate() < Bandwidth::FromKilobitsPerSecond(24000)) {
    return 2 * mss_;
  } else {
    return std::min(uint64_t(65536), PacingRate().BytesSentForTime(
                                         TimeDelta::FromMilliseconds(1)));
  }
}

Bandwidth BBR::PacingRate() const {
  if (!pacing_rate_) {
    return Bandwidth::BytesPerTime(3 * mss_, TimeDelta::FromMilliseconds(1));
  }
  return *pacing_rate_;
}

uint64_t BBR::Inflight(Gain gain) const {
  if (rtprop_ == TimeDelta::PositiveInf()) {
    return 3 * mss_;
  }
  auto quanta = 3 * SendQuantum();
  auto estimated_bdp =
      bottleneck_bandwidth_filter_.best_estimate().BytesSentForTime(rtprop_);
  return gain * estimated_bdp + quanta;
}

void BBR::SaveCwnd() {
  if (recovery_ == Recovery::None && state_ != State::ProbeRTT) {
    prior_cwnd_ = cwnd_;
  } else {
    prior_cwnd_ = std::max(prior_cwnd_, cwnd_);
  }
}

void BBR::RestoreCwnd() { cwnd_ = std::max(cwnd_, prior_cwnd_); }

}  // namespace overnet
