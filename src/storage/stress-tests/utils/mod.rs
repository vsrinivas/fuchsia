// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod actor;
pub mod data;
pub mod environment;
pub mod fvm;
pub mod io;

mod actor_runner;
mod counter;

use {
    crate::{actor_runner::ActorRunner, counter::start_counter, environment::Environment},
    fuchsia_async::TimeoutExt,
    futures::{
        future::{select, select_all, Either},
        FutureExt,
    },
    log::{debug, info, set_logger, set_max_level, LevelFilter},
    rand::{rngs::SmallRng, FromEntropy, Rng},
    std::{
        io::{stdout, Write},
        time::Duration,
    },
};

// A simple logger that prints to stdout
pub struct StdoutLogger;

impl StdoutLogger {
    pub fn init(filter: LevelFilter) {
        set_logger(&StdoutLogger).expect("Failed to set StdoutLogger as global logger");
        set_max_level(filter);
    }
}

impl log::Log for StdoutLogger {
    fn enabled(&self, _metadata: &log::Metadata<'_>) -> bool {
        true
    }

    fn log(&self, record: &log::Record<'_>) {
        if self.enabled(record.metadata()) {
            match record.level() {
                log::Level::Info => {
                    println!("{}", record.args());
                }
                log::Level::Error => {
                    eprintln!("{}: {}", record.level(), record.args());
                }
                _ => {
                    println!("{}: {}", record.level(), record.args());
                }
            }
        }
    }

    fn flush(&self) {
        stdout().flush().unwrap();
    }
}

/// Use entropy to generate a random seed
pub fn random_seed() -> u128 {
    let mut temp_rng = SmallRng::from_entropy();
    temp_rng.gen()
}

/// Runs the test loop for the given environment to completion.
pub async fn run_test<E: 'static + Environment>(mut env: E) {
    // Extract the data from the environment
    let target_operations = env.target_operations().unwrap_or(u64::MAX);
    let timeout_secs = env.timeout_seconds();

    // Start the counter thread
    // The counter thread keeps track of the global operation count.
    // Each actor will send a message to the counter thread when an operation is completed.
    // When the target operation count is hit, the counter task exits.
    let (mut counter_task, counter_tx) = start_counter(target_operations);

    // A monotonically increasing counter representing the current instance.
    // On every environment reset, the instance ID is incremented.
    let mut instance_id: u64 = 0;

    let test_loop = async move {
        loop {
            {
                debug!("Creating instance-under-test #{}", instance_id);

                let configs = env.actor_configs();
                let mut tasks = vec![];

                // Create runners for all the actor configs
                for config in configs {
                    let runner = ActorRunner::new(config);
                    let future = runner.run(instance_id.clone(), counter_tx.clone());
                    let future = future.boxed();
                    tasks.push(future);
                }

                let runners_future = select_all(tasks);

                // Wait for one of the runners or the counter task to return
                let either = select(counter_task, runners_future).await;
                match either {
                    Either::Left(_) => {
                        // The counter task returned.
                        // The target operation count was hit.
                        // The test has completed.
                        info!("Test completed {} operations!", target_operations);
                        break;
                    }
                    Either::Right((_, task)) => {
                        // Normally, actor runners run indefinitely.
                        // However, one of the actor runners has returned.
                        // This is because an actor has requested an environment reset.

                        // Get the counter task back
                        counter_task = task;
                    }
                }

                instance_id += 1;
            }

            info!("Resetting environment");
            env.reset().await;
        }
    };

    if let Some(timeout_secs) = timeout_secs {
        // Put a timeout on the test loop.
        // Users can ask a stress test to run as many operations as it can within
        // a certain time limit. Hence it is not an error for this timeout to be hit.
        test_loop
            .on_timeout(Duration::from_secs(timeout_secs), move || {
                info!("Test completed after {} seconds!", timeout_secs);
            })
            .await;
    } else {
        test_loop.await;
    }
}
