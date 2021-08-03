// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Job Handling Support
//!
//! # Summary
//!
//! [Jobs] are basic units of work that interfaces in the setting service can specify. In addition
//! to the workload, [Job] definitions also capture information about how the work should be
//! handled. For example, a Job can specify that it would like to run in sequence within a set of
//! similar [Jobs](Job). This behavior is captured by the [Job's](Job) [ExecutionType].
//!
//! Sources are streams that provide jobs from a given source. The lifetime of (Jobs)[Job] produced
//! by a source are bound to the source's lifetime. The end of a source stream will lead to any
//! in-flight and pending jobs being cancelled.
//!
//! Job Manager is responsible for managing sources and the jobs they produce. The manager
//! associates and maintains any supporting data for jobs, such as caches.
use crate::clock::now;
use crate::payload_convert;
use crate::service::message;
use crate::trace;

use core::fmt::{Debug, Formatter};
use core::pin::Pin;
use futures::future::BoxFuture;
use futures::lock::Mutex;
use futures::stream::Stream;
use std::collections::HashMap;
use std::sync::Arc;

pub mod manager;
pub mod source;

payload_convert!(Job, Payload);

/// [StoreHandleMapping] represents the mapping from a [Job's](Job) [Signature] to the [data::Data]
/// store. This store is shared by all [Jobs](Job) with the same [Signature].
pub(super) type StoreHandleMapping = HashMap<Signature, data::StoreHandle>;
type PinStream<T> = Pin<Box<dyn Stream<Item = T> + Send>>;
type SourceStreamHandle = Arc<Mutex<Option<PinStream<Result<Job, source::Error>>>>>;

/// The data payload that can be sent to the [Job Manager](crate::job::manager::Manager).
#[derive(Clone)]
pub enum Payload {
    /// [Source] represents a new source of [Jobs](Job).
    Source(SourceStreamHandle),
}

impl Debug for Payload {
    fn fmt(&self, f: &mut Formatter<'_>) -> core::fmt::Result {
        write!(f, "Job Payload")
    }
}

impl PartialEq for Payload {
    fn eq(&self, _other: &Self) -> bool {
        false
    }
}

pub mod data {
    //! The data mod provides the components for interacting with information that can be stored and
    //! retrieved by a [Job](super::Job) during its execution. [Keys](Key) provide a way to address
    //! this data while [Data] defines the type of data that can be stored per entry.

    use crate::base::SettingInfo;
    use futures::lock::Mutex;
    use std::collections::HashMap;
    use std::sync::Arc;

    /// A shared handle to the [Data] mapping.
    pub type StoreHandle = Arc<Mutex<HashMap<Key, Data>>>;

    #[derive(Clone, PartialEq, Eq, Hash)]
    pub enum Key {
        Identifier(&'static str),
        #[cfg(test)]
        TestInteger(usize),
    }

    #[derive(Clone, PartialEq)]
    pub enum Data {
        SettingInfo(SettingInfo),
        #[cfg(test)]
        TestData(usize),
    }
}

pub mod work {
    use super::{data, Signature};
    use crate::service::message;
    use crate::trace::TracingNonce;
    use async_trait::async_trait;

    pub enum Load {
        /// [Sequential] loads are run in order after [Loads](Load) from [Jobs](crate::job::Job)
        /// of the same [Signature](crate::job::Signature) that preceded them. These [Loads](Load)
        /// share a common data store, which can be used to share information.
        Sequential(Box<dyn Sequential + Send + Sync>, Signature),
        /// [Independent] loads are run as soon as there is availability to run as dictated by the
        /// containing [Job's](crate::job::Job) handler.
        Independent(Box<dyn Independent + Send + Sync>),
    }

    impl Load {
        /// Executes the contained workload, providing the individualized parameters based on type.
        /// This function is asynchronous and is meant to be waited upon for workload completion.
        /// Workloads are expected to wait and complete all work within the scope of this execution.
        /// Therefore, invoking code can safely presume all work has completed after this function
        /// returns.
        pub(super) async fn execute(
            self,
            messenger: message::Messenger,
            store: Option<data::StoreHandle>,
            nonce: TracingNonce,
        ) {
            match self {
                Load::Sequential(load, _) => {
                    load.execute(
                        messenger,
                        store.expect("all sequential loads should have store"),
                        nonce,
                    )
                    .await;
                }
                Load::Independent(load) => {
                    load.execute(messenger, nonce).await;
                }
            }
        }
    }

    impl std::fmt::Debug for Load {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            match self {
                Load::Sequential(_, _) => f.debug_tuple("Sequential"),
                Load::Independent(_) => f.debug_tuple("Independent"),
            }
            .field(&"..")
            .finish()
        }
    }

    #[async_trait]
    pub trait Sequential {
        /// Called when the [Job](super::Job) processing is ready for the encapsulated
        /// [Workload](super::Workload) be executed. The provided [StoreHandle](data::StoreHandle)
        /// is specific to the parent [Job](super::Job) group.
        async fn execute(
            self: Box<Self>,
            messenger: message::Messenger,
            store: data::StoreHandle,
            nonce: TracingNonce,
        );
    }

    #[async_trait]
    pub trait Independent {
        /// Called when a [Workload](super::Workload) should run. All workload specific logic should
        /// be encompassed in this method.
        async fn execute(self: Box<Self>, messenger: message::Messenger, nonce: TracingNonce);
    }
}

/// An identifier specified by [Jobs](Job) to group related workflows. This is useful for
/// [Workloads](Workload) that need to be run sequentially. The [Signature] is used by the job
/// infrastructure to associate resources such as caches.
#[derive(PartialEq, Copy, Clone, Debug, Eq, Hash)]
pub struct Signature {
    key: usize,
}

impl Signature {
    /// Constructs a new [Signature]. The key provided will group the associated [Job] with other
    /// [Jobs](Job) of the same key. The association is scoped to other [Jobs](Job) in the same
    /// parent source.
    pub(crate) fn new(key: usize) -> Self {
        Self { key }
    }
}

/// A [Job] is a simple data container that associates a [Workload] with an [execution::Type]
/// along with metadata, such as the creation time.
#[derive(Debug)]
pub struct Job {
    /// The [Workload] to be run.
    pub workload: work::Load,
    /// The [execution::Type] determining how the [Workload] will be run.
    pub execution_type: execution::Type,
}

impl Job {
    pub(crate) fn new(workload: work::Load) -> Self {
        let execution_type = match &workload {
            work::Load::Sequential(_, signature) => execution::Type::Sequential(*signature),
            _ => execution::Type::Independent,
        };

        Self { workload, execution_type }
    }
}

/// [Id] provides a unique identifier for a job within its parent space. Unlike
/// [Signatures](Signature), All [Job Ids](Id) will be unique per [Job]. [Ids](Id) should never be
/// directly constructed. An [IdGenerator] should be used instead.
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

/// [`IdGenerator`] is responsible for generating unique [Ids](Id).
pub(super) struct IdGenerator {
    next_identifier: usize,
}

impl IdGenerator {
    pub(super) fn new() -> Self {
        Self { next_identifier: 0 }
    }

    /// Produces a [`Id`] that is unique from any [`Id`] that has or will be generated by this
    /// [`IdGenerator`] instance.
    pub(super) fn generate(&mut self) -> Id {
        let return_id = Id::new(self.next_identifier);
        self.next_identifier += 1;

        return_id
    }
}

/// An enumeration of stages a [Job] can be in.
enum State {
    /// The workload associated with the [Job] has not been executed yet.
    Ready(Job),
    /// The workload is executing.
    Executing,
    /// THe workload execution has completed.
    Executed,
}

/// [Info] is used to capture details about a [Job] once it has been accepted by an entity that will
/// process it. This includes an assigned [Id] and a recording at what time it was accepted.
pub(self) struct Info {
    id: Id,
    state: State,
    execution_type: execution::Type,
}

impl Info {
    fn new(id: Id, job: Job) -> Self {
        let execution_type = job.execution_type.clone();
        Self { id, state: State::Ready(job), execution_type }
    }

    /// Retrieves the [execution::Type] of the underlying [Job].
    fn get_execution_type(&self) -> &execution::Type {
        &self.execution_type
    }

    /// Prepares the components necessary for a [Job] to execute and then returns a future to
    /// execute the [Job] workload with them. These components include a messenger for communicating
    /// with the system and the store associated with the [Job's](Job) group if applicable.
    async fn prepare_execution<F: Fn(Self, execution::Details) + Send + 'static>(
        mut self,
        delegate: &mut message::Delegate,
        stores: &mut StoreHandleMapping,
        callback: F,
    ) -> BoxFuture<'static, ()> {
        // Create a messenger for the workload to communicate with the rest of the setting
        // service.
        let messenger = delegate
            .create(message::MessengerType::Unbound)
            .await
            .expect("messenger should be available")
            .0;

        let store = self
            .execution_type
            .get_signature()
            .map(|signature| stores.entry(*signature).or_insert_with(Default::default).clone());

        Box::pin(async move {
            let nonce = fuchsia_trace::generate_nonce();
            trace!(nonce, "job execution");
            let start = now();
            let mut state = State::Executing;
            std::mem::swap(&mut state, &mut self.state);

            if let State::Ready(job) = state {
                job.workload.execute(messenger, store, nonce).await;
                self.state = State::Executed;
                callback(self, execution::Details { start_time: start, end_time: now() });
            } else {
                panic!("job not in the ready state");
            }
        })
    }
}

pub(super) mod execution {
    use super::Signature;
    use crate::job;
    use fuchsia_zircon as zx;
    use std::collections::{HashSet, VecDeque};

    /// The workload types of a [job::Job]. This enumeration is used to define how a [job::Job] will
    /// be treated in relation to other jobs from the same source.
    #[derive(PartialEq, Clone, Debug, Eq, Hash)]
    pub enum Type {
        /// Independent jobs are executed in isolation from other [Jobs](Job). Some functionality is
        /// unavailable for Independent jobs, such as caches.
        Independent,
        /// Sequential [Jobs](job::Job) wait until all pre-existing [Jobs](job::Job) of the same
        /// [Signature] are completed.
        Sequential(Signature),
    }

    impl Type {
        pub(super) fn get_signature(&self) -> Option<&Signature> {
            if let Type::Sequential(signature) = self {
                Some(signature)
            } else {
                None
            }
        }
    }

    /// A collection of [Jobs](job::Job) which have matching execution types. [Groups](Group)
    /// determine how similar [Jobs](job::Job) are executed.
    pub(super) struct Group {
        group_type: Type,
        active: HashSet<job::Id>,
        pending: VecDeque<job::Info>,
    }

    impl Group {
        /// Creates a new [Group] based on the execution [Type].
        pub(super) fn new(group_type: Type) -> Self {
            Self { group_type, active: HashSet::new(), pending: VecDeque::new() }
        }

        /// Returns whether any [Jobs](job::Job) are currently active. Pending [Jobs](job::Job) do
        /// not count towards this total.
        pub(super) fn is_active(&self) -> bool {
            !self.active.is_empty()
        }

        /// Returns whether there are any [Jobs](job::Job) waiting to be executed.
        pub(super) fn has_available_jobs(&self) -> bool {
            if self.pending.is_empty() {
                return false;
            }

            match self.group_type {
                Type::Independent => true,
                Type::Sequential(_) => self.active.is_empty(),
            }
        }

        pub(super) fn add(&mut self, job_info: job::Info) {
            self.pending.push_back(job_info);
        }

        /// Invoked by [Job](super::Job) processing code to retrieve the next [Job](super::Job) to
        /// run from this group. If there are no [Jobs](super::Job) ready for execution, None is
        /// return. Otherwise, the selected [Job](super::Job) is added to the list of active
        /// [Jobs](super::Job) for the group and handed back to the caller.
        pub(super) fn promote_next_to_active(&mut self) -> Option<job::Info> {
            if !self.has_available_jobs() {
                return None;
            }

            let active_job = self.pending.pop_front();

            if let Some(job) = &active_job {
                self.active.insert(job.id);
            }

            active_job
        }

        pub(super) fn complete(&mut self, job_info: job::Info) {
            self.active.remove(&job_info.id);
        }
    }

    /// [Details] represent the final result of an execution.
    pub(super) struct Details {
        /// The time at which the job execution started.
        // TODO(fxbug.dev/77068): Remove this attribute.
        #[allow(dead_code)]
        pub start_time: zx::Time,
        /// The time at which the job execution ended.
        // TODO(fxbug.dev/77068): Remove this attribute.
        #[allow(dead_code)]
        pub end_time: zx::Time,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::message::base::MessengerType;
    use crate::message::MessageHubUtil;
    use crate::service::test::Payload;
    use crate::service::MessageHub;
    use crate::tests::scaffold::workload::Workload;

    use matches::assert_matches;
    use rand::Rng;

    #[test]
    fn test_id_generation() {
        let mut generator = IdGenerator::new();
        // Ensure generator creates subsequent ids that don't match.
        assert!(generator.generate() != generator.generate());
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_job_functionality() {
        // Create delegate for communication between components.
        let message_hub_delegate = MessageHub::create_hub();

        // Create a top-level receptor to receive communication from the workload.
        let mut receptor = message_hub_delegate
            .create(MessengerType::Unbound)
            .await
            .expect("should create receptor")
            .1;

        // Create a messenger to send communication from the workload.
        let messenger = message_hub_delegate
            .create(MessengerType::Unbound)
            .await
            .expect("should create messenger")
            .0;

        let mut rng = rand::thread_rng();
        // The value expected to be conveyed from workload to receptor.
        let val = rng.gen();

        // Create job from workload scaffolding.
        let job = Job::new(work::Load::Independent(Workload::new(
            Payload::Integer(val),
            receptor.get_signature(),
        )));

        job.workload.execute(messenger, Some(Arc::new(Mutex::new(HashMap::new()))), 0).await;

        // Confirm received value matches the value sent from workload.
        assert_matches!(
            receptor.next_of::<Payload>().await.expect("should return result, not error").0,
            Payload::Integer(value) if value == val);
    }
}
