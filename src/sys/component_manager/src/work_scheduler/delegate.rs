// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        model::moniker::AbsoluteMoniker,
        work_scheduler::{
            dispatcher::Dispatcher,
            timer::WorkSchedulerTimer,
            work_item::WorkItem,
            work_scheduler::{WorkScheduler, WORKER_CAPABILITY_NAME},
        },
    },
    cm_rust::ComponentDecl,
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_sys2 as fsys,
    fuchsia_async::{self as fasync, Time},
    futures::lock::Mutex,
    std::{
        collections::HashMap,
        sync::{Arc, Weak},
    },
};

/// Business logic and state for `WorkScheduler` with all pub methods protected by a single `Mutex`.
/// This structure allows `WorkSchedulerDelegate` to assume that each public API flow is mutually
/// exclusive.
pub(super) struct WorkSchedulerDelegate {
    /// Scheduled work items that have not been dispatched.
    work_items: Vec<WorkItem>,
    /// Period between wakeup, batch, dispatch cycles. Set to `None` when dispatching work is
    /// disabled.
    batch_period: Option<i64>,
    /// Current timer for next wakeup, batch, dispatch cycle, if any.
    timer: Option<WorkSchedulerTimer>,
    /// Weak reference back to `WorkScheduler`. This reference is needed by `timer` to call back
    /// into `WorkScheduler.dispatch_work()`. Calling directly into
    /// `WorkSchedulerDelegate.dispatch_work()` is _not_ an option because it could violate mutual
    /// exclusion over pub methods on `WorkSchedulerDelegate'.
    weak_work_scheduler: Option<Weak<WorkScheduler>>,
    /// Instances that have exposed the Worker capability to the framework
    worker_monikers: Vec<AbsoluteMoniker>,
}

impl WorkSchedulerDelegate {
    /// `WorkSchedulerDelegate` is always instantiated inside a `Mutex` to enforce mutual exclusion
    /// between public API flows.
    pub(super) fn new() -> Mutex<Self> {
        Mutex::new(Self::new_raw())
    }

    fn new_raw() -> Self {
        Self {
            work_items: Vec::new(),
            batch_period: None,
            timer: None,
            weak_work_scheduler: None,
            worker_monikers: Vec::new(),
        }
    }

    /// Invoked immediately by `WorkScheduler` during initialization.
    pub(super) fn init(&mut self, weak_work_scheduler: Weak<WorkScheduler>) {
        self.weak_work_scheduler = Some(weak_work_scheduler);
    }

    /// Adds the realm's moniker to the list of instances that exposes the Worker capability
    /// to the framework.
    pub async fn try_add_realm_as_worker(
        &mut self,
        target_moniker: &AbsoluteMoniker,
        decl: &ComponentDecl,
    ) {
        if decl.is_protocol_exposed_to_framework(&WORKER_CAPABILITY_NAME) {
            self.worker_monikers.push(target_moniker.clone());
        }
    }

    /// Checks that this moniker is in the list of instances that expose the Worker capability
    /// to the framework.
    pub fn verify_worker_exposed_to_framework(&self, moniker: &AbsoluteMoniker) -> bool {
        self.worker_monikers.contains(&moniker)
    }

    /// `fuchsia.sys2.WorkScheduler.ScheduleWork` FIDL protocol method implementation.
    pub(super) fn schedule_work(
        &mut self,
        dispatcher: Arc<dyn Dispatcher>,
        work_id: &str,
        work_request: &fsys::WorkRequest,
    ) -> Result<(), fcomponent::Error> {
        let work_items = &mut self.work_items;
        let work_item = WorkItem::try_new(dispatcher, work_id, work_request)?;

        if work_items.contains(&work_item) {
            return Err(fcomponent::Error::InstanceAlreadyExists);
        }

        work_items.push(work_item);
        work_items.sort_by(WorkItem::deadline_order);

        self.update_timeout();

        Ok(())
    }

    /// `fuchsia.sys2.WorkScheduler.CancelWork` FIDL protocol method implementation.
    pub(super) fn cancel_work(
        &mut self,
        dispatcher: Arc<dyn Dispatcher>,
        work_id: &str,
    ) -> Result<(), fcomponent::Error> {
        let work_items = &mut self.work_items;
        let work_item = WorkItem::new_by_identity(dispatcher, work_id);

        // TODO(markdittmer): Use `work_items.remove_item(work_item)` if/when it becomes stable.
        let mut found = false;
        work_items.retain(|item| {
            let matches = &work_item == item;
            found = found || matches;
            !matches
        });

        if !found {
            return Err(fcomponent::Error::InstanceNotFound);
        }

        self.update_timeout();

        Ok(())
    }

    /// `fuchsia.sys2.WorkSchedulerControl.GetBatchPeriod` FIDL protocol method implementation.
    pub(super) fn get_batch_period(&self) -> Result<i64, fcomponent::Error> {
        match self.batch_period {
            Some(batch_period) => Ok(batch_period),
            // TODO(markdittmer): GetBatchPeriod Ok case should probably return Option<i64> to
            // more directly reflect "dispatching work disabled".
            None => Ok(std::i64::MAX),
        }
    }

    /// `fuchsia.sys2.WorkSchedulerControl.SetBatchPeriod` FIDL protocol method implementation.
    pub(super) fn set_batch_period(&mut self, batch_period: i64) -> Result<(), fcomponent::Error> {
        if batch_period <= 0 {
            return Err(fcomponent::Error::InvalidArguments);
        }

        if batch_period != std::i64::MAX {
            self.batch_period = Some(batch_period);
        } else {
            // TODO(markdittmer): SetBatchPeriod should probably accept Option<i64> to more directly
            // reflect "dispatching work disabled".
            self.batch_period = None;
        }

        self.update_timeout();

        Ok(())
    }

    /// Dispatch expired `work_items`. In the one-shot case expired items are dispatched and dropped
    /// from `work_items`. In the periodic case expired items are retained and given a new deadline.
    /// New deadlines must meet all of the following criteria:
    ///
    ///   now < new_deadline
    ///   and
    ///   now + period <= new_deadline
    ///   and
    ///   new_deadline = first_deadline + n * period
    ///
    /// Example:
    ///
    /// F = First expected dispatch time for work item
    /// C = Current expected dispatch time for work item
    /// N = Now
    /// * = New expected dispatch time for work item
    /// | = Period marker for work item (that isn't otherwise labeled)
    ///
    /// Period markers only:      ...------|----|----|----|----|----|----|----|----|...
    /// Fully annotated timeline: ...------F----|----C----|----|----|-N--*----|----|...
    ///
    /// Example of edge case:
    ///
    /// Now lands exactly on a period marker.
    ///
    /// Period markers only:      ...------|----|----|----|----|----|----|----|----|...
    /// Fully annotated timeline: ...------F----|----C----|----|----N----*----|----|...
    ///
    /// Example of edge case:
    ///
    /// Period markers only:      ...------||||||||||||||||||||...
    /// Fully annotated timeline: ...------F||C||||||||N*||||||...
    ///
    /// Example of edge case:
    ///
    /// N=C. Denote M = N=C.
    ///
    /// Period markers only:      ...------|----|----|----|----|----|...
    /// Fully annotated timeline: ...------F----|----M----*----|----|...
    ///
    /// Note that updating `WorkItem` deadlines is _independent_ of updating `WorkScheduler` batch
    /// period. When either `work_items` (and their deadlines) change or `batch_period` changes, the
    /// next wakeup timeout is re-evaluated, but this involves updating _only_ the wakeup timeout,
    /// not any `WorkItem` deadlines.
    pub(super) fn dispatch_work(&mut self) {
        let now = Time::now().into_nanos();
        let work_items = &mut self.work_items;
        let mut to_dispatch = HashMap::new();

        // Establish work item groups (by `AbsoluteMoniker`). This avoids lifetime issues that arise
        // when attempting to `to_dispatch.get_mut(item.dispatcher.abs_moniker())` where `item` does
        // not live as long as `to_dispatch`.
        for item in work_items.iter() {
            if !to_dispatch.contains_key(&item.dispatcher) {
                to_dispatch.insert(item.dispatcher.clone(), vec![]);
            }
        }

        work_items.retain(|item| {
            // Retain future work items.
            if item.next_deadline_monotonic > now {
                return true;
            }

            to_dispatch.get_mut(&item.dispatcher).unwrap().push(item.clone());

            // Only dispatched/past items to retain: periodic items that will recur.
            item.period.is_some()
        });

        // Dispatch work items that are due.
        // TODO(fxbug.dev/42310): It may be advantageous to spawn a separate task for each dispatcher.
        fasync::Task::spawn(async move {
            for (dispatcher, items) in to_dispatch.into_iter() {
                let _ = dispatcher.dispatch(items).await;
            }
        })
        .detach();

        // Update deadlines on dispatched periodic items.
        for mut item in work_items.iter_mut() {
            // Stop processing items once we reach future items.
            if item.next_deadline_monotonic > now {
                break;
            }

            // All retained dispatched/past items have a period (hence, safe to unwrap()).
            let period = item.period.unwrap();
            item.next_deadline_monotonic += if now < item.next_deadline_monotonic + period {
                // Normal case: next deadline after adding one period is in the future.
                period
            } else {
                // Skip deadlines in the past by advancing `next_deadline_monotonic` to the first
                // multiple of `period` after now
                period * (((now - item.next_deadline_monotonic) / period) + 1)
            };
        }

        work_items.sort_by(WorkItem::deadline_order);

        self.update_timeout();
    }

    /// Update the timeout for the next wakeup, batch, and dispatch cycle, if necessary. The timeout
    /// should be disabled if either there are no `work_items` or there is no `batch_period`.
    /// Otherwise, a suitable timeout may already be set. A suitable timeout is one that satisfies:
    ///
    ///   timeout > work_deadline
    ///   and
    ///   timeout - batch_period < work_deadline
    ///     where
    ///       work_deadline is the earliest expected dispatch time of all `work_items`
    ///
    /// That is, a suitable timeout will trigger after there is something to schedule, but before a
    /// full `batch_period` has elapsed since the next schedulable `WorkItem` hit its deadline.
    ///
    /// If the current timeout is not suitable, then the timeout is updated to the unique suitable
    /// timeout rounded to the nearest `batch_deadline` (in absolute monotonic time):
    ///
    ///   timeout > work_deadline
    ///   and
    ///   timeout - batch_period < work_deadline
    ///   and
    ///   (timeout % batch_period) == 0
    ///     where
    ///       work_deadline is the earliest expected dispatch time of all `work_items`
    ///
    /// This scheme avoids updating the timeout whenever possible, while maintaining that all
    /// scheduled `WorkItem` objects will be dispatched no later than
    /// `WorkItem.next_deadline_monotonic + WorkScheduler.batch_period`.
    fn update_timeout(&mut self) {
        let work_items = &self.work_items;
        let batch_period = self.batch_period;
        if work_items.is_empty() || batch_period.is_none() {
            // No work to schedule. Abort any existing timer to wakeup and dispatch work.
            self.timer = None;
            return;
        }
        let work_deadline = work_items[0].next_deadline_monotonic;
        let batch_period = batch_period.unwrap();

        if let Some(timer) = &self.timer {
            let timeout = timer.next_timeout_monotonic();
            if timeout > work_deadline && timeout - batch_period < work_deadline {
                // There is an active timeout that will fire after the next deadline but before a
                // full batch period has elapsed after the deadline. Timer needs no update.
                return;
            }
        }

        // Define a deadline, an absolute monotonic time, as the soonest time after `work_deadline`
        // that is aligned with `batch_period`.
        let new_deadline = work_deadline - (work_deadline % batch_period) + batch_period;
        self.timer = Some(WorkSchedulerTimer::new(
            new_deadline,
            self.weak_work_scheduler
                .as_ref()
                .expect("WorkSchedulerDelegate not initialized")
                .clone(),
        ));
    }
}

/// Provide test-only access to schedulable `WorkItem` instances.
#[cfg(test)]
impl WorkSchedulerDelegate {
    pub(super) fn work_items(&self) -> &Vec<WorkItem> {
        &self.work_items
    }
}

#[cfg(test)]
mod tests {
    use {
        super::WorkSchedulerDelegate,
        crate::{
            model::moniker::AbsoluteMoniker,
            work_scheduler::{
                dispatcher::{self as dspr, Dispatcher},
                work_item::WorkItem,
            },
        },
        fidl_fuchsia_component as fcomponent, fidl_fuchsia_sys2 as fsys,
        futures::future::BoxFuture,
        std::sync::Arc,
    };

    /// Time is measured in nanoseconds. This provides a constant symbol for one second.
    const SECOND: i64 = 1000000000;

    // Use arbitrary start monolithic time. This will surface bugs that, for example, are not
    // apparent when "time starts at 0".
    const FAKE_MONOTONIC_TIME: i64 = 374789234875;

    impl Dispatcher for AbsoluteMoniker {
        fn abs_moniker(&self) -> &AbsoluteMoniker {
            &self
        }
        fn dispatch(&self, _work_items: Vec<WorkItem>) -> BoxFuture<Result<(), dspr::Error>> {
            Box::pin(async move { Err(dspr::Error::ComponentNotRunning) })
        }
    }

    fn dispatcher(s: &str) -> Arc<dyn Dispatcher> {
        Arc::new(AbsoluteMoniker::from(vec![s]))
    }

    impl WorkSchedulerDelegate {
        fn schedule_work_item(
            &mut self,
            component_id: &str,
            work_id: &str,
            work_request: &fsys::WorkRequest,
        ) -> Result<(), fcomponent::Error> {
            self.schedule_work(dispatcher(component_id), work_id, work_request)
        }

        fn cancel_work_item(
            &mut self,
            component_id: &str,
            work_id: &str,
        ) -> Result<(), fcomponent::Error> {
            self.cancel_work(Arc::new(AbsoluteMoniker::from(vec![component_id])), work_id)
        }

        fn get_work_status(
            &self,
            component_id: &str,
            work_id: &str,
        ) -> Result<(i64, Option<i64>), fcomponent::Error> {
            let abs_moniker: AbsoluteMoniker = vec![component_id].into();
            match self.work_items.iter().find(|work_item| {
                work_item.dispatcher.abs_moniker() == &abs_moniker && work_item.id == work_id
            }) {
                Some(work_item) => Ok((work_item.next_deadline_monotonic, work_item.period)),
                None => Err(fcomponent::Error::InstanceNotFound),
            }
        }
    }

    #[test]
    fn work_scheduler_basic() {
        let mut work_scheduler = WorkSchedulerDelegate::new_raw();
        let a = "a:0";
        let b = "b:0";
        let c = "c:0";

        let now_once = fsys::WorkRequest {
            start: Some(fsys::Start::MonotonicTime(FAKE_MONOTONIC_TIME)),
            period: None,
        };
        let each_second = fsys::WorkRequest {
            start: Some(fsys::Start::MonotonicTime(FAKE_MONOTONIC_TIME + SECOND)),
            period: Some(SECOND),
        };
        let in_an_hour = fsys::WorkRequest {
            start: Some(fsys::Start::MonotonicTime(FAKE_MONOTONIC_TIME + (SECOND * 60 * 60))),
            period: None,
        };

        // Schedule different 2 out of 3 requests on each component instance.

        assert_eq!(Ok(()), work_scheduler.schedule_work_item(a, "NOW_ONCE", &now_once));
        assert_eq!(Ok(()), work_scheduler.schedule_work_item(a, "EACH_SECOND", &each_second));

        assert_eq!(Ok(()), work_scheduler.schedule_work_item(b, "EACH_SECOND", &each_second));
        assert_eq!(Ok(()), work_scheduler.schedule_work_item(b, "IN_AN_HOUR", &in_an_hour));

        assert_eq!(Ok(()), work_scheduler.schedule_work_item(c, "IN_AN_HOUR", &in_an_hour));
        assert_eq!(Ok(()), work_scheduler.schedule_work_item(c, "NOW_ONCE", &now_once));

        assert_eq!(Ok((FAKE_MONOTONIC_TIME, None)), work_scheduler.get_work_status(a, "NOW_ONCE"));
        assert_eq!(
            Ok((FAKE_MONOTONIC_TIME + SECOND, Some(SECOND))),
            work_scheduler.get_work_status(a, "EACH_SECOND")
        );
        assert_eq!(
            Err(fcomponent::Error::InstanceNotFound),
            work_scheduler.get_work_status(a, "IN_AN_HOUR")
        );

        assert_eq!(
            Err(fcomponent::Error::InstanceNotFound),
            work_scheduler.get_work_status(b, "NOW_ONCE")
        );
        assert_eq!(
            Ok((FAKE_MONOTONIC_TIME + SECOND, Some(SECOND))),
            work_scheduler.get_work_status(b, "EACH_SECOND")
        );
        assert_eq!(
            Ok((FAKE_MONOTONIC_TIME + (SECOND * 60 * 60), None)),
            work_scheduler.get_work_status(b, "IN_AN_HOUR")
        );

        assert_eq!(Ok((FAKE_MONOTONIC_TIME, None)), work_scheduler.get_work_status(c, "NOW_ONCE"));
        assert_eq!(
            Err(fcomponent::Error::InstanceNotFound),
            work_scheduler.get_work_status(c, "EACH_SECOND")
        );
        assert_eq!(
            Ok((FAKE_MONOTONIC_TIME + (SECOND * 60 * 60), None)),
            work_scheduler.get_work_status(c, "IN_AN_HOUR")
        );

        // Cancel a's NOW_ONCE. Confirm it only affects a's scheduled work.

        assert_eq!(Ok(()), work_scheduler.cancel_work_item(a, "NOW_ONCE"));

        assert_eq!(
            Err(fcomponent::Error::InstanceNotFound),
            work_scheduler.get_work_status(a, "NOW_ONCE")
        );
        assert_eq!(
            Ok((FAKE_MONOTONIC_TIME + SECOND, Some(SECOND))),
            work_scheduler.get_work_status(a, "EACH_SECOND")
        );
        assert_eq!(
            Err(fcomponent::Error::InstanceNotFound),
            work_scheduler.get_work_status(a, "IN_AN_HOUR")
        );

        assert_eq!(
            Err(fcomponent::Error::InstanceNotFound),
            work_scheduler.get_work_status(b, "NOW_ONCE")
        );
        assert_eq!(
            Ok((FAKE_MONOTONIC_TIME + SECOND, Some(SECOND))),
            work_scheduler.get_work_status(b, "EACH_SECOND")
        );
        assert_eq!(
            Ok((FAKE_MONOTONIC_TIME + (SECOND * 60 * 60), None)),
            work_scheduler.get_work_status(b, "IN_AN_HOUR")
        );

        assert_eq!(Ok((FAKE_MONOTONIC_TIME, None)), work_scheduler.get_work_status(c, "NOW_ONCE"));
        assert_eq!(
            Err(fcomponent::Error::InstanceNotFound),
            work_scheduler.get_work_status(c, "EACH_SECOND")
        );
        assert_eq!(
            Ok((FAKE_MONOTONIC_TIME + (SECOND * 60 * 60), None)),
            work_scheduler.get_work_status(c, "IN_AN_HOUR")
        );
    }

    #[test]
    fn work_scheduler_deadline_order() {
        let mut work_scheduler = WorkSchedulerDelegate::new_raw();
        let a = "a:0";
        let b = "b:0";
        let c = "c:0";

        let now_once = fsys::WorkRequest {
            start: Some(fsys::Start::MonotonicTime(FAKE_MONOTONIC_TIME)),
            period: None,
        };
        let each_second = fsys::WorkRequest {
            start: Some(fsys::Start::MonotonicTime(FAKE_MONOTONIC_TIME + SECOND)),
            period: Some(SECOND),
        };
        let in_an_hour = fsys::WorkRequest {
            start: Some(fsys::Start::MonotonicTime(FAKE_MONOTONIC_TIME + (SECOND * 60 * 60))),
            period: None,
        };

        assert_eq!(Ok(()), work_scheduler.schedule_work_item(a, "EACH_SECOND", &each_second));
        assert_eq!(Ok(()), work_scheduler.schedule_work_item(c, "NOW_ONCE", &now_once));
        assert_eq!(Ok(()), work_scheduler.schedule_work_item(b, "IN_AN_HOUR", &in_an_hour));

        // Order should match deadlines, not order of scheduling or component topology.
        assert_eq!(
            vec![
                WorkItem::new(dispatcher(c), "NOW_ONCE", FAKE_MONOTONIC_TIME, None),
                WorkItem::new(
                    dispatcher(a),
                    "EACH_SECOND",
                    FAKE_MONOTONIC_TIME + SECOND,
                    Some(SECOND),
                ),
                WorkItem::new(
                    dispatcher(b),
                    "IN_AN_HOUR",
                    FAKE_MONOTONIC_TIME + (SECOND * 60 * 60),
                    None,
                ),
            ],
            work_scheduler.work_items
        );
    }

    #[test]
    fn work_scheduler_batch_period() {
        let mut work_scheduler = WorkSchedulerDelegate::new_raw();
        assert_eq!(Ok(std::i64::MAX), work_scheduler.get_batch_period());
        assert_eq!(Ok(()), work_scheduler.set_batch_period(SECOND));
        assert_eq!(Ok(SECOND), work_scheduler.get_batch_period())
    }

    #[test]
    fn work_scheduler_batch_period_error() {
        let mut work_scheduler = WorkSchedulerDelegate::new_raw();
        assert_eq!(Err(fcomponent::Error::InvalidArguments), work_scheduler.set_batch_period(0));
        assert_eq!(Err(fcomponent::Error::InvalidArguments), work_scheduler.set_batch_period(-1))
    }
}
