// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        actor::{ActorConfig, ActorError},
        counter::CounterTx,
        instance::InstanceUnderTest,
    },
    fuchsia_async::Task,
    futures::lock::Mutex,
    log::debug,
    std::{sync::Arc, thread::sleep, time::Duration},
};

pub enum ActorRunnerResult {
    /// The actor owned by this runner requires a new instance.
    /// This happens when the actor returns ActorError::GetNewInstance.
    GetNewInstance,

    /// The actor was taken from the runner.
    /// This can happen when another runner returned GetNewInstance,
    /// causing ActorRunner::take() to be called on all runners.
    ActorLost,
}

/// The thread that runs an actor against an instance until an error occurs.
#[derive(Clone)]
pub struct ActorRunner<I: InstanceUnderTest> {
    config: Arc<Mutex<Option<ActorConfig<I>>>>,
}

impl<I: InstanceUnderTest> ActorRunner<I> {
    pub fn new(
        instance_id: u64,
        config: ActorConfig<I>,
        instance: I,
        counter_tx: CounterTx,
    ) -> (Self, Task<ActorRunnerResult>) {
        let runner = Self { config: Arc::new(Mutex::new(Some(config))) };
        let task = runner.clone().run(instance_id, instance, counter_tx);
        (runner, task)
    }

    /// Takes ownership of the actor away from this runner.
    /// Blocks if the actor is currently performing.
    /// Panics if the actor was already taken.
    ///
    /// If any actor requires a new instance, all actors are taken from their runners.
    /// A new instance is created and the actors are given new runners that operate on
    /// the new instance.
    ///
    /// Runners that no longer have an actor will terminate with ActorRunnerResult::ActorLost.
    pub async fn take(self) -> ActorConfig<I> {
        let mut lock = self.config.lock().await;
        lock.take().expect("Actor was already taken!")
    }

    /// Gets the actor and makes it perform a single operation against a given instance.
    ///
    /// Possible results:
    /// * Some(Ok(())) -> Actor was present and operation succeeded
    /// * Some(Err(ActorError)) -> Actor was present but operation failed
    /// * None -> Actor was taken
    async fn perform(&self, instance: &mut I) -> Option<Result<(), ActorError>> {
        let mut lock = self.config.lock().await;
        if let Some(config) = &mut *lock {
            // At this point, the actor must perform.
            // If any other thread is attempting to take the actor, they must
            // wait until the actor is done performing.
            Some(config.actor.perform(instance).await)
        } else {
            None
        }
    }

    /// Run the actor against a given instance on a new thread indefinitely.
    /// The runner will stop if the actor returns an error or if the actor is taken.
    fn run(
        self,
        instance_id: u64,
        mut instance: I,
        counter_tx: CounterTx,
    ) -> Task<ActorRunnerResult> {
        Task::blocking(async move {
            let (name, delay) = {
                let lock = self.config.lock().await;
                let config = lock.as_ref().unwrap();
                (config.name.clone(), config.delay)
            };
            let mut local_count: u64 = 0;
            loop {
                if delay > 0 {
                    debug!(
                        "[{}][{}][{}] Sleeping for {} seconds",
                        name, instance_id, local_count, delay
                    );
                    sleep(Duration::from_secs(delay));
                }
                debug!("[{}][{}][{}] Performing...", name, instance_id, local_count);
                let result = self.perform(&mut instance).await;
                match result {
                    Some(Ok(())) => {
                        // Count this iteration towards the global count
                        let _ = counter_tx.unbounded_send(name.clone());
                        debug!("[{}][{}][{}] Done!", name, instance_id, local_count);
                    }
                    Some(Err(ActorError::DoNotCount)) => {
                        // Do not count this iteration towards global count
                    }
                    Some(Err(ActorError::GetNewInstance)) => {
                        // Actor needs new instance. Stop the runner
                        debug!("[{}][{}][{}] Needs new instance!", name, local_count, instance_id);
                        return ActorRunnerResult::GetNewInstance;
                    }
                    None => {
                        // The actor was taken. Stop the runner
                        debug!("[{}][{}][{}] Actor lost!", name, instance_id, local_count);
                        return ActorRunnerResult::ActorLost;
                    }
                }
                local_count += 1;
            }
        })
    }
}
