// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_admin.h"

#include <fcntl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

namespace media::audio {
namespace {

// TODO(fxbug.dev/35491): Remove when transitioned to xunion; xunions generate these functions.
fuchsia::media::Usage Usage(fuchsia::media::AudioRenderUsage u) {
  fuchsia::media::Usage usage;
  usage.set_render_usage(u);
  return usage;
}

fuchsia::media::Usage Usage(fuchsia::media::AudioCaptureUsage u) {
  fuchsia::media::Usage usage;
  usage.set_capture_usage(u);
  return usage;
}

}  // namespace

AudioAdmin::AudioAdmin(StreamVolumeManager* stream_volume_manager,
                       async_dispatcher_t* fidl_dispatcher,
                       PolicyActionReporter* policy_action_reporter,
                       ActivityDispatcher* activity_dispatcher)
    : AudioAdmin(
          BehaviorGain{
              .none_gain_db = 0.0f,
              .duck_gain_db = -35.0f,
              .mute_gain_db = fuchsia::media::audio::MUTED_GAIN_DB,
          },
          stream_volume_manager, policy_action_reporter, activity_dispatcher, fidl_dispatcher) {}

AudioAdmin::AudioAdmin(BehaviorGain behavior_gain, StreamVolumeManager* stream_volume_manager,
                       PolicyActionReporter* policy_action_reporter,
                       ActivityDispatcher* activity_dispatcher, async_dispatcher_t* fidl_dispatcher)
    : behavior_gain_(behavior_gain),
      stream_volume_manager_(*stream_volume_manager),
      policy_action_reporter_(*policy_action_reporter),
      activity_dispatcher_(*activity_dispatcher),
      fidl_dispatcher_(fidl_dispatcher) {
  FX_DCHECK(stream_volume_manager);
  FX_DCHECK(policy_action_reporter);
  FX_DCHECK(fidl_dispatcher);
}

void AudioAdmin::SetInteraction(fuchsia::media::Usage active, fuchsia::media::Usage affected,
                                fuchsia::media::Behavior behavior) {
  async::PostTask(fidl_dispatcher_, [this, active = std::move(active),
                                     affected = std::move(affected), behavior = behavior]() {
    TRACE_DURATION("audio", "AudioAdmin::SetInteraction");
    std::lock_guard<fxl::ThreadChecker> lock(fidl_thread_checker_);
    if (active.Which() == fuchsia::media::Usage::Tag::kCaptureUsage &&
        affected.Which() == fuchsia::media::Usage::Tag::kCaptureUsage) {
      active_rules_.SetRule(active.capture_usage(), affected.capture_usage(), behavior);
    } else if (active.Which() == fuchsia::media::Usage::Tag::kCaptureUsage &&
               affected.Which() == fuchsia::media::Usage::Tag::kRenderUsage) {
      active_rules_.SetRule(active.capture_usage(), affected.render_usage(), behavior);

    } else if (active.Which() == fuchsia::media::Usage::Tag::kRenderUsage &&
               affected.Which() == fuchsia::media::Usage::Tag::kCaptureUsage) {
      active_rules_.SetRule(active.render_usage(), affected.capture_usage(), behavior);

    } else if (active.Which() == fuchsia::media::Usage::Tag::kRenderUsage &&
               affected.Which() == fuchsia::media::Usage::Tag::kRenderUsage) {
      active_rules_.SetRule(active.render_usage(), affected.render_usage(), behavior);
    }
  });
}

bool AudioAdmin::IsActive(fuchsia::media::AudioRenderUsage usage) {
  TRACE_DURATION("audio", "AudioAdmin::IsActive(Render)");
  std::lock_guard<fxl::ThreadChecker> lock(fidl_thread_checker_);
  auto usage_index = fidl::ToUnderlying(usage);
  return active_streams_playback_[usage_index].size() > 0;
}

bool AudioAdmin::IsActive(fuchsia::media::AudioCaptureUsage usage) {
  TRACE_DURATION("audio", "AudioAdmin::IsActive(Capture)");
  std::lock_guard<fxl::ThreadChecker> lock(fidl_thread_checker_);
  auto usage_index = fidl::ToUnderlying(usage);
  return active_streams_capture_[usage_index].size() > 0;
}

void AudioAdmin::SetUsageNone(fuchsia::media::AudioRenderUsage usage) {
  TRACE_DURATION("audio", "AudioAdmin::SetUsageNone(Render)");
  std::lock_guard<fxl::ThreadChecker> lock(fidl_thread_checker_);
  stream_volume_manager_.SetUsageGainAdjustment(
      fuchsia::media::Usage::WithRenderUsage(fidl::Clone(usage)), behavior_gain_.none_gain_db);
  policy_action_reporter_.ReportPolicyAction(Usage(usage), fuchsia::media::Behavior::NONE);
}

void AudioAdmin::SetUsageNone(fuchsia::media::AudioCaptureUsage usage) {
  TRACE_DURATION("audio", "AudioAdmin::SetUsageNone(Capture)");
  std::lock_guard<fxl::ThreadChecker> lock(fidl_thread_checker_);
  stream_volume_manager_.SetUsageGainAdjustment(
      fuchsia::media::Usage::WithCaptureUsage(fidl::Clone(usage)), behavior_gain_.none_gain_db);
  policy_action_reporter_.ReportPolicyAction(Usage(usage), fuchsia::media::Behavior::NONE);
}

void AudioAdmin::SetUsageMute(fuchsia::media::AudioRenderUsage usage) {
  TRACE_DURATION("audio", "AudioAdmin::SetUsageMute(Render)");
  std::lock_guard<fxl::ThreadChecker> lock(fidl_thread_checker_);
  stream_volume_manager_.SetUsageGainAdjustment(
      fuchsia::media::Usage::WithRenderUsage(fidl::Clone(usage)), behavior_gain_.mute_gain_db);
  policy_action_reporter_.ReportPolicyAction(Usage(usage), fuchsia::media::Behavior::MUTE);
}

void AudioAdmin::SetUsageMute(fuchsia::media::AudioCaptureUsage usage) {
  TRACE_DURATION("audio", "AudioAdmin::SetUsageMute(Capture)");
  std::lock_guard<fxl::ThreadChecker> lock(fidl_thread_checker_);
  stream_volume_manager_.SetUsageGainAdjustment(
      fuchsia::media::Usage::WithCaptureUsage(fidl::Clone(usage)), behavior_gain_.mute_gain_db);
  policy_action_reporter_.ReportPolicyAction(Usage(usage), fuchsia::media::Behavior::MUTE);
}

void AudioAdmin::SetUsageDuck(fuchsia::media::AudioRenderUsage usage) {
  TRACE_DURATION("audio", "AudioAdmin::SetUsageDuck(Render)");
  std::lock_guard<fxl::ThreadChecker> lock(fidl_thread_checker_);
  stream_volume_manager_.SetUsageGainAdjustment(
      fuchsia::media::Usage::WithRenderUsage(fidl::Clone(usage)), behavior_gain_.duck_gain_db);
  policy_action_reporter_.ReportPolicyAction(Usage(usage), fuchsia::media::Behavior::DUCK);
}

void AudioAdmin::SetUsageDuck(fuchsia::media::AudioCaptureUsage usage) {
  TRACE_DURATION("audio", "AudioAdmin::SetUsageDuck(Capture)");
  std::lock_guard<fxl::ThreadChecker> lock(fidl_thread_checker_);
  stream_volume_manager_.SetUsageGainAdjustment(
      fuchsia::media::Usage::WithCaptureUsage(fidl::Clone(usage)), behavior_gain_.duck_gain_db);
  policy_action_reporter_.ReportPolicyAction(Usage(usage), fuchsia::media::Behavior::DUCK);
}

bool AudioAdmin::IsUsageMuted(fuchsia::media::AudioRenderUsage usage) {
  std::lock_guard<fxl::ThreadChecker> lock(fidl_thread_checker_);
  auto usage_gain_settings = stream_volume_manager_.GetUsageGainSettings();
  auto gain_adjustment = usage_gain_settings.GetUsageGainAdjustment(
      fuchsia::media::Usage::WithRenderUsage(std::move(usage)));
  return gain_adjustment == behavior_gain_.mute_gain_db;
}

bool AudioAdmin::IsUsageMuted(fuchsia::media::AudioCaptureUsage usage) {
  std::lock_guard<fxl::ThreadChecker> lock(fidl_thread_checker_);
  auto usage_gain_settings = stream_volume_manager_.GetUsageGainSettings();
  auto gain_adjustment = usage_gain_settings.GetUsageGainAdjustment(
      fuchsia::media::Usage::WithCaptureUsage(std::move(usage)));
  return gain_adjustment == behavior_gain_.mute_gain_db;
}

bool AudioAdmin::IsUsageDucked(fuchsia::media::AudioRenderUsage usage) {
  std::lock_guard<fxl::ThreadChecker> lock(fidl_thread_checker_);
  auto usage_gain_settings = stream_volume_manager_.GetUsageGainSettings();
  auto gain_adjustment = usage_gain_settings.GetUsageGainAdjustment(
      fuchsia::media::Usage::WithRenderUsage(std::move(usage)));
  return gain_adjustment == behavior_gain_.duck_gain_db;
}

bool AudioAdmin::IsUsageDucked(fuchsia::media::AudioCaptureUsage usage) {
  std::lock_guard<fxl::ThreadChecker> lock(fidl_thread_checker_);
  auto usage_gain_settings = stream_volume_manager_.GetUsageGainSettings();
  auto gain_adjustment = usage_gain_settings.GetUsageGainAdjustment(
      fuchsia::media::Usage::WithCaptureUsage(std::move(usage)));
  return gain_adjustment == behavior_gain_.duck_gain_db;
}

void AudioAdmin::ApplyPolicies(fuchsia::media::AudioCaptureUsage active) {
  TRACE_DURATION("audio", "AudioAdmin::ApplyPolicies(Capture)");
  std::lock_guard<fxl::ThreadChecker> lock(fidl_thread_checker_);

  // If a stream is muted as the result of another policy, do not apply its policy.
  if (IsUsageMuted(active)) {
    return;
  }

  for (int i = 0; i < fuchsia::media::RENDER_USAGE_COUNT; i++) {
    auto affected = static_cast<fuchsia::media::AudioRenderUsage>(i);
    switch (active_rules_.GetPolicy(active, affected)) {
      case fuchsia::media::Behavior::NONE:
        if (!IsUsageMuted(affected) && !IsUsageDucked(affected)) {
          SetUsageNone(affected);
        }
        break;
      case fuchsia::media::Behavior::DUCK:
        if (!IsUsageMuted(affected)) {
          SetUsageDuck(affected);
        }
        break;
      case fuchsia::media::Behavior::MUTE:
        SetUsageMute(affected);
        break;
    }
  }
  for (int i = 0; i < fuchsia::media::CAPTURE_USAGE_COUNT; i++) {
    auto affected = static_cast<fuchsia::media::AudioCaptureUsage>(i);
    switch (active_rules_.GetPolicy(active, affected)) {
      case fuchsia::media::Behavior::NONE:
        if (!IsUsageMuted(affected) && !IsUsageDucked(affected)) {
          SetUsageNone(affected);
        }
        break;
      case fuchsia::media::Behavior::DUCK:
        if (!IsUsageMuted(affected)) {
          SetUsageDuck(affected);
        }
        break;
      case fuchsia::media::Behavior::MUTE:
        SetUsageMute(affected);
        break;
    }
  }
}

void AudioAdmin::ApplyPolicies(fuchsia::media::AudioRenderUsage active) {
  TRACE_DURATION("audio", "AudioAdmin::ApplyPolicies(Render)");
  std::lock_guard<fxl::ThreadChecker> lock(fidl_thread_checker_);
  if (IsUsageMuted(active)) {
    return;
  }

  for (int i = 0; i < fuchsia::media::RENDER_USAGE_COUNT; i++) {
    auto affected = static_cast<fuchsia::media::AudioRenderUsage>(i);
    switch (active_rules_.GetPolicy(active, affected)) {
      case fuchsia::media::Behavior::NONE:
        if (!IsUsageMuted(affected) && !IsUsageDucked(affected)) {
          SetUsageNone(affected);
        }
        break;
      case fuchsia::media::Behavior::DUCK:
        if (!IsUsageMuted(affected)) {
          SetUsageDuck(affected);
        }
        break;
      case fuchsia::media::Behavior::MUTE:
        SetUsageMute(affected);
        break;
    }
  }
  for (int i = 0; i < fuchsia::media::CAPTURE_USAGE_COUNT; i++) {
    auto affected = static_cast<fuchsia::media::AudioCaptureUsage>(i);
    switch (active_rules_.GetPolicy(active, affected)) {
      case fuchsia::media::Behavior::NONE:
        if (!IsUsageMuted(affected) && !IsUsageDucked(affected)) {
          SetUsageNone(affected);
        }
        break;
      case fuchsia::media::Behavior::DUCK:
        if (!IsUsageMuted(affected)) {
          SetUsageDuck(affected);
        }
        break;
      case fuchsia::media::Behavior::MUTE:
        SetUsageMute(affected);
        break;
    }
  }
}

void AudioAdmin::InitPolicies() {
  std::lock_guard<fxl::ThreadChecker> lock(fidl_thread_checker_);
  for (int i = 0; i < fuchsia::media::RENDER_USAGE_COUNT; i++) {
    auto usage = static_cast<fuchsia::media::AudioRenderUsage>(i);
    stream_volume_manager_.SetUsageGainAdjustment(
        fuchsia::media::Usage::WithRenderUsage(std::move(usage)), behavior_gain_.none_gain_db);
  }
  for (int i = 0; i < fuchsia::media::CAPTURE_USAGE_COUNT; i++) {
    auto usage = static_cast<fuchsia::media::AudioCaptureUsage>(i);
    stream_volume_manager_.SetUsageGainAdjustment(
        fuchsia::media::Usage::WithCaptureUsage(std::move(usage)), behavior_gain_.none_gain_db);
  }
}

void AudioAdmin::UpdatePolicy() {
  TRACE_DURATION("audio", "AudioAdmin::UpdatePolicy");
  // TODO(perley): convert this to an array of Usage unions or something else
  //               that makes it at least a little flexible.
  InitPolicies();
  if (IsActive(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT)) {
    ApplyPolicies(fuchsia::media::AudioCaptureUsage::SYSTEM_AGENT);
  }
  if (IsActive(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT)) {
    ApplyPolicies(fuchsia::media::AudioRenderUsage::SYSTEM_AGENT);
  }
  if (IsActive(fuchsia::media::AudioRenderUsage::INTERRUPTION)) {
    ApplyPolicies(fuchsia::media::AudioRenderUsage::INTERRUPTION);
  }
  if (IsActive(fuchsia::media::AudioCaptureUsage::COMMUNICATION)) {
    ApplyPolicies(fuchsia::media::AudioCaptureUsage::COMMUNICATION);
  }
  if (IsActive(fuchsia::media::AudioRenderUsage::COMMUNICATION)) {
    ApplyPolicies(fuchsia::media::AudioRenderUsage::COMMUNICATION);
  }
  if (IsActive(fuchsia::media::AudioRenderUsage::MEDIA)) {
    ApplyPolicies(fuchsia::media::AudioRenderUsage::MEDIA);
  }
  if (IsActive(fuchsia::media::AudioCaptureUsage::FOREGROUND)) {
    ApplyPolicies(fuchsia::media::AudioCaptureUsage::FOREGROUND);
  }
  if (IsActive(fuchsia::media::AudioCaptureUsage::BACKGROUND)) {
    ApplyPolicies(fuchsia::media::AudioCaptureUsage::BACKGROUND);
  }
  if (IsActive(fuchsia::media::AudioRenderUsage::BACKGROUND)) {
    ApplyPolicies(fuchsia::media::AudioRenderUsage::BACKGROUND);
  }
}

void AudioAdmin::UpdateActivity() {
  TRACE_DURATION("audio", "AudioAdmin::UpdateActivity");
  std::lock_guard<fxl::ThreadChecker> lock(fidl_thread_checker_);

  std::bitset<fuchsia::media::RENDER_USAGE_COUNT> activity;
  for (int i = 0; i < fuchsia::media::RENDER_USAGE_COUNT; i++) {
    if (IsActive(static_cast<fuchsia::media::AudioRenderUsage>(i))) {
      activity.set(i);
    }
  }
  activity_dispatcher_.OnActivityChanged(activity);
}

void AudioAdmin::UpdateRendererState(fuchsia::media::AudioRenderUsage usage, bool active,
                                     fuchsia::media::AudioRenderer* renderer) {
  async::PostTask(fidl_dispatcher_, [this, usage = usage, active = active, renderer = renderer] {
    TRACE_DURATION("audio", "AudioAdmin::UpdateRendererState");
    std::lock_guard<fxl::ThreadChecker> lock(fidl_thread_checker_);
    auto usage_index = fidl::ToUnderlying(usage);
    FX_DCHECK(usage_index < fuchsia::media::RENDER_USAGE_COUNT);
    if (active) {
      active_streams_playback_[usage_index].insert(renderer);
    } else {
      active_streams_playback_[usage_index].erase(renderer);
    }

    UpdatePolicy();
    UpdateActivity();
  });
}

void AudioAdmin::UpdateCapturerState(fuchsia::media::AudioCaptureUsage usage, bool active,
                                     fuchsia::media::AudioCapturer* capturer) {
  async::PostTask(fidl_dispatcher_, [this, usage = usage, active = active, capturer = capturer] {
    TRACE_DURATION("audio", "AudioAdmin::UpdateCapturerState");
    std::lock_guard<fxl::ThreadChecker> lock(fidl_thread_checker_);
    auto usage_index = fidl::ToUnderlying(usage);
    FX_DCHECK(usage_index < fuchsia::media::CAPTURE_USAGE_COUNT);
    if (active) {
      active_streams_capture_[usage_index].insert(capturer);
    } else {
      active_streams_capture_[usage_index].erase(capturer);
    }

    UpdatePolicy();
  });
}

void AudioAdmin::PolicyRules::ResetInteractions() {
  TRACE_DURATION("audio", "AudioAdmin::ResetInteractions");
  for (int i = 0; i < fuchsia::media::RENDER_USAGE_COUNT; i++) {
    auto active = static_cast<fuchsia::media::AudioRenderUsage>(i);
    for (int j = 0; j < fuchsia::media::RENDER_USAGE_COUNT; j++) {
      auto affected = static_cast<fuchsia::media::AudioRenderUsage>(j);
      SetRule(active, affected, fuchsia::media::Behavior::NONE);
    }
    for (int j = 0; j < fuchsia::media::CAPTURE_USAGE_COUNT; j++) {
      auto affected = static_cast<fuchsia::media::AudioCaptureUsage>(j);
      SetRule(active, affected, fuchsia::media::Behavior::NONE);
    }
  }
  for (int i = 0; i < fuchsia::media::CAPTURE_USAGE_COUNT; i++) {
    auto active = static_cast<fuchsia::media::AudioCaptureUsage>(i);
    for (int j = 0; j < fuchsia::media::RENDER_USAGE_COUNT; j++) {
      auto affected = static_cast<fuchsia::media::AudioRenderUsage>(j);
      SetRule(active, affected, fuchsia::media::Behavior::NONE);
    }
    for (int j = 0; j < fuchsia::media::CAPTURE_USAGE_COUNT; j++) {
      auto affected = static_cast<fuchsia::media::AudioCaptureUsage>(j);
      SetRule(active, affected, fuchsia::media::Behavior::NONE);
    }
  }
}

void AudioAdmin::SetInteractionsFromAudioPolicy(AudioPolicy policy) {
  async::PostTask(fidl_dispatcher_, [this, policy = std::move(policy)] {
    ResetInteractions();
    for (auto& rule : policy.rules()) {
      SetInteraction(fidl::Clone(rule.active), fidl::Clone(rule.affected), rule.behavior);
    }
  });
}

}  // namespace media::audio
