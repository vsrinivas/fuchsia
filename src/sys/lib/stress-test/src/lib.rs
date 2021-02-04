// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod actor;
pub mod environment;

mod actor_runner;
mod counter;

use {
    crate::{actor_runner::ActorRunner, counter::start_counter, environment::Environment},
    fuchsia_async::{Task, TimeoutExt},
    futures::future::{select, select_all, Either},
    log::{error, info, set_logger, set_max_level, LevelFilter},
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
    let env_string = format!("{:#?}", env);

    info!("--------------------- stressor is starting -----------------------");
    info!("{}", env_string);
    info!("------------------------------------------------------------------");

    {
        // Setup a panic handler that prints out details of this invocation on crash
        let default_panic_hook = std::panic::take_hook();
        std::panic::set_hook(Box::new(move |panic_info| {
            error!("");
            error!("--------------------- stressor has crashed -----------------------");
            error!("{}", env_string);
            error!("------------------------------------------------------------------");
            error!("");
            default_panic_hook(panic_info);
        }));
    }

    // Extract the data from the environment
    let target_operations = env.target_operations().unwrap_or(u64::MAX);
    let timeout_secs = env.timeout_seconds();

    // Start the counter thread
    // The counter thread keeps track of the global operation count.
    // Each actor will send a message to the counter thread when an operation is completed.
    // When the target operation count is hit, the counter task exits.
    let (mut counter_task, counter_tx) = start_counter(target_operations);

    // A monotonically increasing counter representing the current generation.
    // On every environment reset, the generation is incremented.
    let mut generation: u64 = 0;

    // Start all the runners
    let mut runner_tasks: Vec<Task<(ActorRunner, u64)>> =
        env.actor_runners().into_iter().map(|r| r.run(counter_tx.clone(), generation)).collect();

    let test_loop = async move {
        loop {
            let joined_runners = select_all(runner_tasks.drain(..));

            // Wait for one of the runners or the counter task to return
            let either = select(counter_task, joined_runners).await;
            match either {
                Either::Left(_) => {
                    // The counter task returned.
                    // The target operation count was hit.
                    // The test has completed.
                    info!("Test completed {} operations!", target_operations);
                    break;
                }
                Either::Right((((runner, runner_generation), _, mut other_runner_tasks), task)) => {
                    // Normally, actor runners run indefinitely.
                    // However, one of the actor runners has returned.
                    // This is because an actor has requested an environment reset.

                    // Move the counter task back
                    counter_task = task;

                    // Did the runner request a reset at the current generation?
                    if runner_generation == generation {
                        // Reset the environment
                        info!("Resetting environment");
                        env.reset().await;

                        // Advance the generation
                        generation += 1;
                    }

                    // Restart this runner with the current generation
                    let task = runner.run(counter_tx.clone(), generation);
                    other_runner_tasks.push(task);

                    // Move the runner tasks back
                    runner_tasks = other_runner_tasks;
                }
            }
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
