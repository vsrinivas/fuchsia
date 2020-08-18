// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/activity_dispatcher.h"

#include <optional>

namespace media::audio {
namespace {
using fuchsia::media::AudioRenderUsage;

std::vector<AudioRenderUsage> ActivityToUsageVector(
    const ActivityDispatcherImpl::Activity& activity) {
  std::vector<AudioRenderUsage> usage_vector;
  usage_vector.reserve(activity.count());

  for (int i = 0; i < fuchsia::media::RENDER_USAGE_COUNT; i++) {
    if (activity[i]) {
      usage_vector.push_back(static_cast<AudioRenderUsage>(i));
    }
  }
  return usage_vector;
}
}  // namespace

class ActivityDispatcherImpl::ActivityReporterImpl : public fuchsia::media::ActivityReporter {
 public:
  // The activity must outlive the ActivityReporterImpl.
  explicit ActivityReporterImpl(const Activity& last_known_activity,
                                fit::callback<void(ActivityReporterImpl*)> on_client_error);
  ~ActivityReporterImpl() override;

  // Signal that the activity changed.
  void OnActivityChanged();

 private:
  // fuchsia::media::ActivityReporter.
  void WatchRenderActivity(WatchRenderActivityCallback callback) override;
  // Send Activity if there was some update.
  void MaybeSendActivity();

  // Last state known by the dispatcher.
  const Activity& last_known_activity_;
  // Last activity sent to client.
  // Absent if no  state was sent to the client yet.
  std::optional<Activity> last_sent_activity_;
  // If present, callback to call next time a state is available.
  WatchRenderActivityCallback waiting_callback_;
  // Called when the client has more than one hanging gets in flight.
  fit::callback<void(ActivityReporterImpl*)> on_client_error_;
};

ActivityDispatcherImpl::ActivityDispatcherImpl() = default;
ActivityDispatcherImpl::~ActivityDispatcherImpl() = default;

ActivityDispatcherImpl::ActivityReporterImpl::ActivityReporterImpl(
    const Activity& last_known_activity, fit::callback<void(ActivityReporterImpl*)> on_client_error)
    : last_known_activity_(last_known_activity), on_client_error_(std::move(on_client_error)) {}

ActivityDispatcherImpl::ActivityReporterImpl::~ActivityReporterImpl() = default;

void ActivityDispatcherImpl::ActivityReporterImpl::OnActivityChanged() { MaybeSendActivity(); }

void ActivityDispatcherImpl::ActivityReporterImpl::WatchRenderActivity(
    WatchRenderActivityCallback callback) {
  // If there is more than one hanging get in flight, disconnect the client.
  if (waiting_callback_) {
    on_client_error_(this);
    return;
  }

  waiting_callback_ = std::move(callback);
  MaybeSendActivity();
}

void ActivityDispatcherImpl::ActivityReporterImpl::MaybeSendActivity() {
  // No request in flight.
  if (!waiting_callback_) {
    return;
  }

  // No new update.
  if (last_sent_activity_.has_value() && (last_sent_activity_.value() == last_known_activity_)) {
    return;
  }

  waiting_callback_(ActivityToUsageVector(last_known_activity_));
  last_sent_activity_ = last_known_activity_;
  waiting_callback_ = nullptr;
}

fidl::InterfaceRequestHandler<fuchsia::media::ActivityReporter>
ActivityDispatcherImpl::GetFidlRequestHandler() {
  return fit::bind_member(this, &ActivityDispatcherImpl::Bind);
}

void ActivityDispatcherImpl::Bind(
    fidl::InterfaceRequest<fuchsia::media::ActivityReporter> request) {
  constexpr auto kEpitaphValue = ZX_ERR_PEER_CLOSED;
  bindings_.AddBinding(
      std::make_unique<ActivityReporterImpl>(
          last_known_activity_,
          [this](ActivityReporterImpl* impl) { bindings_.CloseBinding(impl, kEpitaphValue); }),
      std::move(request));
}

void ActivityDispatcherImpl::OnActivityChanged(Activity activity) {
  last_known_activity_ = activity;
  for (const auto& listener : bindings_.bindings()) {
    listener->impl()->OnActivityChanged();
  }
}
}  // namespace media::audio
