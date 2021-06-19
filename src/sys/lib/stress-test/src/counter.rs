// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async::Task,
    futures::{channel::mpsc, StreamExt},
    log::debug,
    std::collections::HashMap,
};

/// An unbounded mpsc channel connecting each runner to the counter thread.
///
/// CounterTx is cloned and given to all runners. Upon a successful operation, the runner
/// will send their actor's name over the channel.
///
/// The counter thread receives the actors name and updates the current operation count.
/// This ensures that runners are not blocked.
pub type CounterTx = mpsc::UnboundedSender<String>;

/// Starts a new task that maintains a count of all successful operations
/// This task will terminate when the target number of operations has been hit.
///
/// Returns the counter task and a CounterTx.
pub fn start_counter(target: u64) -> (Task<()>, CounterTx) {
    let (tx, mut rx) = mpsc::unbounded();
    let task = Task::spawn(async move {
        // Keep track of global count + individual actor contributions
        let mut count_map: HashMap<String, u64> = HashMap::new();
        let mut total = 0;

        // Run this task until the count has been met
        while total < target {
            // Wait for an actor to finish an operation
            let key = rx.next().await.unwrap();

            // Update the actor's contribution
            if let Some(value) = count_map.get_mut(&key) {
                *value += 1;
            } else {
                count_map.insert(key, 1);
            }

            // Update global count
            total += 1;

            debug!("Counters -> [total:{}] {:?}", total, count_map);
        }
    });
    (task, tx)
}
