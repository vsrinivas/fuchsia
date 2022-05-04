// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/helpers/transport_controller.h"

namespace fmlib {

// static
fpromise::promise<> TransportController::MakePromiseForTime(Thread& thread, zx::time time,
                                                            Canceler* canceler_out) {
  if (!canceler_out) {
    return thread.MakePromiseForTime(time);
  }

  fpromise::bridge<> bridge;
  auto entry = std::make_shared<Entry>(zx::duration(), std::move(bridge.completer));
  *canceler_out = Canceler(entry);
  thread.PostTaskForTime(
      [entry]() {
        if (entry->completer_) {
          entry->completer_.complete_ok();
        }
      },
      time);

  return bridge.consumer.promise();
}

fpromise::promise<void, fuchsia::media2::StartError> TransportController::Start(
    Thread& thread, const fuchsia::media2::RealTimePtr& when, zx::duration presentation_time,
    zx::duration margin) {
  if (!progressing() && stop_canceler_) {
    FX_CHECK(start_canceler_);
    // There's a pending stop subsequent to a pending start. Cancel it.
    stop_canceler_.Cancel();
  }

  // Cancel a pending start, if there is one.
  start_canceler_.Cancel();

  zx::time now = zx::clock::get_monotonic();

  if (progressing()) {
    if (stop_canceler_) {
      // We're progressing, so we need to evaluate the new request in the context of the pending
      // stop request.
      if (PrecedesPendingStop(when, now)) {
        // We're progressing, and this new request precedes its antecedent, so return an error.
        return fpromise::make_error_promise(fuchsia::media2::StartError::PRECEDES_PENDING_STOP);
      }
    } else {
      // We're progressing, and there's no pending stop, so return an error.
      return fpromise::make_error_promise(fuchsia::media2::StartError::ALREADY_STARTED);
    }
  }

  pending_start_system_time_ = ToSystemTime(when, now);
  pending_start_presentation_time_ = presentation_time;

  return MakePromiseForTime(thread, pending_start_system_time_ - margin, &start_canceler_)
      .then([this](
                fpromise::result<>& result) -> fpromise::result<void, fuchsia::media2::StartError> {
        if (result.is_error()) {
          return fpromise::error(fuchsia::media2::StartError::CANCELED);
        }

        timeline_.initial_presentation_time() = pending_start_presentation_time_;
        timeline_.initial_reference_time() = ToReferenceTime(pending_start_system_time_);
        timeline_.progressing() = true;
        MaybeStartPresentationTimer();
        return fpromise::ok();
      })
      .wrap_with(scope_);
}

fpromise::promise<void, fuchsia::media2::StopError> TransportController::Stop(
    Thread& thread, const fuchsia::media2::RealOrPresentationTimePtr& when, zx::duration margin) {
  if (progressing() && start_canceler_) {
    FX_CHECK(stop_canceler_);
    // There's a pending start subsequent to a pending stop. Cancel it.
    start_canceler_.Cancel();
  }

  // Cancel a pending stop, if there is one.
  stop_canceler_.Cancel();

  zx::time now = zx::clock::get_monotonic();

  if (!progressing()) {
    if (start_canceler_) {
      // We're not progressing, so we need to evaluate the new request in the context of the pending
      // start request.
      if (PrecedesPendingStart(when, now)) {
        // We're not progressing, and this new request precedes its antecedent, so return an error.
        return fpromise::make_error_promise(fuchsia::media2::StopError::PRECEDES_PENDING_START);
      }
    } else {
      // We're not progressing, and there's no pending start, so return an error.
      return fpromise::make_error_promise(fuchsia::media2::StopError::ALREADY_STOPPED);
    }
  }

  pending_stop_time_ = fidl::Clone(when);

  return MakePromiseFor(thread, when, &stop_canceler_)
      .then(
          [this](fpromise::result<>& result) -> fpromise::result<void, fuchsia::media2::StopError> {
            if (result.is_error()) {
              return fpromise::error(fuchsia::media2::StopError::CANCELED);
            }

            timeline_.time() = Resolve(pending_stop_time_);
            timeline_.progressing() = false;

            presentation_timer_canceler_.Cancel();

            return fpromise::ok();
          })
      .wrap_with(scope_);
}

fpromise::promise<void, fuchsia::media2::AmendPresentationError>
TransportController::AmendPresentation(Thread& thread,
                                       const fuchsia::media2::RealOrPresentationTimePtr& when,
                                       zx::duration delta, zx::duration margin) {
  // Cancel a pending amendment, if there is one.
  amend_canceler_.Cancel();

  if (!progressing()) {
    return fpromise::make_error_promise(fuchsia::media2::AmendPresentationError::NOT_STARTED);
  }

  return MakePromiseFor(thread, when, &amend_canceler_)
      .then([this, delta](fpromise::result<>& result)
                -> fpromise::result<void, fuchsia::media2::AmendPresentationError> {
        if (result.is_error()) {
          return fpromise::error(fuchsia::media2::AmendPresentationError::CANCELED);
        }

        timeline_.initial_presentation_time() += zx::duration(delta);
        MaybeStartPresentationTimer();

        return fpromise::ok();
      })
      .wrap_with(scope_);
}

fpromise::promise<> TransportController::MakePromiseFor(
    Thread& thread, const fuchsia::media2::RealOrPresentationTimePtr& when,
    Canceler* canceler_out) {
  if (!when) {
    if (canceler_out) {
      *canceler_out = Canceler(nullptr);
    }

    return fpromise::make_ok_promise();
  }

  switch (when->Which()) {
    case fuchsia::media2::RealOrPresentationTime::Tag::kSystemTime:
      return MakePromiseForTime(thread, zx::time(when->system_time()), canceler_out);
    case fuchsia::media2::RealOrPresentationTime::Tag::kReferenceTime:
      return MakePromiseForTime(thread, ToSystemTime(zx::time(when->reference_time())),
                                canceler_out);
    case fuchsia::media2::RealOrPresentationTime::Tag::kPresentationTime:
      return MakePromiseForPresentationTime(zx::duration(when->presentation_time()), canceler_out);
    case fuchsia::media2::RealOrPresentationTime::Tag::Invalid:
    case fuchsia::media2::RealOrPresentationTime::Tag::kUnknown:
      FX_CHECK(false);
      return fpromise::make_error_promise();
  }
}

fpromise::promise<> TransportController::MakePromiseForPresentationTime(
    zx::duration presentation_time, Canceler* canceler_out) {
  if (presentation_time <= presentation_time_) {
    return fpromise::make_ok_promise();
  }

  fpromise::bridge<> bridge;
  auto entry = std::make_shared<Entry>(presentation_time, std::move(bridge.completer));
  if (canceler_out) {
    *canceler_out = Canceler(entry);
  }

  presentation_time_entries_.push(entry);
  MaybeStartPresentationTimer();

  return bridge.consumer.promise();
}

void TransportController::SetCurrentPresentationTime(zx::duration presentation_time) {
  presentation_time_ = presentation_time;

  while (!presentation_time_entries_.empty() &&
         presentation_time_entries_.top()->presentation_time_ <= presentation_time) {
    if (presentation_time_entries_.top()->completer_) {
      presentation_time_entries_.top()->completer_.complete_ok();
    }

    presentation_time_entries_.pop();
  }
}

void TransportController::CancelAllPresentationTimePromises() {
  while (!presentation_time_entries_.empty()) {
    if (presentation_time_entries_.top()->completer_) {
      presentation_time_entries_.top()->completer_.complete_error();
    }

    presentation_time_entries_.pop();
  }
}

zx::time TransportController::ToSystemTime(const fuchsia::media2::RealTimePtr& when,
                                           zx::time system_time_now) const {
  if (!when) {
    return system_time_now;
  }

  switch (when->Which()) {
    case fuchsia::media2::RealTime::Tag::kReferenceTime:
      return zx::time(ToSystemTime(zx::time(when->reference_time())));
    case fuchsia::media2::RealTime::Tag::kSystemTime:
      return zx::time(when->system_time());
    default:
      FX_CHECK(false);
      return zx::time();
  }
}

zx::time TransportController::ToSystemTime(const fuchsia::media2::RealOrPresentationTimePtr& when,
                                           zx::time system_time_now) const {
  FX_CHECK(progressing());
  if (!when) {
    return system_time_now;
  }

  switch (when->Which()) {
    case fuchsia::media2::RealOrPresentationTime::Tag::kReferenceTime:
      return zx::time(ToSystemTime(zx::time(when->reference_time())));
    case fuchsia::media2::RealOrPresentationTime::Tag::kSystemTime:
      return zx::time(when->system_time());
    case fuchsia::media2::RealOrPresentationTime::Tag::kPresentationTime:
      return ToSystemTime(timeline_.ToReferenceTime(zx::duration(when->presentation_time())));
    default:
      FX_CHECK(false);
      return zx::time();
  }
}

zx::time TransportController::ToReferenceTime(const fuchsia::media2::RealTimePtr& when,
                                              zx::time system_time_now) const {
  if (!when) {
    return ToReferenceTime(system_time_now);
  }

  switch (when->Which()) {
    case fuchsia::media2::RealTime::Tag::kReferenceTime:
      return zx::time(when->reference_time());
    case fuchsia::media2::RealTime::Tag::kSystemTime:
      return ToReferenceTime(zx::time(when->system_time()));
    default:
      FX_CHECK(false);
      return zx::time();
  }
}

bool TransportController::PrecedesPendingStop(const fuchsia::media2::RealTimePtr& when,
                                              zx::time system_time_now) {
  return ToSystemTime(when, system_time_now) < ToSystemTime(pending_stop_time_, system_time_now);
}

bool TransportController::PrecedesPendingStart(
    const fuchsia::media2::RealOrPresentationTimePtr& when, zx::time system_time_now) {
  if (!when) {
    return system_time_now >= pending_start_system_time_;
  }

  switch (when->Which()) {
    case fuchsia::media2::RealOrPresentationTime::Tag::kReferenceTime:
      return ToSystemTime(zx::time(when->reference_time())) >= pending_start_system_time_;
    case fuchsia::media2::RealOrPresentationTime::Tag::kSystemTime:
      return zx::time(when->system_time()) >= pending_start_system_time_;
    case fuchsia::media2::RealOrPresentationTime::Tag::kPresentationTime:
      return zx::duration(when->presentation_time()) >= pending_start_presentation_time_;
    default:
      FX_CHECK(false);
      return false;
  }
}

fmlib::ScheduledPresentationTime TransportController::Resolve(
    const fuchsia::media2::RealOrPresentationTimePtr& when) {
  if (!when) {
    zx::time reference_now = ToReferenceTime(zx::clock::get_monotonic());
    return fmlib::ScheduledPresentationTime(timeline_.ToPresentationTime(reference_now),
                                            reference_now);
  }

  switch (when->Which()) {
    case fuchsia::media2::RealOrPresentationTime::Tag::kSystemTime: {
      zx::time reference_time = ToReferenceTime(zx::time(when->system_time()));
      return fmlib::ScheduledPresentationTime(timeline_.ToPresentationTime(reference_time),
                                              reference_time);
    }
    case fuchsia::media2::RealOrPresentationTime::Tag::kReferenceTime: {
      zx::time reference_time = zx::time(when->reference_time());
      return fmlib::ScheduledPresentationTime(timeline_.ToPresentationTime(reference_time),
                                              reference_time);
    }
    case fuchsia::media2::RealOrPresentationTime::Tag::kPresentationTime: {
      zx::duration presentation_time = zx::duration(when->presentation_time());
      return fmlib::ScheduledPresentationTime(presentation_time,
                                              timeline_.ToReferenceTime(presentation_time));
    }
    case fuchsia::media2::RealOrPresentationTime::Tag::kUnknown:
      FX_LOGS(FATAL) << "got Tag::kUnknown RealOrPresentationTime value";
      return fmlib::ScheduledPresentationTime(zx::duration(), zx::time());
    case fuchsia::media2::RealOrPresentationTime::Tag::Invalid:
      FX_LOGS(FATAL) << "got Tag::Invalid RealOrPresentationTime value";
      return fmlib::ScheduledPresentationTime(zx::duration(), zx::time());
  }
}

void TransportController::MaybeStartPresentationTimer() {
  if (!use_presentation_timer_ || presentation_time_entries_.empty() || !timeline_.progressing()) {
    return;
  }

  presentation_timer_canceler_.Cancel();

  thread_.schedule_task(
      MakePromiseForTime(thread_,
                         ToSystemTime(timeline_.ToReferenceTime(
                             presentation_time_entries_.top()->presentation_time_)),
                         &presentation_timer_canceler_)
          .and_then([this]() {
            if (!timeline_.progressing()) {
              return;
            }

            SetCurrentPresentationTime(
                timeline_.ToPresentationTime(ToReferenceTime(zx::clock::get_monotonic())));

            MaybeStartPresentationTimer();
          }));
}

/////////////////////////////////////////////////////////////////////////////////////////////////
// TransportController::Canceler definitions.

bool TransportController::Canceler::Cancel() {
  if (!is_valid()) {
    return false;
  }

  entry_->completer_.complete_error();
  entry_ = nullptr;

  return true;
}

}  // namespace fmlib
