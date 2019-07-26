// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/deprecated/lib/packet_protocol/bbr.h"

#include <iostream>

namespace overnet {

static constexpr uint64_t kMinPipeCwndSegments = 4;
static constexpr auto kRTpropFilterLength = TimeDelta::FromSeconds(10);
static constexpr auto kProbeRTTDuration = TimeDelta::FromMilliseconds(200);
static constexpr TimeDelta kMinRTT = TimeDelta::FromMicroseconds(1);

const BBR::Gain BBR::kProbeBWGainCycle[kProbeBWGainCycleLength] = {
    {5, 4}, {3, 4}, UnitGain(), UnitGain(), UnitGain(), UnitGain(), UnitGain(), UnitGain(),
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

BBR::~BBR() {
  if (transmit_request_ != nullptr) {
    transmit_request_->Cancel();
  }
  while (auto front = active_transmits_.Front()) {
    front->Cancel();
  }
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
    return is_full_length &&
           (ack.nacked_packets.size() > 0 || prior_inflight_ >= Inflight(pacing_gain_));
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

void BBR::CheckFullPipe(const Ack& ack) {
  if (filled_pipe_ || !round_start_ || last_sample_is_app_limited_) {
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
  if (state_ == State::Drain && packets_in_flight_ <= Inflight(UnitGain()) / mss_) {
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
  if (probe_rtt_done_stamp_ == TimeStamp::Epoch() && packets_in_flight_ <= kMinPipeCwndSegments) {
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
                       << ack.nacked_packets.size() << " packets_in_flight=" << packets_in_flight_
                       << " bytes_in_flight=" << bytes_in_flight_;
  assert(packets_in_flight_ >= ack.acked_packets.size() + ack.nacked_packets.size());
  packets_in_flight_ -= ack.acked_packets.size();
  packets_in_flight_ -= ack.nacked_packets.size();
  assert(bytes_in_flight_ >= SumBytes(ack.acked_packets) + SumBytes(ack.nacked_packets));
  bytes_in_flight_ -= SumBytes(ack.acked_packets);
  bytes_in_flight_ -= SumBytes(ack.nacked_packets);
  UpdateModelAndState(now, ack);
  UpdateControlParameters(ack);
  OVERNET_TRACE(DEBUG) << "end-ack packets_in_flight=" << packets_in_flight_
                       << " bytes_in_flight=" << bytes_in_flight_ << " cwnd=" << cwnd_bytes_;
  if (bytes_in_flight_ < cwnd_bytes_ && transmit_request_ && !transmit_request_->ready_.empty()) {
    transmit_request_->Ready();
  }
  ValidateState();
}

BBR::TransmitRequest::TransmitRequest(BBR* bbr, StatusCallback ready)
    : bbr_(bbr), ready_(std::move(ready)) {
  ScopedModule<BBR> in_bbr(bbr_);
  bbr_->ValidateState();
  assert(bbr_->transmit_request_ == nullptr);
  bbr_->transmit_request_ = this;
  bbr_->active_transmits_.PushBack(this);
  OVERNET_TRACE(DEBUG) << "RequestTransmit: packets_in_flight=" << bbr_->packets_in_flight_
                       << " bytes_in_flight=" << bbr_->bytes_in_flight_
                       << " cwnd=" << bbr_->cwnd_bytes_
                       << (bbr_->bytes_in_flight_ >= bbr_->cwnd_bytes_ ? " PAUSED" : "");
  paused_ = bbr_->bytes_in_flight_ >= bbr_->cwnd_bytes_;
  if (!paused_) {
    Ready();
  }
  // We may be moved after the Ready() call, so use the originally provided
  // pointer here.
  bbr->ValidateState();
}

BBR::TransmitRequest::~TransmitRequest() { Cancel(); }

BBR::TransmitRequest::TransmitRequest(TransmitRequest&& other)
    : bbr_(other.bbr_),
      ready_(std::move(other.ready_)),
      timeout_(std::move(other.timeout_)),
      paused_(other.paused_),
      reserved_bytes_(other.reserved_bytes_) {
  if (bbr_) {
    bbr_->active_transmits_.PushBack(this);
    bbr_->active_transmits_.Remove(&other);
  }
  if (bbr_ && bbr_->transmit_request_ == &other) {
    bbr_->transmit_request_ = this;
  }
  other.bbr_ = nullptr;
}

void BBR::TransmitRequest::Ready() {
  OVERNET_TRACE(DEBUG) << "QueuedPacketReady: packets_in_flight=" << bbr_->packets_in_flight_
                       << " bytes_in_flight=" << bbr_->bytes_in_flight_
                       << " cwnd=" << bbr_->cwnd_bytes_
                       << " last_send_time=" << bbr_->last_send_time_;
  assert(bbr_->transmit_request_ == this);
  bbr_->transmit_request_ = nullptr;
  bbr_->HandleRestartFromIdle();
  bbr_->packets_in_flight_++;
  // We reserve away one packet's worth of sending here, and clear it
  // later in ScheduleTransmit once we know the actual packet length.
  // This prevents accidental floods of messages getting queued in lower
  // layers.
  bbr_->bytes_in_flight_ += bbr_->mss_;
  reserved_bytes_ = true;
  auto cb = std::move(ready_);
  if (bbr_->timer_->Now() >= bbr_->last_send_time_) {
    cb(Status::Ok());
  } else {
    timeout_ = std::make_unique<Timeout>(bbr_->timer_, bbr_->last_send_time_, std::move(cb));
  }
}

void BBR::TransmitRequest::Cancel() {
  if (bbr_ != nullptr) {
    auto bbr = bbr_;
    bbr_ = nullptr;
    ScopedModule<BBR> in_bbr(bbr);
    OVERNET_TRACE(DEBUG) << "Cancel: reserved_bytes=" << reserved_bytes_
                         << " waiting=" << !ready_.empty()
                         << " head=" << (bbr->transmit_request_ == this);
    if (bbr->transmit_request_ == this) {
      bbr->transmit_request_ = nullptr;
    }
    if (reserved_bytes_) {
      bbr->bytes_in_flight_ -= bbr->mss_;
      bbr->packets_in_flight_--;
    }
    bbr->active_transmits_.Remove(this);
    // Always move the last send time forward, to avoid potentially
    // infinite recursion trying to send a new packet.
    bbr->last_send_time_ =
        std::max(bbr->timer_->Now() + TimeDelta::FromMilliseconds(1),
                 bbr->last_send_time_ + bbr->PacingRate().SendTimeForBytes(bbr->mss_));
    if (reserved_bytes_ && bbr->bytes_in_flight_ < bbr->cwnd_bytes_ && bbr->transmit_request_ &&
        !bbr->transmit_request_->ready_.empty()) {
      // Releasing the reservation might mean another packet can come through!
      bbr->transmit_request_->Ready();
    }
    timeout_.reset();
    if (!ready_.empty()) {
      ready_(Status::Cancelled());
    }
  }
}

BBR::SentPacket BBR::TransmitRequest::Sent(OutgoingPacket packet) {
  ScopedModule<BBR> in_bbr(bbr_);

  bbr_->ValidateState();

  if (packet.sequence == 1) {
    bbr_->delivered_time_ = bbr_->timer_->Now();
  }

  // Subtract out the reserved mss packet length that we applied in
  // QueuedPacketReady first.
  assert(reserved_bytes_);
  assert(bbr_->bytes_in_flight_ >= bbr_->mss_);
  bbr_->bytes_in_flight_ -= bbr_->mss_;
  bbr_->bytes_in_flight_ += packet.size;
  reserved_bytes_ = false;

  OVERNET_TRACE(DEBUG) << "Sent packet.sequence=" << packet.sequence
                       << " packet.size=" << packet.size
                       << " last_sent_packet=" << bbr_->last_sent_packet_;

  assert(packet.sequence > bbr_->last_sent_packet_);
  bbr_->last_sent_packet_ = packet.sequence;

  const auto now = bbr_->timer_->Now();
  TimeStamp send_time =
      bbr_->last_send_time_ +
      std::max(TimeDelta::FromMicroseconds(1), bbr_->PacingRate().SendTimeForBytes(packet.size));
  OVERNET_TRACE(DEBUG) << "Sent bytes_in_flight=" << bbr_->bytes_in_flight_ << " mss=" << bbr_->mss_
                       << " packet_size=" << packet.size << " pacing_rate=" << bbr_->PacingRate()
                       << " send_time_for_bytes="
                       << bbr_->PacingRate().SendTimeForBytes(packet.size)
                       << " last_send_time=" << bbr_->last_send_time_ << " ("
                       << (bbr_->timer_->Now() - bbr_->last_send_time_) << " ago)"
                       << " initial_send_time=" << send_time << " rtt=" << bbr_->rtt();
  if (send_time < now) {
    send_time = now;
  } else if (paused_) {
    bbr_->app_limited_seq_ = bbr_->delivered_seq_ + std::max(bbr_->packets_in_flight_, uint64_t(1));
  }
  std::swap(bbr_->last_send_time_, send_time);

  bbr_->ValidateState();

  if (bbr_->bytes_in_flight_ < bbr_->cwnd_bytes_ && bbr_->transmit_request_ &&
      !bbr_->transmit_request_->ready_.empty()) {
    // Releasing the reservation might mean another packet can come through!
    bbr_->transmit_request_->Ready();
  }

  SentPacket sp{packet,
                bbr_->delivered_bytes_,
                bbr_->recovery_ == Recovery::Fast,
                bbr_->app_limited_seq_ != 0,
                now,
                bbr_->delivered_time_};

  bbr_->active_transmits_.Remove(this);
  bbr_ = nullptr;

  return sp;
}

void BBR::UpdateModelAndState(TimeStamp now, const Ack& ack) {
  UpdateBandwidthAndRtt(now, ack);
  CheckCyclePhase(now, ack);
  CheckFullPipe(ack);
  CheckDrain(now, ack);
  CheckProbeRTT(now, ack);
}

BBR::RateSample BBR::SampleBandwidth(TimeStamp now, const SentPacket& acked_sent_packet) {
  delivered_bytes_ += acked_sent_packet.outgoing.size;
  OVERNET_TRACE(DEBUG) << "ack: sent=" << acked_sent_packet.send_time
                       << " delivered_time_at_send=" << acked_sent_packet.delivered_time_at_send
                       << " delivered_bytes_at_send=" << acked_sent_packet.delivered_bytes_at_send
                       << " size=" << acked_sent_packet.outgoing.size
                       << " seq=" << acked_sent_packet.outgoing.sequence;

  delivered_seq_ = acked_sent_packet.outgoing.sequence;
  delivered_time_ = now;
  const TimeDelta interval = delivered_time_ - acked_sent_packet.delivered_time_at_send;
  first_sent_time_ = acked_sent_packet.send_time;

  // Clear app-limited field if bubble is Ack'd.
  if (app_limited_seq_ != 0 && delivered_seq_ > app_limited_seq_) {
    app_limited_seq_ = 0;
  }

  const uint64_t delivered = delivered_bytes_ - acked_sent_packet.delivered_bytes_at_send;

  OVERNET_TRACE(DEBUG) << "SampleBandwidth: interval=" << interval << " delivered=" << delivered
                       << " delivered_time_at_send=" << acked_sent_packet.delivered_time_at_send
                       << " delivered_bytes_at_send=" << acked_sent_packet.delivered_bytes_at_send
                       << " rtt=" << std::max(TimeDelta::Zero(), now - acked_sent_packet.send_time);

  if (interval < kMinRTT) {
    return RateSample{Bandwidth::Zero(), TimeDelta::NegativeInf(), false};
  }
  OVERNET_TRACE(DEBUG) << "  => BW=" << Bandwidth::BytesPerTime(delivered, interval);
  return RateSample{
      Bandwidth::BytesPerTime(delivered, interval),
      std::max(TimeDelta::Zero(), now - acked_sent_packet.send_time),
      acked_sent_packet.is_app_limited,
  };
}

void BBR::UpdateControlParameters(const Ack& ack) {
  SetPacingRate();
  // SetSendQuantum();
  SetCwnd(ack);
}

void BBR::UpdateBandwidthAndRtt(TimeStamp now, const Ack& ack) {
  TimeDelta min_rtt = TimeDelta::PositiveInf();
  for (const auto& acked_sent_packet : ack.acked_packets) {
    auto rs = SampleBandwidth(now, acked_sent_packet);
    if (rs.delivery_rate >= bottleneck_bandwidth_filter_.best_estimate() || !rs.is_app_limited) {
      bottleneck_bandwidth_filter_.Update(round_count_, rs.delivery_rate);
    }
    if (rs.rtt > TimeDelta::Zero()) {
      min_rtt = std::min(min_rtt, rs.rtt);
    }
  }
  UpdateRound(ack);
  rtprop_expired_ = (now > rtprop_stamp_ + kRTpropFilterLength);
  if (min_rtt > TimeDelta::Zero() && (min_rtt < rtprop_ || rtprop_expired_)) {
    rtprop_ = min_rtt;
    rtprop_stamp_ = now;
  }
}

void BBR::UpdateRound(const Ack& ack) {
  if (ack.acked_packets.empty()) {
    round_start_ = false;
    return;
  }
  const auto& last_packet_acked = ack.acked_packets.back();
  if (last_packet_acked.delivered_bytes_at_send >= next_round_delivered_bytes_) {
    next_round_delivered_bytes_ = delivered_bytes_;
    round_count_++;
    round_start_ = true;
  } else {
    round_start_ = false;
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
      SetCwndBytes(std::min(cwnd_bytes_ + SumBytes(ack.acked_packets), target_cwnd_bytes_), [this] {
        OVERNET_TRACE(DEBUG) << "SetCwnd, no packet conservation, filled pipe; target_cwnd="
                             << target_cwnd_bytes_ << " new=" << cwnd_bytes_;
      });
    } else if (cwnd_bytes_ < target_cwnd_bytes_ || SumBytes(ack.acked_packets) < 3 * mss_) {
      SetCwndBytes(cwnd_bytes_ + SumBytes(ack.acked_packets), [this]() {
        OVERNET_TRACE(DEBUG) << "SetCwnd, no packet conservation, unfilled pipe; target_cwnd="
                             << target_cwnd_bytes_ << " delivered_bytes=" << delivered_bytes_
                             << " mss=" << mss_ << " new=" << cwnd_bytes_;
      });
    }
    SetCwndBytes(std::max(target_cwnd_bytes_, kMinPipeCwndSegments * mss_), [this] {
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
      SetCwndBytes(
          mss_, [this] { OVERNET_TRACE(DEBUG) << "ModulateCwndForRecovery new=" << cwnd_bytes_; });
    }
  } else if (ack.acked_packets.size() > 0 &&
             ack.acked_packets.back().outgoing.sequence >= exit_recovery_at_seq_) {
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
    SetCwndBytes(std::max(cwnd_bytes_, bytes_in_flight_ + SumBytes(ack.acked_packets)), [this] {
      OVERNET_TRACE(DEBUG) << "ModulateCwndForRecovery packet_conservation new=" << cwnd_bytes_;
    });
  }
}

void BBR::ModulateCwndForProbeRTT() {
  SetCwndBytes(std::min(cwnd_bytes_, kMinPipeCwndSegments * mss_),
               [] { OVERNET_TRACE(DEBUG) << "ModulateCwndForProbeRTT"; });
}

void BBR::SetPacingRateWithGain(Gain gain) {
  auto rate = gain * bottleneck_bandwidth_filter_.best_estimate();
  OVERNET_TRACE(DEBUG) << "SetPacingRateWithGain: gain=" << gain
                       << " best_est=" << bottleneck_bandwidth_filter_.best_estimate()
                       << " rate=" << rate << " filled_pipe=" << filled_pipe_
                       << " pacing_rate=" << pacing_rate_;
  if (rate != Bandwidth::Zero() && (filled_pipe_ || !pacing_rate_ || rate > *pacing_rate_)) {
    pacing_rate_ = rate;
  }
}

void BBR::SetFastRecovery(const Ack& ack) {
  assert(recovery_ == Recovery::None);
  SaveCwnd();
  SetCwndBytes(bytes_in_flight_ + std::max(SumBytes(ack.acked_packets), uint64_t(mss_)),
               [this] { OVERNET_TRACE(DEBUG) << "SetFastRecovery new=" << cwnd_bytes_; });
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
    return std::min(uint64_t(65536), PacingRate().BytesSentForTime(TimeDelta::FromMilliseconds(1)));
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
  auto estimated_bdp = bottleneck_bandwidth_filter_.best_estimate().BytesSentForTime(rtprop_);
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
  SetCwndBytes(std::max(cwnd_bytes_, prior_cwnd_bytes_),
               [this] { OVERNET_TRACE(DEBUG) << "RestoreCwnd new=" << cwnd_bytes_; });
}

}  // namespace overnet
