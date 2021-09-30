// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Job Source Support
//!
//! # Summary
//!
//! The source mod contains components for providing [Jobs](Job) to Job manager (most likely
//! [manager](job::manager::Manager)). In a typical workflow, client code will create a [Seeder],
//! which is used to send [Job](Job) streams to a manager via the
//! [MessageHub](crate::message::message_hub::MessageHub). The [Seeder] can send any stream where
//! the data implements [Into<Job>]. Once the source is received, the manager can assign a unique
//! [Id] to the source, using [IdGenerator]. The manager can then use a [Handler] to handle the
//!  produced [Jobs](Job) and their results.

use crate::clock::now;
use crate::job::{self, execution, Job, Payload, StoreHandleMapping};
use crate::message::base::{Audience, MessengerType};
use crate::service::message::{Delegate, Messenger, Signature};
use crate::trace::TracingNonce;
use crate::trace_guard;

use core::pin::Pin;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::lock::Mutex;
use futures::Stream;
use futures::StreamExt;
use std::collections::{HashMap, VecDeque};
use std::convert::{Infallible, TryFrom};
use std::sync::Arc;
use thiserror::Error as ThisError;

#[derive(Clone)]
/// [Seeder] properly packages and sends [Job] source streams to a [Job] manager.
pub struct Seeder {
    /// A [Messenger](crate::message::messenger::MessengerClient) to send Payloads to the manager.
    messenger: Messenger,
    /// The [Signature](crate::message::base::Signature) of the manager to receive the source
    /// Payloads.
    manager_signature: Signature,
}

impl Seeder {
    pub(crate) async fn new(delegate: &Delegate, manager_signature: Signature) -> Self {
        Self {
            messenger: delegate
                .create(MessengerType::Unbound)
                .await
                .expect("should create messenger")
                .0,
            manager_signature,
        }
    }

    // TODO(fxbug.dev/78962) Ensure we also track a control_handle in case we need
    // to send an epitaph back across the stream without a responder.
    pub(crate) fn seed<J, E, E2, T>(&self, source: T)
    where
        Job: TryFrom<J, Error = E2>,
        Error: From<E> + From<E2>,
        T: Stream<Item = Result<J, E>> + Send + 'static,
    {
        // Convert the incoming stream into the expected types for a Job source.
        let mapped_stream: Pin<Box<dyn Stream<Item = Result<Job, Error>> + Send>> = source
            .map(|result| {
                result
                    // First convert the error type from the result so we can be compatible
                    // with conversions done with try_from below.
                    .map_err(Error::from)
                    // Then map the job. Ideally try_from will return `Error` directly, but we
                    // also need to handle the `Infallible` type. It should compile to a no-op,
                    // but the types still need to align.
                    .and_then(|j| Job::try_from(j).map_err(Error::from))
            })
            .boxed();

        // Send the source stream to the manager.
        self.messenger
            .message(
                Payload::Source(Arc::new(Mutex::new(Some(mapped_stream)))).into(),
                Audience::Messenger(self.manager_signature),
            )
            .send()
            .ack();
    }
}

/// The types of errors for [Jobs](Job). This is a single, unified set over all Job source
/// related-errors. This enumeration should be expanded to capture any future error variant.
#[derive(ThisError)]
pub enum Error {
    #[error("Unknown error")]
    Unknown,
    #[error("Invalid input")]
    InvalidInput(Box<dyn ErrorResponder + Send>),
    #[error("Unsupported API call")]
    Unsupported,
}

impl std::fmt::Debug for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Error::Unknown => f.write_str("Unknown"),
            Error::InvalidInput(_) => f.write_str("InvalidInput(..)"),
            Error::Unsupported => f.write_str("Unsupported"),
        }
    }
}

/// Abstract over how to respond with a settings fidl error.
pub trait ErrorResponder {
    /// Unique identifier for the API this responder is responsible for.
    fn id(&self) -> &'static str;

    /// Respond with the supplied error. Returns any fidl errors that occur when
    /// trying to send the response.
    fn respond(self: Box<Self>, error: fidl_fuchsia_settings::Error) -> Result<(), fidl::Error>;
}

// This implementation is necessary when converting into a Job is infallible. This can happen if an
// input to a job has no possible way to fail, or in tests when the streams a populated with Jobs
// directly. This is used by the Seeder::seed fn above.
impl From<Infallible> for Error {
    fn from(_: Infallible) -> Self {
        unreachable!()
    }
}

impl From<fidl::Error> for Error {
    fn from(_item: fidl::Error) -> Self {
        Error::Unknown
    }
}

#[derive(Copy, Clone, Debug, PartialEq)]
/// The current state of the source. This is used by the managing entity to understand how to handle
/// pending and completed [Jobs](Job) from a source.
pub(super) enum State {
    /// The source is still available to produce new [Jobs](Job).
    Active,
    /// Completion has been requested, but [Jobs](Job) must complete before the source is considered
    /// done.
    PendingCompletion,
    /// The source is no longer producing new [Jobs](Job).
    Completed,
}

/// [Id] provides a unique identifier for a source within its parent space, most often a manager.
// TODO(fxbug.dev/73541): Explore using generational indices instead.
#[derive(Copy, Clone, Debug, PartialEq, Eq, Hash)]
pub(super) struct Id {
    _identifier: usize,
}

impl Id {
    fn new(identifier: usize) -> Self {
        Self { _identifier: identifier }
    }
}

/// [IdGenerator] creates unique [Ids] to be associated with a source. This uniqueness is
/// guaranteed for [Ids] generated by the same [IdGenerator].
///
/// [Ids]: Id
pub(super) struct IdGenerator {
    next_identifier: usize,
}

impl IdGenerator {
    pub(super) fn new() -> Self {
        Self { next_identifier: 0 }
    }

    pub(super) fn generate(&mut self) -> Id {
        let return_id = Id::new(self.next_identifier);
        self.next_identifier += 1;

        return_id
    }
}

/// [Handler] handles [Jobs](Job) within the scope of a single scope. It determines what [Job](Job)
/// should be executed (if any). This responsibility includes managing any queueing that might be
/// necessary based on the [Job] type.
pub(super) struct Handler {
    /// A [IdGenerator](job::IdGenerator) to assign unique ids to incoming jobs.
    job_id_generator: job::IdGenerator,
    /// A mapping from [execution types](execution::Type) to [groups](execution::Group). Each entry
    /// enables tracking across [Jobs](Job) in the same group, such as storing persistent values.
    /// The mapping is also consulted finding the next [Jobs](Job) to execute.
    jobs: HashMap<execution::Type, execution::Group>,
    /// A list of states. The element represents the most current [State]. We keep track of seen
    /// states to allow post analysis, such as source duration.
    states: VecDeque<(State, zx::Time)>,
    /// This [HashMap] associates a given [Job] [Signature] with a [Data](job::data::Data) mapping.
    /// [Signature] is used over [execution::Type] to allow storage to be shared across groups of
    /// different [types](execution::Type) that share the same [Signature].
    stores: StoreHandleMapping,
}

impl Handler {
    pub(crate) fn new() -> Self {
        let mut handler = Self {
            job_id_generator: job::IdGenerator::new(),
            jobs: HashMap::new(),
            states: VecDeque::new(),
            stores: HashMap::new(),
        };

        handler.set_state(State::Active);

        handler
    }

    /// Marks the source as completed.
    pub(crate) fn complete(&mut self) {
        self.set_state(if self.is_active() { State::PendingCompletion } else { State::Completed });
    }

    /// Returns whether the source has completed.
    pub(crate) fn is_completed(&mut self) -> bool {
        matches!(self.states.back(), Some(&(State::Completed, _)))
    }

    fn set_state(&mut self, state: State) {
        // State should not be set after the source has been completed.
        assert!(!self.is_completed());

        // Do not try to set the state if it matches the last updated state.
        if matches!(self.states.back(), Some(&(x,_)) if x == state) {
            return;
        }

        self.states.push_back((state, now()));
    }

    /// Returns true if any job is executed, false otherwise.
    pub(crate) async fn execute_next<F: Fn(job::Info, job::execution::Details) + Send + 'static>(
        &mut self,
        delegate: &mut Delegate,
        callback: F,
        nonce: TracingNonce,
    ) -> bool {
        for execution_group in self.jobs.values_mut() {
            // If there are no jobs ready to become active, move to next group.
            if let Some(job_info) = execution_group.promote_next_to_active() {
                let guard = trace_guard!(nonce, "prepare_execution");
                let execution =
                    job_info.prepare_execution(delegate, &mut self.stores, callback).await;
                drop(guard);

                fasync::Task::spawn(execution).detach();
                return true;
            }
        }

        false
    }

    /// Returns whether the source is active, defined as having at least one [Job] which is
    /// currently active (running, not pending).
    pub(crate) fn is_active(&self) -> bool {
        self.jobs.iter().any(|(_, group)| group.is_active())
    }

    /// Adds a [Job] to be handled by this [Handler].
    pub(crate) fn add_pending_job(&mut self, incoming_job: Job) {
        let job_info = job::Info::new(self.job_id_generator.generate(), incoming_job);
        let execution_type = job_info.get_execution_type().clone();

        // Execution groups are based on matching execution::Type.
        let execution_group = self
            .jobs
            .entry(execution_type.clone())
            .or_insert_with(move || execution::Group::new(execution_type));
        execution_group.add(job_info);
    }

    /// Informs the [Handler] that a [Job] by the given [Id](job::Id) has completed.
    pub(crate) fn handle_job_completion(&mut self, job: job::Info) {
        self.jobs.get_mut(job.get_execution_type()).expect("group should be present").complete(job);

        // When a source end is detected, the managing entity will try to complete the source. If
        // there is active work, the source completion will be deferred. It is the source's
        // responsibility after each subsequent completion to check whether completion can now
        // proceed.
        if matches!(self.states.back(), Some(&(State::PendingCompletion, _))) {
            self.complete();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::job::execution;
    use crate::message::MessageHubUtil;
    use crate::service::{test, MessageHub};
    use crate::tests::scaffold::workload::{Sequential, StubWorkload, Workload};
    use rand::Rng;

    use futures::FutureExt;
    use matches::assert_matches;

    #[test]
    fn test_id_generation() {
        let mut generator = IdGenerator::new();
        // Ensure generator is creating unique ids
        assert!(generator.generate() != generator.generate());
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_seeding() {
        // Create delegate for communication between components.
        let message_hub_delegate = MessageHub::create_hub();

        // Create a top-level receptor to receive sources.
        let mut receptor = message_hub_delegate
            .create(MessengerType::Unbound)
            .await
            .expect("should create receptor")
            .1;

        // Create seeder.
        let seeder = Seeder::new(&message_hub_delegate, receptor.get_signature()).await;

        let job_stream = async {
            Ok(Job::new(job::work::Load::Independent(StubWorkload::new()))) as Result<Job, Error>
        }
        .into_stream();

        seeder.seed(job_stream);

        assert_matches!(receptor.next_of::<Payload>().await, Ok((Payload::Source(_), _)));
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_handling() {
        // Create delegate for communication between components.
        let mut message_hub_delegate = MessageHub::create_hub();

        let results: Vec<i64> = (0..10).collect();

        // Create a top-level receptor to receive job results from.
        let mut receptor = message_hub_delegate
            .create(MessengerType::Unbound)
            .await
            .expect("should create receptor")
            .1;

        let mut handler = Handler::new();

        assert!(!handler.execute_next(&mut message_hub_delegate, |_, _| {}, 0).await);

        for result in &results {
            handler.add_pending_job(Job::new(job::work::Load::Independent(Workload::new(
                test::Payload::Integer(*result),
                receptor.get_signature(),
            ))));
        }

        for result in results {
            let (execution_tx, mut execution_rx) = futures::channel::mpsc::unbounded::<job::Info>();

            // Execute job.
            assert!(
                handler
                    .execute_next(
                        &mut message_hub_delegate,
                        move |job, _| {
                            execution_tx.unbounded_send(job).expect("send should succeed");
                        },
                        0
                    )
                    .await
            );

            // Confirm received value matches the value sent from workload.
            let test::Payload::Integer(value) =
                receptor.next_of::<test::Payload>().await.expect("should have payload").0;
            assert_eq!(value, result);

            handler
                .handle_job_completion(execution_rx.next().await.expect("should have gotten job"));
        }
    }

    // Ensures that proper queueing happens amongst Jobs within Execution Groups.
    #[fuchsia_async::run_until_stalled(test)]
    async fn test_execution_order() {
        let (execution_tx, mut execution_rx) = futures::channel::mpsc::unbounded::<job::Info>();

        // Create delegate for communication between components.
        let mut message_hub_delegate = MessageHub::create_hub();

        let mut handler = Handler::new();

        // Create a top-level receptor to receive job results from.
        let mut receptor = message_hub_delegate
            .create(MessengerType::Unbound)
            .await
            .expect("should create receptor")
            .1;

        // Create 2 jobs of the same sequential type.
        let results: Vec<i64> = (0..=1).collect();

        assert!(!handler.execute_next(&mut message_hub_delegate, |_, _| {}, 0).await);

        for result in &results {
            handler.add_pending_job(Job::new(job::work::Load::Sequential(
                Workload::new(test::Payload::Integer(*result), receptor.get_signature()),
                job::Signature::new::<usize>(),
            )));
        }

        // Execute first job, ensuring handler has a job to execute.
        {
            let execution_tx = execution_tx.clone();
            assert!(
                handler
                    .execute_next(
                        &mut message_hub_delegate,
                        move |job, _| {
                            execution_tx.unbounded_send(job).expect("send should succeed");
                        },
                        0
                    )
                    .await
            );
        }

        // Verify we receive result back for the first job.
        assert_eq!(
            test::Payload::Integer(0),
            receptor.next_of::<test::Payload>().await.expect("should have payload").0
        );

        // Capture first completed job, do not handle yet.
        let first_job_info = execution_rx.next().await.expect("should have gotten job");

        // Ensure no job is ready to execute.
        assert!(!handler.execute_next(&mut message_hub_delegate, move |_, _| {}, 0).await);

        // Add an independent job.
        handler.add_pending_job(Job::new(job::work::Load::Independent(StubWorkload::new())));

        // Execute independent job.
        {
            let execution_tx = execution_tx.clone();
            // Execute next job and ensure that the response max
            assert!(
                handler
                    .execute_next(
                        &mut message_hub_delegate,
                        move |job, _| {
                            execution_tx.unbounded_send(job).expect("send should succeed");
                        },
                        0
                    )
                    .await
            );
        }

        let independent_job_info = execution_rx.next().await.expect("should have gotten job");
        assert_matches!(*independent_job_info.get_execution_type(), execution::Type::Independent);

        // Handle independent job completion.
        handler.handle_job_completion(independent_job_info);

        // Handle first job completion.
        handler.handle_job_completion(first_job_info);

        {
            let execution_tx = execution_tx.clone();
            // Execute next job. Assert job is ready to execute
            assert!(
                handler
                    .execute_next(
                        &mut message_hub_delegate,
                        move |job, _| {
                            execution_tx.unbounded_send(job).expect("send should succeed");
                        },
                        0
                    )
                    .await
            );
        }

        // Verify we receive result from the second job back.
        assert_eq!(
            test::Payload::Integer(1),
            receptor.next_of::<test::Payload>().await.expect("should have payload").0
        );
    }

    // Ensures that proper queueing happens amongst Jobs within Execution Groups.
    #[fuchsia_async::run_until_stalled(test)]
    async fn test_data() {
        let mut rng = rand::thread_rng();

        let (result_tx, mut result_rx) = futures::channel::mpsc::unbounded::<usize>();

        // Create delegate for communication between components.
        let mut message_hub_delegate = MessageHub::create_hub();

        let mut handler = Handler::new();

        let data_key = job::data::Key::TestInteger(rng.gen());
        let initial_value = rng.gen_range(0, 9);
        let signature = job::Signature::new::<usize>();

        // Each result is the square of the previous result,
        let results: Vec<usize> = (0..5)
            .map(move |val| {
                let mut return_value: usize = initial_value;

                for _ in 0..val {
                    return_value = return_value.pow(2);
                }

                return_value
            })
            .collect();

        for _ in &results {
            let data_key = data_key.clone();
            let result_tx = result_tx.clone();

            // Add a job that writes the initial value and reads it back.
            handler.add_pending_job(Job::new(job::work::Load::Sequential(
                Sequential::boxed(move |_, store| {
                    let result_tx = result_tx.clone();
                    let data_key = data_key.clone();

                    Box::pin(async move {
                        let mut storage_lock = store.lock().await;
                        let new_value = if let Some(job::data::Data::TestData(value)) =
                            storage_lock.get(&data_key)
                        {
                            value.pow(2)
                        } else {
                            initial_value
                        };

                        // Store value.
                        storage_lock.insert(data_key, job::data::Data::TestData(new_value));

                        // Relay value back.
                        result_tx.unbounded_send(new_value).expect("should send");
                    })
                }),
                signature,
            )));
        }

        for value in results {
            let (completion_tx, mut completion_rx) =
                futures::channel::mpsc::unbounded::<job::Info>();

            // Execute next job.
            assert!(
                handler
                    .execute_next(
                        &mut message_hub_delegate,
                        move |job, _| {
                            completion_tx.unbounded_send(job).expect("should send job");
                        },
                        0
                    )
                    .await
            );

            // Ensure the returned value matches the calculation
            assert_eq!(value, result_rx.next().await.expect("value should be returned"));
            handler.handle_job_completion(completion_rx.next().await.expect("should receive job"));
        }
    }
}
