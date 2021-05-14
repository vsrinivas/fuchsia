// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Job Management Support
//!
//! # Summary
//!
//! The manager mod defines entities for managing [Job] sources and controlling the execution of
//! pending [workloads](crate::job::work::Load) contained in those [Jobs](Job). [Manager] provides a
//! concrete implementation of a [Job] processor. Outside clients send [Job] sources to the
//! [Manager] over the [MessageHub](crate::message::message_hub::MessageHub). In turn, the [Manager]
//! will process each received source for new [Jobs](Job) and provide the necessary backing, such as
//! caches, to support executing the [Job].

use crate::job::execution;
use crate::job::source::{self, Error};
use crate::job::{self, Job, Payload};
use crate::message::base::MessengerType;
use crate::service;
use crate::service::message;

use ::futures::FutureExt;
use core::pin::Pin;
use fuchsia_async as fasync;
use futures::stream::{FuturesOrdered, StreamFuture};
use futures::Stream;
use futures::StreamExt;
use std::collections::HashMap;
use std::convert::TryFrom;

/// [Manager] processes incoming streams for new [Job]s. [Job]s are handled and executed by the
/// [Manager] based on the [Job] definitions.
// TODO(fxbug.dev/70534): Use Manager to handle FIDL requests.
#[allow(dead_code)]
pub struct Manager {
    /// A mapping from [source id](source::Id) to [handler](source::Handler). This mapping is used
    /// to retrieve the [handler](source::Handler) for job updates (inserting, retrieving,
    /// completing) and source maintenance (cleaning up on exit).
    sources: HashMap<source::Id, source::Handler>,
    /// A future representing the collection of sources given to this manager. The future produces
    /// a tuple containing the [Job] and [source id](source::Id) of the stream to associate with it.
    /// None will be passed as the [Job] if the [Job] stream has been reached for a given source.
    job_futures: FuturesOrdered<
        StreamFuture<Pin<Box<dyn Stream<Item = (source::Id, Option<Result<Job, Error>>)> + Send>>>,
    >,
    /// A [Id generator](source::IdGenerator) responsible for producing unique [Ids](source::Id) for
    /// the received sources.
    source_id_generator: source::IdGenerator,
    /// A Sender used to communicate back to the [Manager] that the execution of a [Job] has
    /// completed.
    execution_completion_sender:
        futures::channel::mpsc::UnboundedSender<(source::Id, job::Info, execution::Details)>,
    /// A [delegate](message::Delegate) used to generate the necessary messaging components for
    /// [Jobs](Job) to use.
    message_hub_delegate: message::Delegate,
}

impl Manager {
    /// Creates a new [Manager] with the given MessageHub. A reference to the service MessageHub is
    /// provided so that it can be passed to [Jobs](Job) for communicating with the rest of the
    /// service.
    // TODO(fxbug.dev/70534): Use Manager to handle FIDL requests.
    #[allow(dead_code)]
    pub async fn spawn(message_hub_delegate: &message::Delegate) -> message::Signature {
        // Create a top-level receptor in the MessageHub to accept new sources from.
        let receptor = message_hub_delegate
            .create(MessengerType::Unbound)
            .await
            .expect("messenger should be available")
            .1;

        // Create a channel for execution tasks to communicate when a Job has been completed.
        let (execution_completion_sender, execution_completion_receiver) =
            futures::channel::mpsc::unbounded::<(source::Id, job::Info, execution::Details)>();

        // Capture the top-level receptor's signature so it can be passed back
        // to the caller for sending new sources.
        let signature = receptor.get_signature();

        let mut manager = Self {
            sources: HashMap::new(),
            job_futures: FuturesOrdered::new(),
            source_id_generator: source::IdGenerator::new(),
            execution_completion_sender,
            message_hub_delegate: message_hub_delegate.clone(),
        };

        // Spawn a task to run the main event loop, which handles the following events:
        // 1) Receiving new sources to process
        // 2) Accepting and processing new jobs from sources
        // 3) Executing jobs and handling the their results
        fasync::Task::spawn(async move {
            let source_fuse = receptor.fuse();
            let execution_fuse = execution_completion_receiver.fuse();

            futures::pin_mut!(source_fuse, execution_fuse);
            loop {
                futures::select! {
                    source_event = source_fuse.select_next_some() => {
                        manager.process_source_event(source_event).await;
                    },
                    (source_id, job_info, details) = execution_fuse.select_next_some() => {
                        manager.process_completed_execution(source_id, job_info, details).await;
                    },
                    (job_info, stream) = manager.job_futures.select_next_some() => {
                        // Since the manager owns job_futures, we should never reach the end of
                        // the stream.
                        let (source_id, job) = job_info.expect("job should be present");
                        manager.process_job(source_id, job, stream).await;
                    }
                }
            }
        })
        .detach();

        signature
    }

    // Propagates results of a completed job by cleaning up references, informing the parent source
    // of the job completion, and checking if another job can be processed.
    async fn process_completed_execution(
        &mut self,
        source_id: source::Id,
        job_info: job::Info,
        _execution_details: execution::Details,
    ) {
        // Fetch the source and inform it that its child Job has completed.
        let source_handler = &mut self.sources.get_mut(&source_id).expect("should find source");
        source_handler.handle_job_completion(job_info);
        self.remove_source_if_necessary(&source_id);

        // Continue processing available jobs.
        self.process_next_job().await;
    }

    // Executes the next job if conditions to run another job are met. If so, the manager consults
    // available sources for a candidate job and then executes the first one found.
    async fn process_next_job(&mut self) {
        // Iterate through sources and see if any source has a pending job
        for (source_id, source_handler) in &mut self.sources.iter_mut() {
            let source_id = *source_id;
            let execution_tx = self.execution_completion_sender.clone();

            source_handler
                .execute_next(&mut self.message_hub_delegate, move |job_info, details| {
                    if let Err(error) = execution_tx.unbounded_send((source_id, job_info, details))
                    {
                        panic!("Failed to send message. error: {:?}", error);
                    };
                })
                .await;
        }
    }

    // Processes a new source, generating the associated tracking data and inserting its job stream
    // into the monitored job futures.
    async fn process_source_event(&mut self, event: service::message::MessageEvent) {
        // Manager only expects to receive new job streams from events passed into this method.
        let Payload::Source(source) = Payload::try_from(event).expect("should convert to source");

        // Extract job stream from payload.
        let job_stream = source.lock().await.take().expect("should capture job stream");

        // Associate stream with a new id.
        let source_id = self.source_id_generator.generate();

        // Create a handler to manage jobs produced by this stream.
        self.sources.insert(source_id, source::Handler::new());

        // Add the stream to the monitored pool. associate jobs with the source id along with
        // appending an empty value to the end for indicating when the stream has completed.
        self.job_futures.push(
            job_stream
                .map(move |val| (source_id, Some(val)))
                .chain(async move { (source_id, None) }.into_stream())
                .boxed()
                .into_future(),
        );
    }

    async fn process_job(
        &mut self,
        source: source::Id,
        job: Option<Result<Job, Error>>,
        source_stream: Pin<Box<dyn Stream<Item = (source::Id, Option<Result<Job, Error>>)> + Send>>,
    ) {
        if let Some(Ok(job)) = job {
            // When the stream produces a job, associate with the appropriate source. Then try see
            // if any job is available to run.
            self.sources.get_mut(&source).expect("source should be present").add_pending_job(job);
            self.job_futures.push(source_stream.into_future());
            self.process_next_job().await;
        } else {
            // In the case of an error or the end of the stream has been reached (None), clean up
            // the source.
            self.complete_source(&source);

            // TODO(fxbug.dev/73414): Cancel in-flight jobs for the source.
        }
    }

    fn complete_source(&mut self, source_id: &source::Id) {
        self.sources.get_mut(source_id).expect("should find source").complete();
        self.remove_source_if_necessary(source_id);
    }

    fn remove_source_if_necessary(&mut self, source_id: &source::Id) {
        let source_info = self.sources.get_mut(source_id).expect("should find source");

        if source_info.is_completed() {
            self.sources.remove(source_id);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::job::Payload;
    use crate::message::base::Audience;
    use crate::service::message;
    use crate::service::test;
    use crate::tests::scaffold::workload::Workload;

    use futures::lock::Mutex;
    use matches::assert_matches;
    use std::sync::Arc;

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_manager_job_processing() {
        // Create delegate for communication between components.
        let message_hub_delegate = message::create_hub();

        let results: Vec<i64> = (0..10).collect();

        // Create a top-level receptor to receive job results from.
        let mut receptor = message_hub_delegate
            .create(MessengerType::Unbound)
            .await
            .expect("should create receptor")
            .1;

        let manager_signature = Manager::spawn(&message_hub_delegate).await;

        // Create a messenger to send job sources to the manager.
        let messenger = message_hub_delegate
            .create(MessengerType::Unbound)
            .await
            .expect("should create messenger")
            .0;

        let mut job_futures = FuturesOrdered::new();

        // Send each job as a separate source.
        for result in &results {
            let result = *result;
            let signature = receptor.get_signature();
            job_futures.push(async move {
                Ok(Job::new(job::work::Load::Independent(Workload::new(
                    test::Payload::Integer(result),
                    signature,
                ))))
            });
        }

        messenger
            .message(
                Payload::Source(Arc::new(Mutex::new(Some(job_futures.boxed())))).into(),
                Audience::Messenger(manager_signature),
            )
            .send()
            .ack();

        for result in results {
            // Confirm received value matches the value sent from workload.
            assert_matches!(receptor.next_of::<test::Payload>().await.expect("should have payload").0,
                test::Payload::Integer(value) if value == result);
        }
    }
}
