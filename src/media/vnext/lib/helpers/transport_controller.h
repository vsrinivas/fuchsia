// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_HELPERS_TRANSPORT_CONTROLLER_H_
#define SRC_MEDIA_VNEXT_LIB_HELPERS_TRANSPORT_CONTROLLER_H_

#include <fuchsia/media2/cpp/fidl.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/promise.h>
#include <lib/fpromise/scope.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <queue>

#include "src/media/vnext/lib/helpers/presentation_timeline.h"
#include "src/media/vnext/lib/threads/thread.h"

namespace fmlib {

// Handles transport control and related timing, including scheduling of events based on real or
// presentation times with cancellation.
// TODO(dalesat): Support SetRate.
class TransportController {
 private:
  struct Entry;

 public:
  // An object that may be used to cancel promises, causing them to complete with an error.
  // Instances of this class are copyable, and all copies have the capability of canceling the
  // promise in question.
  class Canceler {
   public:
    // Constructs an invalid |Canceler|.
    Canceler() = default;

    // Cancels the associated promise if it isn't already canceled. Canceled promises return an
    // error. Returns true if the promise was actually canceled, false if the promise was already
    // canceled or this canceler is invalid.
    bool Cancel();

    // Indicates whether this |Canceler| is valid. A |Canceler| produced by |MakePromiseForTime|,
    // |MakePromiseFor| or |MakePromiseForPresentationTime| or any copy thereof is valid until the
    // associated promise completes, either because the waited-for time arrived or one of the
    // canceler's |Cancel| method was invoked.
    bool is_valid() const { return entry_ && entry_->completer_; }
    explicit operator bool() const { return is_valid(); }

   private:
    explicit Canceler(std::shared_ptr<Entry> entry) : entry_(entry) {}

    std::shared_ptr<Entry> entry_;

    friend class TransportController;
  };

  // Makes a promise that completes at the specified ZX_CLOCK_MONOTONIC time with the option to get
  // a canceler for If |canceler_out| is not null, it is used to deliver a |Canceler| that can cause
  // the returned the promise. If |canceler_out| is null, this is equivalent to
  // |thread.MakePromiseForTime|. promise to fail if |Canceler::Cancel| is called before the promise
  // would otherwise complete.
  [[nodiscard]] static fpromise::promise<> MakePromiseForTime(Thread& thread, zx::time time,
                                                              Canceler* canceler_out = nullptr);

  // Constructs a |TransportController|. If |use_presentation_timer| is false, no timer will be
  // used for the presentation time queue, and |SetCurrentPresentationTime| must be called at high
  // frequency while the timeline is progressing. If |use_presentation_timer| is true, a timer is
  // used for the presentation time queue, and calling |SetCurrentPresentationTime| is not
  // recommended.
  explicit TransportController(bool use_presentation_timer = false)
      : use_presentation_timer_(use_presentation_timer) {}

  ~TransportController() = default;

  // Indicates whether the timeline is currently progressing.
  bool progressing() const { return timeline_.progressing(); }

  // Returns a reference to the timeline.
  const PresentationTimeline& timeline() const { return timeline_; }

  // Returns a reference/presentation time tuple for the current timeline. Typically used to
  // generate responses to transport control methods.
  std::tuple<int64_t, int64_t> ResponseTuple() {
    return std::make_tuple(timeline_.initial_reference_time().get(),
                           timeline_.initial_presentation_time().get());
  }

  // Handles the arrival of a |Start| request and returns a promise that starts the presentation
  // timeline as specified. The method handles much of the work invovled in implementing |Start|
  // or |Play| for most services. Pending |Start| and |Stop| requests are canceled as appropriate,
  // and the new request is validated, and, if the request is valid, the timeline is updated at the
  // time specified by |when| and the returned promise completes.
  //
  // |presentation_time| is the presentation time at which the timeline should be started, and
  // |margin| specifies are much in advance of |when| the start operation should occur.
  [[nodiscard]] fpromise::promise<void, fuchsia::media2::StartError> Start(
      Thread& thread, const fuchsia::media2::RealTimePtr& when, zx::duration presentation_time,
      zx::duration margin = zx::nsec(0));

  // Handles the arrival of a |Stop| request and returns a promise that stops the presentation
  // timeline as specified. The method handles much of the work invovled in implementing |Stop|
  // or |Pause| for most services. Pending |Start| and |Stop| requests are canceled as appropriate,
  // and the new request is validated, and, if the request is valid, the timeline is updated at the
  // time specified by |when| and the returned promise completes.
  //
  // |margin| specifies are much in advance of |when| the stop operation should occur.
  [[nodiscard]] fpromise::promise<void, fuchsia::media2::StopError> Stop(
      Thread& thread, const fuchsia::media2::RealOrPresentationTimePtr& when,
      zx::duration margin = zx::nsec(0));

  // Handles the arrival of an |AmendPresentation| request and returns a promise that amends the
  // presentation timeline as specified. The method handles much of the work invovled in
  // implementing |AmendPresentation| for most services. The request is validated, and, if the
  // request is valid, the timeline is updated at the time specified by |when| and the returned
  // promise completes.
  //
  // |delta| is the amount by which the timeline should be amended. |margin| specifies are much in
  // advance of |when| the stop operation should occur.
  [[nodiscard]] fpromise::promise<void, fuchsia::media2::AmendPresentationError> AmendPresentation(
      Thread& thread, const fuchsia::media2::RealOrPresentationTimePtr& when, zx::duration delta,
      zx::duration margin = zx::nsec(0));

  // Makes a promise that completes at the time described by |when|. If |when| is null, the returned
  // promise will be an 'ok promise', and the canceler will be invalid. If |when| is a system time
  // or reference time, this method returns the result of the static method |MakePromiseForTime|
  // called with the appropriate system time. If |when| is a presentation time, this method returns
  // the result of the method |MakePromiseForPresentationTime|.
  // If |canceler_out| is not null, it is used to deliver a |Canceler| that can cause the returned
  // promise to fail if |Canceler::Cancel| is called before the promise would otherwise complete.
  [[nodiscard]] fpromise::promise<> MakePromiseFor(
      Thread& thread, const fuchsia::media2::RealOrPresentationTimePtr& when,
      Canceler* canceler_out = nullptr);

  // Makes a promise that completes at the specified presentation time. Specifically, the promise
  // completes when the most recent presentation time reported via |SetCurrentPresentationTime|
  // is equal to or greater than the |presentation_time| argument.
  // If |canceler_out| is not null, it is used to deliver a |Canceler| that can cause the returned
  // promise to fail if |Canceler::Cancel| is called before the promise would otherwise complete.
  [[nodiscard]] fpromise::promise<> MakePromiseForPresentationTime(
      zx::duration presentation_time, Canceler* canceler_out = nullptr);

  // Updates the current presentation time and executes any tasks that have come due.
  void SetCurrentPresentationTime(zx::duration presentation_time);

  // Clears pending promises that are scheduled at a presentation time. Those promises complete with
  // an error. Does not affect promises that are scheduled at a real (system or reference) time.
  void CancelAllPresentationTimePromises();

 private:
  struct Entry {
    Entry(zx::duration presentation_time, fpromise::completer<> completer)
        : presentation_time_(presentation_time), completer_(std::move(completer)) {}
    zx::duration presentation_time_;
    // |std::priority_queue| gives us only const access to the items in the queue, so the only
    // way to take |completer_| is to make it mutable.
    mutable fpromise::completer<> completer_;
  };

  // Comparator for |presentation_time_entries_|.
  struct LaterPresentationTime {
    size_t operator()(const std::shared_ptr<Entry>& a, const std::shared_ptr<Entry>& b) {
      FX_CHECK(a);
      FX_CHECK(b);
      return a->presentation_time_ > b->presentation_time_;
    }
  };

  // Converts a reference time to a system time.
  // TODO(dalesat): Need to know about the reference clock, if there is one.
  zx::time ToSystemTime(zx::time reference_time) const { return reference_time; }

  // Converts a system time to a reference time.
  // TODO(dalesat): Need to know about the reference clock, if there is one.
  zx::time ToReferenceTime(zx::time system_time) const { return system_time; }

  // Converts a |fuchsia::media2::RealTime| to a system time.
  zx::time ToSystemTime(const fuchsia::media2::RealTimePtr& when, zx::time system_time_now) const;

  // Converts a |fuchsia::media2::RealOrPresentationTimePtr| to a system time. |progressing()| must
  // be true when this method is called.
  zx::time ToSystemTime(const fuchsia::media2::RealOrPresentationTimePtr& when,
                        zx::time system_time_now) const;

  // Converts a |fuchsia::media2::RealTimePtr| to a reference time.
  zx::time ToReferenceTime(const fuchsia::media2::RealTimePtr& when,
                           zx::time system_time_now) const;

  // Determines whether |when| specifies a time prior to a pending stop.
  bool PrecedesPendingStop(const fuchsia::media2::RealTimePtr& when, zx::time system_time_now);

  // Determines whether |when| specifies a time prior to a pending start.
  bool PrecedesPendingStart(const fuchsia::media2::RealOrPresentationTimePtr& when,
                            zx::time system_time_now);

  // Resolves a |fuchsia::media2::RealOrPresentationTimePtr| to a correlated presentation/reference
  // time pair.
  fmlib::ScheduledPresentationTime Resolve(const fuchsia::media2::RealOrPresentationTimePtr& when);

  // Starts a timer to run the presentation timer queue if |use_presentation_timer| was true in the
  // constructor, and the timeline is currently progressing.
  void MaybeStartPresentationTimer();

  PresentationTimeline timeline_;
  zx::duration presentation_time_;

  // A priority queue of entries to be completed at a particular presentation time. Eariliest
  // presentation times come out first.
  std::priority_queue<std::shared_ptr<Entry>, std::vector<std::shared_ptr<Entry>>,
                      LaterPresentationTime>
      presentation_time_entries_;

  fmlib::Thread thread_;

  fmlib::TransportController::Canceler start_canceler_;
  zx::time pending_start_system_time_;
  zx::duration pending_start_presentation_time_;

  fmlib::TransportController::Canceler stop_canceler_;
  fuchsia::media2::RealOrPresentationTimePtr pending_stop_time_;

  fmlib::TransportController::Canceler amend_canceler_;

  bool use_presentation_timer_;
  fmlib::TransportController::Canceler presentation_timer_canceler_;

  fpromise::scope scope_;
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_HELPERS_TRANSPORT_CONTROLLER_H_
