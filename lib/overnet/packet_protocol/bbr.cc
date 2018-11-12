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

static uint64_t SumBytes(const std::vector<BBR::SentPacket>& v) {
  uint64_t sum = 0;
  for (const auto& p : v) {
    sum += p.outgoing.size;
  }
  return sum;
}

BBR::BBR(Timer* timer, RandFunc rand, uint32_t mss, Optional<TimeDelta> srtt)
    : timer_(timer),
      rtprop_(srtt.ValueOr(TimeDelta::PositiveInf())),
      rtprop_stamp_(timer->Now()),
      mss_(mss),
      rand_(rand) {
  UpdateTargetCwnd();
  ValidateState();
}

void BBR::ValidateState() { assert(cwnd_bytes_ != 0); }

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
  cycle_index_ = 1 + rand_() % (kProbeBWGainCycleLength - 1);
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
  ScopedModule<BBR> in_bbr(this);
  ValidateState();
  const auto now = timer_->Now();
  prior_inflight_ = Inflight(UnitGain());
  OVERNET_TRACE(DEBUG) << "ack " << ack.acked_packets.size() << " nack "
                       << ack.nacked_packets.size()
                       << " packets_in_flight=" << packets_in_flight_
                       << " bytes_in_flight=" << bytes_in_flight_;
  assert(packets_in_flight_ >=
         ack.acked_packets.size() + ack.nacked_packets.size());
  packets_in_flight_ -= ack.acked_packets.size();
  packets_in_flight_ -= ack.nacked_packets.size();
  assert(bytes_in_flight_ >=
         SumBytes(ack.acked_packets) + SumBytes(ack.nacked_packets));
  bytes_in_flight_ -= SumBytes(ack.acked_packets);
  bytes_in_flight_ -= SumBytes(ack.nacked_packets);
  UpdateModelAndState(now, ack);
  UpdateControlParameters(ack);
  OVERNET_TRACE(DEBUG) << "end-ack packets_in_flight=" << packets_in_flight_
                       << " bytes_in_flight=" << bytes_in_flight_
                       << " cwnd=" << cwnd_bytes_;
  if (bytes_in_flight_ < cwnd_bytes_ && queued_packet_) {
    QueuedPacketReady();
  }
  ValidateState();
}

void BBR::RequestTransmit(StatusCallback ready) {
  ScopedModule<BBR> in_bbr(this);
  ValidateState();
  if (queued_packet_) {
    assert(false);
    return;
  }
  OVERNET_TRACE(DEBUG) << "RequestTransmit: packets_in_flight="
                       << packets_in_flight_
                       << " bytes_in_flight=" << bytes_in_flight_
                       << " cwnd=" << cwnd_bytes_
                       << (bytes_in_flight_ >= cwnd_bytes_ ? " PAUSED" : "");
  queued_packet_.Reset(std::move(ready));
  queued_packet_paused_ = bytes_in_flight_ >= cwnd_bytes_;
  if (!queued_packet_paused_) {
    QueuedPacketReady();
  }
  ValidateState();
}

void BBR::QueuedPacketReady() {
  OVERNET_TRACE(DEBUG) << "QueuedPacketReady: packets_in_flight="
                       << packets_in_flight_
                       << " bytes_in_flight=" << bytes_in_flight_
                       << " cwnd=" << cwnd_bytes_
                       << " last_send_time=" << last_send_time_;
  assert(queued_packet_);
  HandleRestartFromIdle();
  packets_in_flight_++;
  // We reserve away one packet's worth of sending here, and clear it
  // later in ScheduleTransmit once we know the actual packet length.
  // This prevents accidental floods of messages getting queued in lower
  // layers.
  bytes_in_flight_ += mss_;
  auto cb = queued_packet_.Take();
  if (timer_->Now() >= last_send_time_) {
    cb(Status::Ok());
  } else {
    queued_packet_timeout_.Reset(timer_, last_send_time_, std::move(cb));
  }
}

void BBR::CancelRequestTransmit() {
  ScopedModule<BBR> in_bbr(this);
  ValidateState();
  queued_packet_timeout_.Reset();
  queued_packet_.Reset();
  ValidateState();
}

BBR::SentPacket BBR::ScheduleTransmit(OutgoingPacket packet) {
  ScopedModule<BBR> in_bbr(this);

  ValidateState();

  if (packet.sequence == 1) {
    delivered_time_ = timer_->Now();
  }

  // Subtract out the reserved mss packet length that we applied in
  // QueuedPacketReady first.
  assert(bytes_in_flight_ >= mss_);
  bytes_in_flight_ -= mss_;
  bytes_in_flight_ += packet.size;

  OVERNET_TRACE(DEBUG) << "ScheduleTransmit packet.sequence=" << packet.sequence
                       << " packet.size=" << packet.size
                       << " last_sent_packet=" << last_sent_packet_;

  assert(packet.sequence == last_sent_packet_ + 1);
  last_sent_packet_ = packet.sequence;

  const auto now = timer_->Now();
  TimeStamp send_time =
      last_send_time_ + std::max(TimeDelta::FromMicroseconds(1),
                                 PacingRate().SendTimeForBytes(packet.size));
  OVERNET_TRACE(DEBUG) << "ScheduleTransmit bytes_in_flight="
                       << bytes_in_flight_ << " mss=" << mss_
                       << " packet_size=" << packet.size
                       << " pacing_rate=" << PacingRate()
                       << " send_time_for_bytes="
                       << PacingRate().SendTimeForBytes(packet.size)
                       << " last_send_time=" << last_send_time_ << " ("
                       << (timer_->Now() - last_send_time_) << " ago)"
                       << " initial_send_time=" << send_time
                       << " rtt=" << rtt();
  if (send_time < now) {
    send_time = now;
  } else if (queued_packet_paused_) {
    app_limited_seq_ =
        delivered_seq_ + std::max(packets_in_flight_, uint64_t(1));
  }
  std::swap(last_send_time_, send_time);

  ValidateState();

  if (bytes_in_flight_ < cwnd_bytes_ && queued_packet_) {
    // Releasing the reservation might mean another packet can come through!
    QueuedPacketReady();
  }

  return SentPacket{packet,
                    delivered_bytes_,
                    recovery_ == Recovery::Fast,
                    app_limited_seq_ != 0,
                    now,
                    delivered_time_};
}

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
    OVERNET_TRACE(DEBUG) << "ack: sent=" << p.send_time
                         << " delivered_time_at_send="
                         << p.delivered_time_at_send
                         << " size=" << p.outgoing.size;
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

  OVERNET_TRACE(DEBUG) << "SampleBandwidth: interval=" << interval
                       << " delivered=" << delivered
                       << " delivered_time_at_send="
                       << back.delivered_time_at_send;

  if (interval < kMinRTT) {
    return RateSample{Bandwidth::Zero(), TimeDelta::NegativeInf(), false};
  }
  OVERNET_TRACE(DEBUG) << "  => BW="
                       << Bandwidth::BytesPerTime(delivered, interval);
  return RateSample{
      Bandwidth::BytesPerTime(delivered, interval),
      std::max(TimeDelta::Zero(), now - back.send_time),
      back.is_app_limited,
  };
}

void BBR::UpdateControlParameters(const Ack& ack) {
  SetPacingRate();
  // SetSendQuantum();
  SetCwnd(ack);
}

void BBR::UpdateBtlBw(const Ack& ack, const RateSample& rs) {
  if (!UpdateRound(ack)) {
    return;
  }
  OVERNET_TRACE(DEBUG)
      << "Add BW est?: round_count=" << round_count_
      << " delivery_rate=" << rs.delivery_rate
      << " app_limited=" << rs.is_app_limited << " current_best_estimate="
      << bottleneck_bandwidth_filter_.best_estimate() << ' '
      << (rs.delivery_rate >= bottleneck_bandwidth_filter_.best_estimate() ||
                  !rs.is_app_limited
              ? "YES"
              : "NO");
  if (rs.delivery_rate >= bottleneck_bandwidth_filter_.best_estimate() ||
      !rs.is_app_limited) {
    bottleneck_bandwidth_filter_.Update(round_count_, rs.delivery_rate);
  }
}

bool BBR::UpdateRound(const Ack& ack) {
  if (ack.acked_packets.empty()) {
    return false;
  }
  const auto& last_packet_acked = ack.acked_packets.back();
  if (last_packet_acked.delivered_bytes_at_send >=
      next_round_delivered_bytes_) {
    next_round_delivered_bytes_ = delivered_bytes_;
    round_count_++;
    round_start_ = true;
    return true;
  } else {
    round_start_ = false;
    return false;
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
      SetCwndBytes(
          std::min(cwnd_bytes_ + SumBytes(ack.acked_packets),
                   target_cwnd_bytes_),
          [this] {
            OVERNET_TRACE(DEBUG)
                << "SetCwnd, no packet conservation, filled pipe; target_cwnd="
                << target_cwnd_bytes_ << " new=" << cwnd_bytes_;
          });
    } else if (cwnd_bytes_ < target_cwnd_bytes_ ||
               SumBytes(ack.acked_packets) < 3 * mss_) {
      SetCwndBytes(cwnd_bytes_ + SumBytes(ack.acked_packets), [this]() {
        OVERNET_TRACE(DEBUG)
            << "SetCwnd, no packet conservation, unfilled pipe; target_cwnd="
            << target_cwnd_bytes_ << " delivered_bytes=" << delivered_bytes_
            << " mss=" << mss_ << " new=" << cwnd_bytes_;
      });
    }
    SetCwndBytes(
        std::max(target_cwnd_bytes_, kMinPipeCwndSegments * mss_), [this] {
          OVERNET_TRACE(DEBUG) << "SetCwnd, adjust for kMinPipeCwndSegments"
                               << " new=" << cwnd_bytes_;
        });
  }
  if (state_ == State::ProbeRTT) {
    ModulateCwndForProbeRTT();
  }
}

void BBR::ModulateCwndForRecovery(const Ack& ack) {
  if (ack.nacked_packets.size() > 0) {
    exit_recovery_at_seq_ = last_sent_packet_;
    auto nacked_bytes = SumBytes(ack.nacked_packets);
    if (cwnd_bytes_ > nacked_bytes + mss_) {
      SetCwndBytes(cwnd_bytes_ - nacked_bytes, [this] {
        OVERNET_TRACE(DEBUG) << "ModulateCwndForRecovery new=" << cwnd_bytes_;
      });
    } else {
      SetCwndBytes(mss_, [this] {
        OVERNET_TRACE(DEBUG) << "ModulateCwndForRecovery new=" << cwnd_bytes_;
      });
    }
  } else if (ack.acked_packets.size() > 0 &&
             ack.acked_packets.back().outgoing.sequence >=
                 exit_recovery_at_seq_) {
    packet_conservation_ = false;
    RestoreCwnd();
    recovery_ = Recovery::None;
  }
  if (ack.nacked_packets.size() == 0) {
    if (packet_conservation_) {
      // Reset packet conservation after one Fast recovery RTT
      for (const auto& p : ack.acked_packets) {
        if (p.in_fast_recovery) {
          packet_conservation_ = false;
        }
      }
    }
  } else {
    packet_conservation_ = true;
  }
  if (packet_conservation_) {
    SetCwndBytes(
        std::max(cwnd_bytes_, bytes_in_flight_ + SumBytes(ack.acked_packets)),
        [this] {
          OVERNET_TRACE(DEBUG)
              << "ModulateCwndForRecovery packet_conservation new="
              << cwnd_bytes_;
        });
  }
}

void BBR::ModulateCwndForProbeRTT() {
  SetCwndBytes(std::min(cwnd_bytes_, kMinPipeCwndSegments * mss_),
               [] { OVERNET_TRACE(DEBUG) << "ModulateCwndForProbeRTT"; });
}

void BBR::SetPacingRateWithGain(Gain gain) {
  auto rate = gain * bottleneck_bandwidth_filter_.best_estimate();
  OVERNET_TRACE(DEBUG) << "SetPacingRateWithGain: gain=" << gain << " best_est="
                       << bottleneck_bandwidth_filter_.best_estimate()
                       << " rate=" << rate << " filled_pipe=" << filled_pipe_
                       << " pacing_rate=" << pacing_rate_;
  if (rate != Bandwidth::Zero() &&
      (filled_pipe_ || !pacing_rate_ || rate > *pacing_rate_)) {
    pacing_rate_ = rate;
  }
}

void BBR::SetFastRecovery(const Ack& ack) {
  assert(recovery_ == Recovery::None);
  SaveCwnd();
  SetCwndBytes(
      bytes_in_flight_ + std::max(SumBytes(ack.acked_packets), uint64_t(mss_)),
      [this] {
        OVERNET_TRACE(DEBUG) << "SetFastRecovery new=" << cwnd_bytes_;
      });
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
    prior_cwnd_bytes_ = cwnd_bytes_;
  } else {
    prior_cwnd_bytes_ = std::max(prior_cwnd_bytes_, cwnd_bytes_);
  }
}

void BBR::RestoreCwnd() {
  SetCwndBytes(std::max(cwnd_bytes_, prior_cwnd_bytes_), [this] {
    OVERNET_TRACE(DEBUG) << "RestoreCwnd new=" << cwnd_bytes_;
  });
}

}  // namespace overnet
