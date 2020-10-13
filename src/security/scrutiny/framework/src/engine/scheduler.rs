// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::collector::DataCollector,
    crate::model::model::DataModel,
    anyhow::Result,
    log::{error, info},
    serde::{Deserialize, Serialize},
    std::collections::HashMap,
    std::fmt,
    std::sync::{mpsc, Arc, Mutex},
    std::thread,
    uuid::Uuid,
};

#[derive(PartialEq, Eq, Debug, Copy, Clone, Deserialize, Serialize)]
pub enum CollectorState {
    Running,
    Idle,
    Terminated,
}

impl fmt::Display for CollectorState {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
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
    pub state: Arc<Mutex<CollectorState>>,
    sender: mpsc::Sender<CollectorMessage>,
    worker: Option<thread::JoinHandle<()>>,
}

impl CollectorInstance {
    pub fn new(instance_id: Uuid, name: String, collector: Arc<dyn DataCollector>) -> Self {
        let (sender, recv) = mpsc::channel();
        let state = Arc::new(Mutex::new(CollectorState::Idle));
        // Clone these so that we can pass references to the worker thread.
        let collector_state = Arc::clone(&state);
        let data_collector = Arc::clone(&collector);
        let worker = thread::spawn(move || loop {
            let message = recv.recv().unwrap();
            match message {
                CollectorMessage::Collect(model) => {
                    {
                        let mut state = collector_state.lock().unwrap();
                        *state = CollectorState::Running;
                    }

                    if let Err(e) = data_collector.collect(Arc::clone(&model)) {
                        error!("Collector failed with error {}", e);
                    }

                    {
                        let mut state = collector_state.lock().unwrap();
                        *state = CollectorState::Idle;
                        if let Err(e) = model.flush() {
                            error!("Store failed to flush results after collection {}", e);
                        }
                    }
                }
                CollectorMessage::Terminate => {
                    let mut state = collector_state.lock().unwrap();
                    *state = CollectorState::Terminated;
                    break;
                }
            }
        });
        Self { instance_id, name, state, sender, worker: Some(worker) }
    }

    /// Sends a message to the collector instance worker to run this collector.
    /// If the worker is currently busy this message will be queued.
    pub fn run(&self, model: Arc<DataModel>) {
        // Set the collector state to running as soon as possible in a
        // synchronous setting.
        let mut state = self.state.lock().unwrap();
        if *state != CollectorState::Terminated {
            *state = CollectorState::Running;
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
    collectors: HashMap<CollectorHandle, CollectorInstance>,
    next_handle: usize,
}

impl CollectorScheduler {
    pub fn new(model: Arc<DataModel>) -> Self {
        Self { model: model, collectors: HashMap::new(), next_handle: 1 }
    }

    /// Adds a collector associated with a particular `instance_id` to the collector
    /// scheduler. An internal handle is returned that can be used to reference
    /// the specific Collector instance in the future.
    pub fn add(
        &mut self,
        instance_id: Uuid,
        name: impl Into<String>,
        collector: Arc<dyn DataCollector>,
    ) -> CollectorHandle {
        let handle = self.next_handle;
        self.next_handle += 1;
        self.collectors.insert(handle, CollectorInstance::new(instance_id, name.into(), collector));
        handle
    }

    /// Returns a list of all the scheduled collectors.
    pub fn collectors_all(&self) -> Vec<(CollectorHandle, String)> {
        let mut collectors = Vec::new();
        for (handle, instance) in self.collectors.iter() {
            collectors.push((handle.clone(), instance.name.clone()))
        }
        collectors
    }

    /// Returns a list of all associated collector handles and names for each
    /// collector. Collector names should be unique per instance_id but not
    /// globally.
    pub fn collectors(&self, instance_id: Uuid) -> Vec<(CollectorHandle, String)> {
        let mut collectors = Vec::new();
        for (handle, instance) in self.collectors.iter() {
            if instance.instance_id == instance_id {
                collectors.push((handle.clone(), instance.name.clone()))
            }
        }
        collectors
    }

    /// Removes all `CollectorInstance` objects with a matching instance-id.
    /// This effectively unhooks all the plugins collectors.
    pub fn remove_all(&mut self, instance_id: Uuid) {
        self.collectors.retain(|_, v| v.instance_id != instance_id);
    }

    /// Removes all `CollectorInstance` objects with a matching instance-id.
    /// This effectively unhooks all the plugins collectors.
    pub fn remove(&mut self, handle: &CollectorHandle) {
        self.collectors.remove(handle);
    }

    /// Runs all of the tasks.
    pub fn schedule(&self) -> Result<()> {
        info!("Collector Scheduler: Scheduling {} Tasks", self.collectors.len());
        for (_, instance) in self.collectors.iter() {
            instance.run(Arc::clone(&self.model));
        }
        Ok(())
    }

    /// Returns true if all collectors are currently idle. This function is not
    /// thread safe.
    pub fn all_idle(&self) -> bool {
        for (_, instance) in self.collectors.iter() {
            if *instance.state.lock().unwrap() == CollectorState::Running {
                return false;
            }
        }
        return true;
    }

    /// Retrieve the collector state of a particular collector.
    pub fn state(&self, handle: &CollectorHandle) -> Option<CollectorState> {
        if let Some(collector) = self.collectors.get(handle) {
            let state = collector.state.lock().unwrap();
            Some(*state)
        } else {
            None
        }
    }
}
#[cfg(test)]
mod tests {
    use {super::*, std::sync::Barrier, tempfile::tempdir};

    struct MockCollector {
        barrier_pre: Arc<Barrier>,
        barrier_post: Arc<Barrier>,
    }

    impl MockCollector {
        /// The default collector does not block.
        pub fn default() -> Self {
            MockCollector::new(Arc::new(Barrier::new(1)), Arc::new(Barrier::new(1)))
        }

        pub fn new(barrier_pre: Arc<Barrier>, barrier_post: Arc<Barrier>) -> Self {
            Self { barrier_pre, barrier_post }
        }
    }

    impl DataCollector for MockCollector {
        fn collect(&self, _: Arc<DataModel>) -> Result<()> {
            self.barrier_pre.wait();
            self.barrier_post.wait();
            Ok(())
        }
    }

    /// Utility function to create a temporary collector.
    fn create_scheduler() -> CollectorScheduler {
        let store_dir = tempdir().unwrap();
        let uri = store_dir.into_path().into_os_string().into_string().unwrap();
        let model = Arc::new(DataModel::connect(uri).unwrap());
        CollectorScheduler::new(model)
    }

    #[test]
    fn test_task_add_idle() {
        let mut scheduler = create_scheduler();
        let collector = Arc::new(MockCollector::default());
        let instance_id = Uuid::new_v4();
        let handle = scheduler.add(instance_id.clone(), "foo", collector.clone());
        let state = scheduler.state(&handle).unwrap();
        assert_eq!(state, CollectorState::Idle);
    }

    #[test]
    fn test_idle_all() {
        let mut scheduler = create_scheduler();
        let collector = Arc::new(MockCollector::default());
        let instance_id = Uuid::new_v4();
        scheduler.add(instance_id.clone(), "foo", collector.clone());
        assert_eq!(scheduler.all_idle(), true);
    }

    #[test]
    fn test_task_add_running() {
        let mut scheduler = create_scheduler();
        let pre = Arc::new(Barrier::new(2));
        let post = Arc::new(Barrier::new(2));
        let collector = Arc::new(MockCollector::new(pre.clone(), post.clone()));
        let instance_id = Uuid::new_v4();
        let handle = scheduler.add(instance_id.clone(), "foo", collector.clone());
        let state = scheduler.state(&handle).unwrap();
        assert_eq!(state, CollectorState::Idle);
        scheduler.schedule().unwrap();
        pre.wait();
        let running_state = scheduler.state(&handle).unwrap();
        assert_eq!(running_state, CollectorState::Running);
        post.wait();
    }

    #[test]
    fn test_task_remove() {
        let mut scheduler = create_scheduler();
        let collector = Arc::new(MockCollector::default());
        let instance_id = Uuid::new_v4();
        let handle = scheduler.add(instance_id.clone(), "foo", collector.clone());
        scheduler.remove(&handle);
        assert_eq!(scheduler.state(&handle).is_none(), true);
    }

    #[test]
    fn test_task_remove_all() {
        let mut scheduler = create_scheduler();
        let collector = Arc::new(MockCollector::default());
        let instance_id = Uuid::new_v4();
        let handle = scheduler.add(instance_id.clone(), "foo", collector.clone());
        scheduler.remove_all(instance_id);
        assert_eq!(scheduler.state(&handle).is_none(), true);
    }
}
