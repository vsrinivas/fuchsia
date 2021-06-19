// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        actor::{Actor, ActorError},
        counter::CounterTx,
    },
    fuchsia_async::{Task, Time, Timer},
    futures::{
        future::{AbortHandle, Abortable, Aborted},
        lock::Mutex,
    },
    log::debug,
    std::{sync::Arc, time::Duration},
};

/// The thread that runs an actor indefinitely
#[derive(Clone)]
pub struct ActorRunner {
    // The name of this actor
    pub name: String,

    // The duration to wait between actor operations
    pub delay: Option<Duration>,

    // A mutable reference to the actor for this configuration.
    // The runner will lock on the actor when it is performing an operation.
    // The environment can lock on the actor during reset.
    pub actor: Arc<Mutex<dyn Actor>>,
}

impl ActorRunner {
    pub fn new<A: Actor>(
        name: impl ToString,
        delay: Option<Duration>,
        actor: Arc<Mutex<A>>,
    ) -> Self {
        Self { name: name.to_string(), delay, actor: actor as Arc<Mutex<dyn Actor>> }
    }

    /// Run the actor in a new task indefinitely for the given generation.
    /// The runner will stop if the actor requests an environment reset.
    /// The amount of parallelism is determined by the caller's executor.
    // TODO(fxbug.dev/78793): Find a different way to set parallelism.
    pub fn run(
        self,
        counter_tx: CounterTx,
        generation: u64,
    ) -> (Task<Result<(ActorRunner, u64), Aborted>>, AbortHandle) {
        let (abort_handle, abort_registration) = AbortHandle::new_pair();
        let fut = async move {
            let mut local_count: u64 = 0;
            loop {
                if let Some(delay) = self.delay {
                    debug!(
                        "[{}][{}][{}] Sleeping for {:?}",
                        generation, self.name, local_count, delay
                    );
                    Timer::new(Time::after(delay.into())).await;
                }

                debug!("[{}][{}][{}] Performing...", generation, self.name, local_count);

                // Lock on the actor and perform. This prevents the environment from
                // modifying the actor until the operation is complete.
                let result = {
                    let mut actor = self.actor.lock().await;
                    actor.perform().await
                };

                match result {
                    Ok(()) => {
                        // Count this iteration towards the global count
                        let _ = counter_tx.unbounded_send(self.name.clone());
                        debug!("[{}][{}][{}] Done!", generation, self.name, local_count);
                    }
                    Err(ActorError::DoNotCount) => {
                        // Do not count this iteration towards global count
                    }
                    Err(ActorError::ResetEnvironment) => {
                        // Actor needs environment to be reset. Stop the runner
                        debug!(
                            "[{}][{}][{}] Reset Environment!",
                            generation, self.name, local_count
                        );
                        return (self, generation);
                    }
                }

                local_count += 1;
            }
        };
        (Task::spawn(Abortable::new(fut, abort_registration)), abort_handle)
    }
}
