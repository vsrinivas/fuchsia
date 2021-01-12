// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod actor;
pub mod data;
pub mod environment;
pub mod fvm;
pub mod instance;
pub mod io;

mod actor_runner;
mod counter;

use {
    crate::{
        actor_runner::ActorRunner, counter::start_counter, environment::Environment,
        instance::InstanceUnderTest,
    },
    fuchsia_async::TimeoutExt,
    futures::future::{select, select_all, Either},
    log::{debug, error, info, set_logger, set_max_level, LevelFilter},
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
pub async fn run_test<I: InstanceUnderTest, E: Environment<I>>(mut env: E) {
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

    let mut actors = env.actors().await;
    let target_operations = env.target_operations().unwrap_or(u64::MAX);
    let (mut counter_task, counter_tx) = start_counter(target_operations);
    let mut instance_id: u64 = 0;
    let timeout_secs = env.timeout_seconds().unwrap_or(u64::MAX);

    // Put a timeout on the test loop
    async move {
        loop {
            debug!("Creating instance-under-test #{}", instance_id);
            let instance = env.new_instance().await;
            let mut runners = vec![];
            let mut tasks = vec![];

            // Create runners for all the actor configs
            for actor in actors.drain(..) {
                let (runner, task) =
                    ActorRunner::new(instance_id, actor, instance.clone(), counter_tx.clone());
                runners.push(runner);
                tasks.push(task);
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
                    // One of the actors failed.
                    // Get all the actors + counter task back.
                    counter_task = task;
                    for runner in runners {
                        let actor = runner.take().await;
                        debug!("Retrieved {}", actor.name);
                        actors.push(actor);
                    }
                }
            }
            instance_id += 1;
        }
    }
    .on_timeout(Duration::from_secs(timeout_secs), || {
        info!("Test completed after {} seconds!", timeout_secs);
    })
    .await;
}
