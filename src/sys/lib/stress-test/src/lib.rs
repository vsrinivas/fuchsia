// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod actor;
pub mod environment;

mod actor_runner;
mod counter;

use {
    crate::{counter::start_counter, environment::Environment},
    fuchsia_async::{Time, Timer},
    futures::{
        future::{select, Aborted, Either},
        stream::FuturesUnordered,
        StreamExt,
    },
    log::{error, info, set_logger, set_max_level, LevelFilter},
    rand::{rngs::SmallRng, Rng, SeedableRng},
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
pub fn random_seed() -> u64 {
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
    // Defaults:
    // - target_operations: 2^64
    // - timeout_secs: 24 hours
    let target_operations = env.target_operations().unwrap_or(u64::MAX);
    let timeout_secs = Duration::from_secs(env.timeout_seconds().unwrap_or(24 * 60 * 60));

    // Start the counter thread
    // The counter thread keeps track of the global operation count.
    // Each actor will send a message to the counter thread when an operation is completed.
    // When the target operation count is hit, the counter task exits.
    let (counter_task, counter_tx) = start_counter(target_operations);

    // Create a timeout task
    let timeout = Timer::new(Time::after(timeout_secs.into()));
    let mut test_end = select(counter_task, timeout);

    // A monotonically increasing counter representing the current generation.
    // On every environment reset, the generation is incremented.
    let mut generation: u64 = 0;

    // Start all the runners
    let (mut runner_tasks, mut runner_abort): (FuturesUnordered<_>, Vec<_>) =
        env.actor_runners().into_iter().map(|r| r.run(counter_tx.clone(), generation)).unzip();

    loop {
        // Wait for one of the runners, counter task or timer to return
        let either = select(test_end, runner_tasks.next()).await;
        match either {
            Either::Left((test_end_either, _next)) => {
                let reason = match test_end_either {
                    Either::Left(..) => "operation count",
                    Either::Right(..) => "timeout",
                };

                // The counter/timer task returned.
                // The target operation count was hit or the timer expired.
                // The test has completed.
                info!("Stress test has completed because of {}!", reason);
                for abort in runner_abort {
                    abort.abort();
                }
                // We don't care if tasks finished or were aborted, but we want them not running
                // anymore before we return.
                //
                // Runaway threads can cause problems if they're using objects from the main
                // executor, and it's generally a good idea to clean up after ourselves here.
                let () = runner_tasks.map(|_: Result<_, Aborted>| ()).collect().await;
                break;
            }
            Either::Right((None, _counter_task)) => {
                info!("No runners to operate");
                break;
            }
            Either::Right((Some(result), task)) => {
                let (runner, runner_generation) = result.expect("no tasks have been aborted");
                // Normally, actor runners run indefinitely.
                // However, one of the actor runners has returned.
                // This is because an actor has requested an environment reset.

                // Move the counter/timer back
                test_end = task;

                // Did the runner request a reset at the current generation?
                if runner_generation == generation {
                    // Reset the environment
                    info!("Resetting environment");
                    env.reset().await;

                    // Advance the generation
                    generation += 1;
                }

                // Restart this runner with the current generation
                let (task, abort) = runner.run(counter_tx.clone(), generation);
                runner_tasks.push(task);
                runner_abort.push(abort);
            }
        }
    }
}
