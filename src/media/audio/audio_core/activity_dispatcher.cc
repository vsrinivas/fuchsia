// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/activity_dispatcher.h"

#include <optional>

namespace media::audio {
namespace {
using fuchsia::media::AudioRenderUsage;

std::vector<AudioRenderUsage> ActivityToUsageVector(
    const ActivityDispatcherImpl::RenderActivity& activity) {
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
  explicit ActivityReporterImpl(const RenderActivity& last_known_render_activity,
                                fit::callback<void(ActivityReporterImpl*)> on_client_error);
  ~ActivityReporterImpl() override;

  // Signal that the activity changed.
  void OnRenderActivityChanged();

  // Handle unresponsive client.
  void OnClientError();

 private:
  // fuchsia::media::ActivityReporter.
  void WatchRenderActivity(WatchRenderActivityCallback callback) override;

  // Class to manage sending Activity updates to clients.
  // An instance of Reporter can be specified to report on RenderActivity.
  template <typename Activity, typename Callback>
  class Reporter {
   public:
    Reporter(ActivityDispatcherImpl::ActivityReporterImpl& parent, const Activity& activity)
        : parent_(parent), last_known_activity_(activity){};
    ~Reporter<Activity, Callback>() = default;
    void WatchActivity(Callback callback);
    void MaybeSendActivity();

   private:
    // Parent class to provide OnClientError().
    ActivityDispatcherImpl::ActivityReporterImpl& parent_;
    // Last state known by the dispatcher.
    const Activity& last_known_activity_;
    // Last activity sent to client.
    // Absent if no  state was sent to the client yet.
    std::optional<Activity> last_sent_activity_;
    // If present, callback to call next time a state is available.
    Callback waiting_callback_;
  };

  Reporter<RenderActivity, WatchRenderActivityCallback> render_reporter_;
  // Called when the client has more than one hanging gets in flight.
  fit::callback<void(ActivityReporterImpl*)> on_client_error_;
};

ActivityDispatcherImpl::ActivityDispatcherImpl() = default;
ActivityDispatcherImpl::~ActivityDispatcherImpl() = default;

ActivityDispatcherImpl::ActivityReporterImpl::ActivityReporterImpl(
    const RenderActivity& last_known_render_activity,
    fit::callback<void(ActivityReporterImpl*)> on_client_error)
    : render_reporter_(*this, last_known_render_activity),
      on_client_error_(std::move(on_client_error)) {}

ActivityDispatcherImpl::ActivityReporterImpl::~ActivityReporterImpl() = default;

void ActivityDispatcherImpl::ActivityReporterImpl::OnRenderActivityChanged() {
  render_reporter_.MaybeSendActivity();
}

void ActivityDispatcherImpl::ActivityReporterImpl::OnClientError() { on_client_error_(this); }

void ActivityDispatcherImpl::ActivityReporterImpl::WatchRenderActivity(
    WatchRenderActivityCallback callback) {
  render_reporter_.WatchActivity(std::move(callback));
}

template <typename Activity, typename Callback>
void ActivityDispatcherImpl::ActivityReporterImpl::Reporter<Activity, Callback>::WatchActivity(
    Callback callback) {
  // If there is more than one hanging get in flight, disconnect the client.
  if (waiting_callback_) {
    parent_.OnClientError();
    return;
  }

  waiting_callback_ = std::move(callback);
  MaybeSendActivity();
}

template <typename Activity, typename Callback>
void ActivityDispatcherImpl::ActivityReporterImpl::Reporter<Activity,
                                                            Callback>::MaybeSendActivity() {
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
          last_known_render_activity_,
          [this](ActivityReporterImpl* impl) { bindings_.CloseBinding(impl, kEpitaphValue); }),
      std::move(request));
}

void ActivityDispatcherImpl::OnRenderActivityChanged(RenderActivity activity) {
  last_known_render_activity_ = activity;
  for (const auto& listener : bindings_.bindings()) {
    listener->impl()->OnRenderActivityChanged();
  }
}
}  // namespace media::audio
