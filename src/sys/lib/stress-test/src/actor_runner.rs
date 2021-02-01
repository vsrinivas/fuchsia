// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        actor::{ActorConfig, ActorError},
        counter::CounterTx,
    },
    fuchsia_async::Timer,
    log::debug,
    std::time::Duration,
};

/// The thread that runs an actor against an instance until an error occurs.
pub struct ActorRunner<'a> {
    config: ActorConfig<'a>,
}

impl<'a> ActorRunner<'a> {
    pub fn new(config: ActorConfig<'a>) -> Self {
        Self { config }
    }

    /// Run the actor against a given instance on a new thread indefinitely.
    /// The runner will stop if the actor returns an error or if the actor is taken.
    pub async fn run(self, instance_id: u64, counter_tx: CounterTx) {
        let name = self.config.name;
        let delay = self.config.delay;
        let mut local_count: u64 = 0;

        loop {
            if delay > 0 {
                debug!(
                    "[{}][{}][{}] Sleeping for {} seconds",
                    name, instance_id, local_count, delay
                );
                Timer::new(Duration::from_secs(delay)).await;
            }
            debug!("[{}][{}][{}] Performing...", name, instance_id, local_count);

            // At this point, the actor must perform.
            // If any other thread is attempting to take the actor, they must
            // wait until the actor is done performing.
            let result = self.config.actor.perform().await;

            match result {
                Ok(()) => {
                    // Count this iteration towards the global count
                    let _ = counter_tx.unbounded_send(name.clone());
                    debug!("[{}][{}][{}] Done!", name, instance_id, local_count);
                }
                Err(ActorError::DoNotCount) => {
                    // Do not count this iteration towards global count
                }
                Err(ActorError::ResetEnvironment) => {
                    // Actor requires the environment to be reset. Stop the runner
                    debug!("[{}][{}][{}] Reset Environment!", name, local_count, instance_id);
                    return;
                }
            }
            local_count += 1;
        }
    }
}
