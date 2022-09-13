// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::collector::DataCollector,
    crate::model::model::DataModel,
    anyhow::Result,
    serde::{Deserialize, Serialize},
    std::collections::{HashMap, HashSet},
    std::fmt,
    std::sync::{mpsc, Arc, Condvar, Mutex},
    std::thread,
    thiserror::Error,
    tracing::{error, info},
    uuid::Uuid,
};

#[derive(Error, Debug)]
pub enum SchedulerError {
    #[error("Scheduled tasks have unmet dependencies")]
    UnmetDependencies,
}

#[derive(PartialEq, Eq, Debug, Copy, Clone, Deserialize, Serialize)]
pub enum CollectorState {
    Scheduled,
    Running,
    Idle,
    Terminated,
}

impl fmt::Display for CollectorState {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            CollectorState::Scheduled => write!(f, "Scheduled"),
            CollectorState::Running => write!(f, "Running"),
            CollectorState::Idle => write!(f, "Idle"),
            CollectorState::Terminated => write!(f, "Terminated"),
        }
    }
}

enum CollectorMessage {
    Collect(Arc<DataModel>),
    Terminate,
}

/// A CollectorInstance runs a single `DataCollector` on its own single thread.
/// The CollectorInstance manages all messaging to the worker thread which
/// includes non-preemptive thread termination when the instance is dropped.
struct CollectorInstance {
    pub instance_id: Uuid,
    pub name: String,
    pub dependencies: HashSet<Uuid>,
    pub state: Arc<(Mutex<CollectorState>, Condvar)>,
    sender: mpsc::Sender<CollectorMessage>,
    worker: Option<thread::JoinHandle<()>>,
}

impl CollectorInstance {
    pub fn new(
        instance_id: Uuid,
        name: String,
        dependencies: HashSet<Uuid>,
        collector: Arc<dyn DataCollector>,
    ) -> Self {
        let (sender, recv) = mpsc::channel();
        let state = Arc::new((Mutex::new(CollectorState::Idle), Condvar::new()));
        // Clone these so that we can pass references to the worker thread.
        let collector_state = Arc::clone(&state);
        let data_collector = Arc::clone(&collector);
        let worker = thread::spawn(move || loop {
            let message = recv.recv().unwrap();
            match message {
                CollectorMessage::Collect(model) => {
                    {
                        let (state_lock, cvar) = &*collector_state;
                        let mut state = state_lock.lock().unwrap();
                        *state = CollectorState::Running;
                        cvar.notify_one();
                    }

                    if let Err(err) = data_collector.collect(Arc::clone(&model)) {
                        error!(%err, "Collector failed with error");
                    }

                    {
                        let (state_lock, cvar) = &*collector_state;
                        let mut state = state_lock.lock().unwrap();
                        *state = CollectorState::Idle;
                        cvar.notify_one();
                    }
                }
                CollectorMessage::Terminate => {
                    let (state_lock, cvar) = &*collector_state;
                    let mut state = state_lock.lock().unwrap();
                    *state = CollectorState::Terminated;
                    cvar.notify_one();
                    break;
                }
            }
        });
        Self { instance_id, name, dependencies, state, sender, worker: Some(worker) }
    }

    /// Sends a message to the collector instance worker to run this collector.
    /// If the worker is currently busy this message will be queued.
    pub fn run(&self, model: Arc<DataModel>) {
        let (state_lock, cvar) = &*self.state;
        let mut state = state_lock.lock().unwrap();
        if *state != CollectorState::Terminated {
            *state = CollectorState::Scheduled;
            cvar.notify_one();
        }
        self.sender.send(CollectorMessage::Collect(model)).unwrap();
    }
}

impl Drop for CollectorInstance {
    fn drop(&mut self) {
        self.sender.send(CollectorMessage::Terminate).unwrap();
        if let Some(worker) = self.worker.take() {
            worker.join().unwrap();
        }
    }
}

type CollectorHandle = usize;

/// The `CollectorScheduler` contains all of the `DataCollectors` registered by `Plugins`.
/// It provides a simple way to collectively run the data collectors.
pub struct CollectorScheduler {
    model: Arc<DataModel>,
    collectors: Arc<Mutex<HashMap<CollectorHandle, CollectorInstance>>>,
    next_handle: usize,
}

impl CollectorScheduler {
    pub fn new(model: Arc<DataModel>) -> Self {
        Self { model: model, collectors: Arc::new(Mutex::new(HashMap::new())), next_handle: 1 }
    }

    /// Adds a collector associated with a particular `instance_id` to the collector
    /// scheduler. An internal handle is returned that can be used to reference
    /// the specific Collector instance in the future.
    pub fn add(
        &mut self,
        instance_id: Uuid,
        name: impl Into<String>,
        dependencies: HashSet<Uuid>,
        collector: Arc<dyn DataCollector>,
    ) -> CollectorHandle {
        let mut collectors = self.collectors.lock().unwrap();
        let handle = self.next_handle;
        self.next_handle += 1;
        collectors.insert(
            handle,
            CollectorInstance::new(instance_id, name.into(), dependencies, collector),
        );
        handle
    }

    /// Returns a list of all the scheduled collectors.
    pub fn collectors_all(&self) -> Vec<(CollectorHandle, String)> {
        let collectors = self.collectors.lock().unwrap();
        let mut collector_list = Vec::new();
        for (handle, instance) in collectors.iter() {
            collector_list.push((handle.clone(), instance.name.clone()))
        }
        collector_list
    }

    /// Returns a list of all associated collector handles and names for each
    /// collector. Collector names should be unique per instance_id but not
    /// globally.
    pub fn collectors(&self, instance_id: Uuid) -> Vec<(CollectorHandle, String)> {
        let collectors = self.collectors.lock().unwrap();
        let mut collector_list = Vec::new();
        for (handle, instance) in collectors.iter() {
            if instance.instance_id == instance_id {
                collector_list.push((handle.clone(), instance.name.clone()))
            }
        }
        collector_list
    }

    /// Removes all `CollectorInstance` objects with a matching instance-id.
    /// This effectively unhooks all the plugins collectors.
    pub fn remove_all(&mut self, instance_id: Uuid) {
        let mut collectors = self.collectors.lock().unwrap();
        collectors.retain(|_, v| v.instance_id != instance_id);
    }

    /// Removes all `CollectorInstance` objects with a matching instance-id.
    /// This effectively unhooks all the plugins collectors.
    pub fn remove(&mut self, handle: &CollectorHandle) {
        let mut collectors = self.collectors.lock().unwrap();
        collectors.remove(handle);
    }

    /// Runs all of the collector instances taking into account dependencies.
    /// For example if Collector A depends on Collector B the scheduler will
    /// first run Collector A wait until it has finished and then Schedule
    /// Collector B. This schedule will terminate once all collectors
    /// have been run.
    pub fn schedule(&self) -> Result<()> {
        let collectors = self.collectors.lock().unwrap();
        info!(total_tasks = collectors.len(), "Collector Scheduler: Scheduling Tasks");
        // The set of instances that have finished.
        let mut collectors_to_schedule: HashSet<CollectorHandle> =
            collectors.iter().map(|(handle, _)| handle.clone()).collect();
        let mut collector_finished = HashSet::new();

        while !collectors_to_schedule.is_empty() {
            let mut collectors_to_run = HashSet::new();
            // Select all collectors that have their dependencies met.
            for handle in collectors_to_schedule.iter() {
                let collector_deps = &collectors.get(handle).unwrap().dependencies;
                if collector_deps.iter().all(|&id| collector_finished.contains(&id)) {
                    collectors_to_run.insert(handle.clone());
                }
            }

            // Safe guard against infinite looping.
            if collectors_to_run.is_empty() && !collectors_to_schedule.is_empty() {
                error!(
                    total_tasks = collectors_to_schedule.len(),
                    "Collector Scheduler: Fatal error, tasks have unmet dependencies.",
                );
                for handle in collectors_to_schedule.iter() {
                    error!("Failed to schedule: {}", collectors.get(handle).unwrap().name);
                }
                error!("Collector Scheduler: Aborting collection tasks");
                return Err(SchedulerError::UnmetDependencies.into());
            }

            // Execute the current batch of collectors that are unblocked.
            info!(
                total_tasks = collectors_to_run.len(),
                "Collector Scheduler: Batching Independent Tasks"
            );
            for handle in collectors_to_run.iter() {
                let collector = collectors.get(handle).unwrap();
                info!(
                    "Running Collector {} from Plugin Instance {}",
                    collector.name, collector.instance_id
                );
                collector.run(Arc::clone(&self.model));
            }
            // Wait for the current set of handles to finish. A cvar is used
            // to sleep this thread waking it up only on state changes from
            // one of the collectors.
            info!("Collector Scheduler: Batched Tasks Started");
            for handle in collectors_to_run.iter() {
                let collector = collectors.get(handle).unwrap();
                let (state_lock, cvar) = &*collector.state;
                let mut state = state_lock.lock().unwrap();
                while *state != CollectorState::Idle {
                    state = cvar.wait(state).unwrap();
                }
            }
            info!("Collector Scheduler: Batched Tasks Finished");
            // Update the collector state sets.
            for handle in collectors_to_run.iter() {
                let instance_id = collectors.get(handle).unwrap().instance_id;
                collector_finished.insert(instance_id);
                collectors_to_schedule.remove(handle);
            }
        }
        info!(total_tasks = collectors.len(), "Collector Scheduler: Finished Tasks");
        Ok(())
    }

    /// Returns true if all collectors are currently idle. This function is not
    /// thread safe.
    pub fn all_idle(&self) -> bool {
        let collectors = self.collectors.lock().unwrap();
        for (_, instance) in collectors.iter() {
            let (state_lock, _cvar) = &*instance.state;
            if *state_lock.lock().unwrap() == CollectorState::Running {
                return false;
            }
        }
        return true;
    }

    /// Retrieve the collector state of a particular collector.
    pub fn state(&self, handle: &CollectorHandle) -> Option<CollectorState> {
        let collectors = self.collectors.lock().unwrap();
        if let Some(collector) = collectors.get(handle) {
            let (state_lock, _cvar) = &*collector.state;
            let state = state_lock.lock().unwrap();
            Some(*state)
        } else {
            None
        }
    }
}
#[cfg(test)]
mod tests {
    use {super::*, scrutiny_testing::fake::fake_model_config};

    struct MockCollector {
        id: u32,
        sequence: Arc<Mutex<Vec<u32>>>,
    }

    impl MockCollector {
        /// The default collector does not block.
        pub fn default() -> Self {
            MockCollector::new(0, Arc::new(Mutex::new(Vec::new())))
        }

        pub fn new(id: u32, sequence: Arc<Mutex<Vec<u32>>>) -> Self {
            Self { id, sequence }
        }
    }

    impl DataCollector for MockCollector {
        fn collect(&self, _: Arc<DataModel>) -> Result<()> {
            // Push the id to the shared sequence.
            self.sequence.lock().unwrap().push(self.id);
            Ok(())
        }
    }

    /// Utility function to create a temporary collector.
    fn create_scheduler() -> CollectorScheduler {
        let model = Arc::new(DataModel::new(fake_model_config()).unwrap());
        CollectorScheduler::new(model)
    }

    #[test]
    fn test_task_add_idle() {
        let mut scheduler = create_scheduler();
        let collector = Arc::new(MockCollector::default());
        let instance_id = Uuid::new_v4();
        let handle = scheduler.add(instance_id.clone(), "foo", HashSet::new(), collector.clone());
        let state = scheduler.state(&handle).unwrap();
        assert_eq!(state, CollectorState::Idle);
    }

    #[test]
    fn test_idle_all() {
        let mut scheduler = create_scheduler();
        let collector = Arc::new(MockCollector::default());
        let instance_id = Uuid::new_v4();
        scheduler.add(instance_id.clone(), "foo", HashSet::new(), collector.clone());
        assert_eq!(scheduler.all_idle(), true);
    }

    #[test]
    fn test_task_remove() {
        let mut scheduler = create_scheduler();
        let collector = Arc::new(MockCollector::default());
        let instance_id = Uuid::new_v4();
        let handle = scheduler.add(instance_id.clone(), "foo", HashSet::new(), collector.clone());
        scheduler.remove(&handle);
        assert_eq!(scheduler.state(&handle).is_none(), true);
    }

    #[test]
    fn test_task_remove_all() {
        let mut scheduler = create_scheduler();
        let collector = Arc::new(MockCollector::default());
        let instance_id = Uuid::new_v4();
        let handle = scheduler.add(instance_id.clone(), "foo", HashSet::new(), collector.clone());
        scheduler.remove_all(instance_id);
        assert_eq!(scheduler.state(&handle).is_none(), true);
    }

    #[test]
    fn test_plugin_dependency_ordering() {
        let sequence = Arc::new(Mutex::new(Vec::new()));
        let mut scheduler = create_scheduler();
        let instance_id_a = Uuid::new_v4();
        let collector_a = Arc::new(MockCollector::new(1, Arc::clone(&sequence)));
        let plugin_a_deps = HashSet::new();
        let instance_id_b = Uuid::new_v4();
        let collector_b = Arc::new(MockCollector::new(2, Arc::clone(&sequence)));
        let mut plugin_b_deps = HashSet::new();
        plugin_b_deps.insert(instance_id_a);
        scheduler.add(instance_id_a.clone(), "A", plugin_a_deps, collector_a.clone());
        scheduler.add(instance_id_b.clone(), "B", plugin_b_deps, collector_b.clone());
        scheduler.schedule().unwrap();
        assert_eq!(*sequence.lock().unwrap(), vec![1, 2]);
    }
}
