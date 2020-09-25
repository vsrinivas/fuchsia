// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains the core algorithm for `WorkScheduler`, a component manager subsytem for
//! dispatching batches of work.
//!
//! The subsystem's interface consists of the following three FIDL prototocols:
//!
//! * `fuchsia.sys2.WorkScheduler`: A framework service for scheduling and canceling work.
//! * `fuchsia.sys2.Worker`: A service that `WorkScheduler` clients expose to the framework to be
//!   notified when work units are dispatched.
//! * `fuchsia.sys2.WorkSchedulerControl`: A built-in service for controlling the period between
//!   wakeup, batch, and dispatch cycles.

use {
    crate::{
        model::{binding::Binder, moniker::AbsoluteMoniker},
        work_scheduler::{delegate::WorkSchedulerDelegate, dispatcher::RealDispatcher},
    },
    cm_rust::{CapabilityName, ComponentDecl},
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_sys2 as fsys,
    futures::lock::Mutex,
    lazy_static::lazy_static,
    std::sync::Arc,
};

// If you change this block, please update test `work_scheduler_capability_paths`.
lazy_static! {
    pub static ref WORKER_CAPABILITY_NAME: CapabilityName = "fuchsia.sys2.Worker".into();
    pub static ref WORK_SCHEDULER_CAPABILITY_NAME: CapabilityName =
        "fuchsia.sys2.WorkScheduler".into();
    pub static ref WORK_SCHEDULER_CONTROL_CAPABILITY_NAME: CapabilityName =
        "fuchsia.sys2.WorkSchedulerControl".into();
}

/// Owns the `Mutex`-synchronized `WorkSchedulerDelegate`, which contains business logic and state
/// for the `WorkScheduler` instance.
pub struct WorkScheduler {
    /// Delegate that implements business logic and holds state behind `Mutex`.
    delegate: Mutex<WorkSchedulerDelegate>,
    /// Used to bind to component instances during dispatch.
    binder: Arc<dyn Binder>,
}

impl WorkScheduler {
    // `Workscheduler` is always instantiated in an `Arc` that will determine its lifetime.
    pub async fn new(binder: Arc<dyn Binder>) -> Arc<Self> {
        let work_scheduler = Self::new_raw(binder);
        {
            let mut delegate = work_scheduler.delegate.lock().await;
            delegate.init(Arc::downgrade(&work_scheduler));
        }
        work_scheduler
    }

    fn new_raw(binder: Arc<dyn Binder>) -> Arc<Self> {
        Arc::new(Self { delegate: WorkSchedulerDelegate::new(), binder })
    }

    /// `schedule_work()` interface method is forwarded to delegate. `Arc<dyn Dispatcher>` is
    /// constructed late to keep it out of the public interface to `WorkScheduler`.
    pub async fn schedule_work<'a>(
        &'a self,
        target_moniker: &AbsoluteMoniker,
        work_id: &'a str,
        work_request: &'a fsys::WorkRequest,
    ) -> Result<(), fcomponent::Error> {
        let mut delegate = self.delegate.lock().await;
        delegate.schedule_work(
            RealDispatcher::new(target_moniker.clone(), self.binder.clone()),
            work_id,
            work_request,
        )
    }

    /// `try_add_realm_as_worker()` interface method is forwarded to delegate.
    pub async fn try_add_realm_as_worker(
        &self,
        target_moniker: &AbsoluteMoniker,
        decl: &ComponentDecl,
    ) {
        let mut delegate = self.delegate.lock().await;
        delegate.try_add_realm_as_worker(target_moniker, decl).await;
    }

    /// `verify_worker_exposed_to_framework()` interface method is forwarded to delegate.
    pub async fn verify_worker_exposed_to_framework(&self, moniker: &AbsoluteMoniker) -> bool {
        let delegate = self.delegate.lock().await;
        delegate.verify_worker_exposed_to_framework(moniker)
    }

    /// `cancel_work()` interface method is forwarded to delegate. `Arc<dyn Dispatcher>` is
    /// constructed late to keep it out of the public interface to `WorkScheduler`.
    pub async fn cancel_work(
        &self,
        target_moniker: &AbsoluteMoniker,
        work_id: &str,
    ) -> Result<(), fcomponent::Error> {
        let mut delegate = self.delegate.lock().await;
        delegate
            .cancel_work(RealDispatcher::new(target_moniker.clone(), self.binder.clone()), work_id)
    }

    /// `get_batch_period()` interface method is forwarded to delegate.
    pub async fn get_batch_period(&self) -> Result<i64, fcomponent::Error> {
        let delegate = self.delegate.lock().await;
        delegate.get_batch_period()
    }

    /// `set_batch_period()` interface method is forwarded to delegate.
    pub async fn set_batch_period(&self, batch_period: i64) -> Result<(), fcomponent::Error> {
        let mut delegate = self.delegate.lock().await;
        delegate.set_batch_period(batch_period)
    }

    /// `dispatch_work()` helper method is forwarded to delegate, `weak_self` in injected to allow
    /// internal timer to asynchronously call back into `dispatch_work()`.
    pub(super) async fn dispatch_work(&self) {
        let mut delegate = self.delegate.lock().await;
        delegate.dispatch_work()
    }
}

#[cfg(test)]
use crate::work_scheduler::work_item::WorkItem;

// Provide test-only access to schedulable `WorkItem` instances.
#[cfg(test)]
impl WorkScheduler {
    pub(super) async fn work_items(&self) -> Vec<WorkItem> {
        let delegate = self.delegate.lock().await;
        delegate.work_items().clone()
    }
}

#[cfg(test)]
mod path_tests {
    use {
        super::{
            WORKER_CAPABILITY_NAME, WORK_SCHEDULER_CAPABILITY_NAME,
            WORK_SCHEDULER_CONTROL_CAPABILITY_NAME,
        },
        fidl::endpoints::ServiceMarker,
        fidl_fuchsia_sys2 as fsys,
    };

    #[test]
    fn work_scheduler_capability_paths() {
        assert_eq!(format!("{}", fsys::WorkerMarker::NAME), WORKER_CAPABILITY_NAME.to_string());
        assert_eq!(
            format!("{}", fsys::WorkSchedulerMarker::NAME),
            WORK_SCHEDULER_CAPABILITY_NAME.to_string()
        );
        assert_eq!(
            format!("{}", fsys::WorkSchedulerControlMarker::NAME),
            WORK_SCHEDULER_CONTROL_CAPABILITY_NAME.to_string()
        );
    }
}

#[cfg(test)]
mod time_tests {
    use {
        super::WorkScheduler,
        crate::{
            model::{binding::Binder, moniker::AbsoluteMoniker, testing::mocks::FakeBinder},
            work_scheduler::{
                dispatcher::{Dispatcher, Error},
                work_item::WorkItem,
            },
        },
        fidl_fuchsia_component as fcomponent, fidl_fuchsia_sys2 as fsys,
        fuchsia_async::{Executor, Time, WaitState},
        futures::{executor::block_on, future::BoxFuture, lock::Mutex, Future},
        std::sync::Arc,
    };

    #[derive(Clone)]
    struct DispatcherCount {
        item_count: usize,
        call_count: u32,
    }

    impl DispatcherCount {
        fn new() -> Self {
            Self { item_count: 0, call_count: 0 }
        }
    }

    struct CountingDispatcher {
        count: Mutex<DispatcherCount>,
        abs_moniker: AbsoluteMoniker,
    }

    impl CountingDispatcher {
        fn new(abs_moniker: &AbsoluteMoniker) -> Self {
            Self { count: Mutex::new(DispatcherCount::new()), abs_moniker: abs_moniker.clone() }
        }

        async fn count(&self) -> DispatcherCount {
            self.count.lock().await.clone()
        }

        async fn dispatch_async(&self, work_items: Vec<WorkItem>) -> Result<(), Error> {
            let mut count = self.count.lock().await;
            count.item_count = count.item_count + work_items.len();
            count.call_count = count.call_count + 1;
            Ok(())
        }
    }

    impl Dispatcher for CountingDispatcher {
        fn abs_moniker(&self) -> &AbsoluteMoniker {
            &self.abs_moniker
        }

        fn dispatch(&self, work_items: Vec<WorkItem>) -> BoxFuture<Result<(), Error>> {
            Box::pin(async move { self.dispatch_async(work_items).await })
        }
    }

    impl WorkScheduler {
        async fn schedule_work_item<'a>(
            &'a self,
            abs_moniker: &AbsoluteMoniker,
            work_id: &'a str,
            work_request: &'a fsys::WorkRequest,
        ) -> Result<(), fcomponent::Error> {
            let mut delegate = self.delegate.lock().await;
            delegate.schedule_work(Arc::new(abs_moniker.clone()), work_id, work_request)
        }

        async fn schedule_counted_work_item<'a>(
            &'a self,
            dispatcher: Arc<CountingDispatcher>,
            work_id: &'a str,
            work_request: &'a fsys::WorkRequest,
        ) -> Result<(), fcomponent::Error> {
            let mut delegate = self.delegate.lock().await;
            delegate.schedule_work(dispatcher, work_id, work_request)
        }
    }

    struct TestWorkUnit {
        start: i64,
        work_item: WorkItem,
    }

    impl TestWorkUnit {
        fn new(
            start: i64,
            abs_moniker: &AbsoluteMoniker,
            id: &str,
            next_deadline_monotonic: i64,
            period: Option<i64>,
        ) -> Self {
            TestWorkUnit {
                start,
                work_item: WorkItem::new(
                    Arc::new(abs_moniker.clone()),
                    id,
                    next_deadline_monotonic,
                    period,
                ),
            }
        }
    }

    struct TimeTest {
        executor: Executor,
        work_scheduler: Arc<WorkScheduler>,
        // Retain `Arc` to keep `Binder` alive throughout test.
        _binder: Arc<dyn Binder>,
    }

    impl TimeTest {
        fn new() -> Self {
            let executor = Executor::new_with_fake_time().unwrap();
            executor.set_fake_time(Time::from_nanos(0));
            let binder = FakeBinder::new();
            let work_scheduler = WorkScheduler::new_raw(binder.clone());
            block_on(async {
                let mut delegate = work_scheduler.delegate.lock().await;
                delegate.init(Arc::downgrade(&work_scheduler));
            });
            TimeTest { executor, work_scheduler, _binder: binder }
        }

        fn work_scheduler(&self) -> Arc<WorkScheduler> {
            self.work_scheduler.clone()
        }

        fn set_time(&mut self, time: i64) {
            self.executor.set_fake_time(Time::from_nanos(time));
        }

        fn run_and_sync<F>(&mut self, fut: &mut F)
        where
            F: Future + Unpin,
        {
            assert!(self.executor.run_until_stalled(fut).is_ready());
            while self.executor.is_waiting() == WaitState::Ready {
                assert!(self.executor.run_until_stalled(&mut Box::pin(async {})).is_ready());
            }
        }

        fn set_time_and_run_timers(&mut self, time: i64) {
            self.set_time(time);
            assert!(self.executor.wake_expired_timers());
            while self.executor.is_waiting() == WaitState::Ready {
                assert!(self.executor.run_until_stalled(&mut Box::pin(async {})).is_ready());
            }
        }

        fn assert_no_timers(&mut self) {
            assert_eq!(None, self.executor.wake_next_timer());
        }

        fn assert_next_timer_at(&mut self, time: i64) {
            assert_eq!(WaitState::Waiting(Time::from_nanos(time)), self.executor.is_waiting());
        }

        fn assert_work_items(
            &mut self,
            work_scheduler: &Arc<WorkScheduler>,
            test_work_units: Vec<TestWorkUnit>,
        ) {
            self.run_and_sync(&mut Box::pin(async {
                // Check collection of work items.
                let work_items: Vec<WorkItem> = test_work_units
                    .iter()
                    .map(|test_work_unit| test_work_unit.work_item.clone())
                    .collect();
                assert_eq!(work_items, work_scheduler.work_items().await);

                // Check invariants on relationships between `now` and `WorkItem` state.
                let now = Time::now().into_nanos();
                for test_work_unit in test_work_units.iter() {
                    let work_item = &test_work_unit.work_item;
                    let deadline = work_item.next_deadline_monotonic;
                    // Either:
                    // 1. This is a check for initial state, in which case allow now=deadline=0, or
                    // 2. All deadlines should be in the future.
                    assert!(
                        (now == 0 && deadline == now) || now < deadline,
                        "Expected either
                            1. This is a check for initial state, so allow now=deadline=0, or
                            2. All deadlines should be in the future."
                    );
                    if let Some(period) = work_item.period {
                        // All periodic deadlines should be either:
                        // 1. Waiting to be dispatched for the first time, or
                        // 2. At most one period into the future (for otherwise, a period would be
                        //    skipped).
                        assert!(
                            now < test_work_unit.start || now + period >= deadline,
                            "Expected all periodic deadlines should be either:
                                1. Waiting to be dispatched for the first time, or
                                2. At most one period into the future (for otherwise, a period would
                                    be skipped"
                        );
                        // All periodic deadlines should be aligned to:
                        // `deadline = start + n*period` for some non-negative integer, `n`.
                        assert_eq!(
                            0,
                            (deadline - test_work_unit.start) % period,
                            "Expected all periodic deadlines should be aligned to:
                                `deadline = start + n*period` for some non-negative integer, `n`."
                        );
                    }
                }
            }));
        }

        fn assert_no_work(&mut self, work_scheduler: &Arc<WorkScheduler>) {
            self.run_and_sync(&mut Box::pin(async {
                assert_eq!(vec![] as Vec<WorkItem>, work_scheduler.work_items().await);
            }));
        }
    }

    #[test]
    fn work_scheduler_time_get_batch_period_queues_nothing() {
        let mut t = TimeTest::new();
        let work_scheduler = t.work_scheduler();
        t.run_and_sync(&mut Box::pin(async {
            assert_eq!(Ok(std::i64::MAX), work_scheduler.get_batch_period().await);
        }));
        t.assert_no_timers();
    }

    #[test]
    fn work_scheduler_time_set_batch_period_no_work_queues_nothing() {
        let mut t = TimeTest::new();
        let work_scheduler = t.work_scheduler();
        t.run_and_sync(&mut Box::pin(async {
            assert_eq!(Ok(()), work_scheduler.set_batch_period(1).await);
        }));
        t.assert_no_timers();
    }

    #[test]
    fn work_scheduler_time_schedule_inf_batch_period_queues_nothing() {
        let mut t = TimeTest::new();
        let work_scheduler = t.work_scheduler();
        t.run_and_sync(&mut Box::pin(async {
            let root = AbsoluteMoniker::root();
            let now_once =
                fsys::WorkRequest { start: Some(fsys::Start::MonotonicTime(0)), period: None };
            assert_eq!(
                Ok(()),
                work_scheduler.schedule_work_item(&root, "NOW_ONCE", &now_once).await
            );
        }));
        t.assert_no_timers();
    }

    #[test]
    fn work_scheduler_time_schedule_finite_batch_period_queues_and_dispatches() {
        let mut t = TimeTest::new();
        let work_scheduler = t.work_scheduler();
        let root = AbsoluteMoniker::root();

        // Set batch period and queue a unit of work.
        t.run_and_sync(&mut Box::pin(async {
            assert_eq!(Ok(()), work_scheduler.set_batch_period(1).await);
            let now_once =
                fsys::WorkRequest { start: Some(fsys::Start::MonotonicTime(0)), period: None };
            assert_eq!(
                Ok(()),
                work_scheduler.schedule_work_item(&root, "NOW_ONCE", &now_once).await
            );
        }));

        // Confirm timer and work item.
        t.assert_next_timer_at(1);
        t.assert_work_items(
            &work_scheduler,
            vec![TestWorkUnit::new(0, &root, "NOW_ONCE", 0, None)],
        );

        // Run work stemming from timer and confirm no more work items.
        t.set_time_and_run_timers(1);
        t.assert_no_work(&work_scheduler);
    }

    #[test]
    fn work_scheduler_time_periodic_stays_queued() {
        let mut t = TimeTest::new();
        let work_scheduler = t.work_scheduler();
        let root = AbsoluteMoniker::root();

        // Set batch period and queue a unit of work.
        t.run_and_sync(&mut Box::pin(async {
            assert_eq!(Ok(()), work_scheduler.set_batch_period(1).await);
            let every_moment =
                fsys::WorkRequest { start: Some(fsys::Start::MonotonicTime(0)), period: Some(1) };
            assert_eq!(
                Ok(()),
                work_scheduler.schedule_work_item(&root, "EVERY_MOMENT", &every_moment).await
            );
        }));

        // Confirm timer and work item.
        t.assert_next_timer_at(1);
        t.assert_work_items(
            &work_scheduler,
            vec![TestWorkUnit::new(0, &root, "EVERY_MOMENT", 0, Some(1))],
        );

        // Dispatch work and assert next periodic work item and timer.
        t.set_time_and_run_timers(1);
        t.assert_work_items(
            &work_scheduler,
            vec![TestWorkUnit::new(0, &root, "EVERY_MOMENT", 2, Some(1))],
        );
        t.assert_next_timer_at(3);
    }

    #[test]
    fn work_scheduler_time_timeout_updates_when_earlier_work_item_added() {
        let mut t = TimeTest::new();
        let work_scheduler = t.work_scheduler();
        let root = AbsoluteMoniker::root();

        // Set batch period and queue a unit of work.
        t.run_and_sync(&mut Box::pin(async {
            assert_eq!(Ok(()), work_scheduler.set_batch_period(5).await);
            let at_nine =
                fsys::WorkRequest { start: Some(fsys::Start::MonotonicTime(9)), period: None };
            assert_eq!(Ok(()), work_scheduler.schedule_work_item(&root, "AT_NINE", &at_nine).await);
        }));

        // Confirm timer and work item.
        t.assert_next_timer_at(10);
        t.assert_work_items(&work_scheduler, vec![TestWorkUnit::new(9, &root, "AT_NINE", 9, None)]);

        // Queue unit of work with deadline _earlier_ than first unit of work.
        t.run_and_sync(&mut Box::pin(async {
            let at_four =
                fsys::WorkRequest { start: Some(fsys::Start::MonotonicTime(4)), period: None };
            assert_eq!(Ok(()), work_scheduler.schedule_work_item(&root, "AT_FOUR", &at_four).await);
        }));

        // Confirm timer moved _back_, and work units are as expected.
        t.assert_next_timer_at(5);
        t.assert_work_items(
            &work_scheduler,
            vec![
                TestWorkUnit::new(4, &root, "AT_FOUR", 4, None),
                TestWorkUnit::new(9, &root, "AT_NINE", 9, None),
            ],
        );

        // Dispatch work and assert remaining work and timer.
        t.set_time_and_run_timers(5);
        t.assert_work_items(&work_scheduler, vec![TestWorkUnit::new(9, &root, "AT_NINE", 9, None)]);
        t.assert_next_timer_at(10);

        // Queue unit of work with deadline _later_ than existing unit of work.
        t.run_and_sync(&mut Box::pin(async {
            let at_ten =
                fsys::WorkRequest { start: Some(fsys::Start::MonotonicTime(10)), period: None };
            assert_eq!(Ok(()), work_scheduler.schedule_work_item(&root, "AT_TEN", &at_ten).await);
        }));

        // Confirm unchanged, and work units are as expected.
        t.assert_next_timer_at(10);
        t.assert_work_items(
            &work_scheduler,
            vec![
                TestWorkUnit::new(9, &root, "AT_NINE", 9, None),
                TestWorkUnit::new(10, &root, "AT_TEN", 10, None),
            ],
        );

        // Dispatch work and assert no work left.
        t.set_time_and_run_timers(10);
        t.assert_no_work(&work_scheduler);
    }

    #[test]
    fn work_scheduler_time_late_timer_fire() {
        let mut t = TimeTest::new();
        let work_scheduler = t.work_scheduler();
        let root = AbsoluteMoniker::root();

        // Set period and schedule two work items, one of which _should_ be dispatched in a second
        // cycle.
        t.run_and_sync(&mut Box::pin(async {
            assert_eq!(Ok(()), work_scheduler.set_batch_period(5).await);
            let at_four =
                fsys::WorkRequest { start: Some(fsys::Start::MonotonicTime(4)), period: None };
            assert_eq!(Ok(()), work_scheduler.schedule_work_item(&root, "AT_FOUR", &at_four).await);
            let at_nine =
                fsys::WorkRequest { start: Some(fsys::Start::MonotonicTime(9)), period: None };
            assert_eq!(Ok(()), work_scheduler.schedule_work_item(&root, "AT_NINE", &at_nine).await);
        }));

        // Confirm timer and work items.
        t.assert_next_timer_at(5);
        t.assert_work_items(
            &work_scheduler,
            vec![
                TestWorkUnit::new(4, &root, "AT_FOUR", 4, None),
                TestWorkUnit::new(9, &root, "AT_NINE", 9, None),
            ],
        );

        // Simulate delayed dispatch: System load or some other factor caused dispatch of work to be
        // delayed beyond the deadline of _both_ units of work.
        t.set_time_and_run_timers(16);

        // Confirm timers and dispatched units.
        t.assert_no_timers();
        t.assert_no_work(&work_scheduler);
    }

    #[test]
    fn work_scheduler_time_late_timer_fire_periodic_work_item() {
        let mut t = TimeTest::new();
        let work_scheduler = t.work_scheduler();
        let root = AbsoluteMoniker::root();

        // Set period and schedule two work items, one of which _should_ be dispatched in a second
        // cycle.
        t.run_and_sync(&mut Box::pin(async {
            assert_eq!(Ok(()), work_scheduler.set_batch_period(5).await);
            let at_four =
                fsys::WorkRequest { start: Some(fsys::Start::MonotonicTime(4)), period: None };
            assert_eq!(Ok(()), work_scheduler.schedule_work_item(&root, "AT_FOUR", &at_four).await);
            let at_nine_periodic =
                fsys::WorkRequest { start: Some(fsys::Start::MonotonicTime(9)), period: Some(5) };
            assert_eq!(
                Ok(()),
                work_scheduler
                    .schedule_work_item(&root, "AT_NINE_PERIODIC_FIVE", &at_nine_periodic)
                    .await
            );
        }));

        // Confirm timer and work items.
        t.assert_next_timer_at(5);
        t.assert_work_items(
            &work_scheduler,
            vec![
                TestWorkUnit::new(4, &root, "AT_FOUR", 4, None),
                TestWorkUnit::new(9, &root, "AT_NINE_PERIODIC_FIVE", 9, Some(5)),
            ],
        );

        // Simulate _seriously_ delayed dispatch: System load or some other
        // factor caused dispatch of work to be delayed _way_ beyond the
        // deadline of _both_ units of work.
        t.set_time_and_run_timers(116);

        // Confirm timer set to next batch period, and periodic work item still queued.
        t.assert_next_timer_at(120);
        t.assert_work_items(
            &work_scheduler,
            // Time:
            //   now=116
            // WorkItem:
            //  start=9
            //  period=5
            //
            // Updated WorkItem.period should be:
            //   WorkItem.next_deadline_monotonic = 9 + 5*n
            //     where
            //       Time.now < WorkItem.next_deadline_monotonic
            //       and
            //       Time.now + WorkItem.period > WorkItem.next_deadline_monotonic
            //
            // WorkItem.next_deadline_monotonic = 119 = 9 + (22 * 5).
            vec![TestWorkUnit::new(9, &root, "AT_NINE_PERIODIC_FIVE", 119, Some(5))],
        );
    }

    #[test]
    fn work_scheduler_time_grouped_dispatch() {
        let mut t = TimeTest::new();
        let work_scheduler = t.work_scheduler();
        let root = AbsoluteMoniker::root();
        let root_dispatcher = Arc::new(CountingDispatcher::new(&root));
        let child = root.child("child:0".into());
        let child_dispatcher = Arc::new(CountingDispatcher::new(&child));

        // Set period and schedule four work items:
        // - Three work items from two components to run in the first period, and
        // - One work item to remain after first period.
        t.run_and_sync(&mut Box::pin(async {
            assert_eq!(Ok(()), work_scheduler.set_batch_period(3).await);
            let root_0 =
                fsys::WorkRequest { start: Some(fsys::Start::MonotonicTime(0)), period: None };
            assert_eq!(
                Ok(()),
                work_scheduler
                    .schedule_counted_work_item(root_dispatcher.clone(), "ROOT_0", &root_0)
                    .await
            );
            let root_1 =
                fsys::WorkRequest { start: Some(fsys::Start::MonotonicTime(1)), period: None };
            assert_eq!(
                Ok(()),
                work_scheduler
                    .schedule_counted_work_item(root_dispatcher.clone(), "ROOT_1", &root_1)
                    .await
            );
            let child_2 =
                fsys::WorkRequest { start: Some(fsys::Start::MonotonicTime(2)), period: None };
            assert_eq!(
                Ok(()),
                work_scheduler
                    .schedule_counted_work_item(child_dispatcher.clone(), "CHILD_2", &child_2)
                    .await
            );
            let child_5 =
                fsys::WorkRequest { start: Some(fsys::Start::MonotonicTime(5)), period: None };
            assert_eq!(
                Ok(()),
                work_scheduler
                    .schedule_counted_work_item(child_dispatcher.clone(), "CHILD_5", &child_5)
                    .await
            );
        }));

        // Confirm timer and work items.
        t.assert_next_timer_at(3);
        t.assert_work_items(
            &work_scheduler,
            vec![
                TestWorkUnit::new(0, &root, "ROOT_0", 0, None),
                TestWorkUnit::new(1, &root, "ROOT_1", 1, None),
                TestWorkUnit::new(2, &child, "CHILD_2", 2, None),
                TestWorkUnit::new(5, &child, "CHILD_5", 5, None),
            ],
        );

        // Complete first period.
        t.set_time_and_run_timers(3);

        // Confirm timer set to next batch period, future item still queued, and one `dispatch()`
        // call on each dispatcher.
        t.assert_next_timer_at(6);
        t.assert_work_items(
            &work_scheduler,
            vec![TestWorkUnit::new(5, &child, "CHILD_5", 5, None)],
        );
        t.run_and_sync(&mut Box::pin(async {
            let root_count = root_dispatcher.count().await;
            let child_count = child_dispatcher.count().await;
            assert_eq!(1, root_count.call_count);
            assert_eq!(2, root_count.item_count);
            assert_eq!(1, child_count.call_count);
            assert_eq!(1, child_count.item_count);
        }));
    }
}

#[cfg(test)]
mod connect_tests {
    use {
        super::{WorkScheduler, WORK_SCHEDULER_CONTROL_CAPABILITY_NAME},
        crate::{
            capability::{CapabilitySource, InternalCapability},
            model::{
                hooks::{Event, EventPayload, Hooks},
                moniker::AbsoluteMoniker,
                testing::mocks::FakeBinder,
            },
        },
        anyhow::Error,
        cm_rust::CapabilityNameOrPath,
        fidl::endpoints::ClientEnd,
        fidl_fuchsia_sys2::WorkSchedulerControlMarker,
        fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::lock::Mutex,
        std::{path::PathBuf, sync::Arc},
    };

    #[fasync::run_singlethreaded(test)]
    async fn connect_to_work_scheduler_control_service() -> Result<(), Error> {
        // Retain `Arc` to keep `Binder` alive throughout test.
        let binder = FakeBinder::new();

        let work_scheduler = WorkScheduler::new(binder).await;
        let hooks = Hooks::new(None);
        hooks.install(work_scheduler.hooks()).await;

        let capability_provider = Arc::new(Mutex::new(None));
        let source = CapabilitySource::Builtin {
            capability: InternalCapability::Protocol(CapabilityNameOrPath::Name(
                WORK_SCHEDULER_CONTROL_CAPABILITY_NAME.clone(),
            )),
        };

        let (client, mut server) = zx::Channel::create()?;

        let event = Event::new_for_test(
            AbsoluteMoniker::root(),
            "fuchsia-pkg://root",
            Ok(EventPayload::CapabilityRouted {
                source,
                capability_provider: capability_provider.clone(),
            }),
        );
        hooks.dispatch(&event).await?;

        let capability_provider = capability_provider.lock().await.take();
        if let Some(capability_provider) = capability_provider {
            capability_provider.open(0, 0, PathBuf::new(), &mut server).await?;
        }

        let work_scheduler_control = ClientEnd::<WorkSchedulerControlMarker>::new(client)
            .into_proxy()
            .expect("failed to create launcher proxy");
        let result = work_scheduler_control.get_batch_period().await;
        result
            .expect("failed to use WorkSchedulerControl service")
            .expect("WorkSchedulerControl.GetBatchPeriod() yielded error");
        Ok(())
    }
}
