// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    bt_avdtp::MediaStream,
    fuchsia_bluetooth::types::PeerId,
};

use crate::codec::MediaCodecConfig;
use crate::inspect::DataStreamInspect;

/// MediaTasks are configured with information about the media codec when either peer in a
/// conversation configures a stream endpoint.  When successfully configured, a handle is provided
/// to the caller which will accept a MediaStream and provides or consume data on that stream until
/// stopped.
///
/// A builder that will make media tasks when congfigured correctly, or return an error if the
/// configuration is not possible to complete.
pub trait MediaTaskBuilder: Send + Sync {
    /// Set up to stream based on the given `codec_config` parameters.
    /// Configuring a stream task is only allowed while not started.
    fn configure(
        &self,
        peer_id: &PeerId,
        codec_config: &MediaCodecConfig,
        data_stream_inspect: DataStreamInspect,
    ) -> Result<Box<dyn MediaTask>, Error>;
}

/// StreamTask represents a task that is performed to consume or provide the data for a A2DP
/// stream.  They are usually built by the MediaTaskBuilder associated with a stream when
/// configured.
pub trait MediaTask: Send {
    /// Start streaming using the MediaStream given.
    /// This procedure will often asynchronously start a process in the background to continue
    /// streaming.
    fn start(&mut self, stream: MediaStream) -> Result<(), Error>;

    /// Stop streaming.
    /// This procedure should stop any background task started by `start`
    /// The task should be ready to re-start with the same config as it was built with.
    /// Calling stop while already stopped should not produce an error.
    fn stop(&mut self) -> Result<(), Error>;
}

pub mod tests {
    use super::*;

    use std::fmt;
    use std::sync::{Arc, Mutex};

    use futures::{channel::mpsc, stream::StreamExt, Future};

    #[derive(Clone)]
    pub struct TestMediaTask {
        /// The PeerId that was used to make this Task
        pub peer_id: PeerId,
        /// The configuration used to make this task
        pub codec_config: MediaCodecConfig,
        /// If the last task was started, this holds the MediaStream that it was started with.
        pub current_stream: Arc<Mutex<Option<MediaStream>>>,
    }

    impl fmt::Debug for TestMediaTask {
        fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            f.debug_struct("TestMediaTask")
                .field("peer_id", &self.peer_id)
                .field("codec_config", &self.codec_config)
                .field("is started", &self.is_started())
                .finish()
        }
    }

    impl TestMediaTask {
        pub fn is_started(&self) -> bool {
            self.current_stream.lock().expect("mutex").is_some()
        }
    }

    /// A TestMediaTask expects to be configured once, and then started and stopped as appropriate.
    /// It will Error if started again while started or stopped while stopped, or if it was
    /// configured multiple times.
    pub struct TestMediaTaskBuilder {
        sender: Mutex<mpsc::Sender<TestMediaTask>>,
        receiver: mpsc::Receiver<TestMediaTask>,
    }

    impl TestMediaTaskBuilder {
        pub fn new() -> Self {
            let (sender, receiver) = mpsc::channel(5);
            Self { sender: Mutex::new(sender), receiver }
        }

        /// Returns a type that implements MediaTaskBuilder.  When a MediaTask is built using
        /// configure(), it will be avialable from `next_task`.
        pub fn builder(&self) -> impl MediaTaskBuilder {
            Mutex::new(self.sender.lock().expect("locking").clone())
        }

        /// Get a handle to the FakeMediaTask, which can tell you when it's started and give you
        /// the MediaStream (for testing)
        /// The TestMediaTask exists before configuration.
        pub fn next_task(&mut self) -> impl Future<Output = Option<TestMediaTask>> + '_ {
            self.receiver.next()
        }
    }

    impl MediaTaskBuilder for Mutex<mpsc::Sender<TestMediaTask>> {
        fn configure(
            &self,
            peer_id: &PeerId,
            codec_config: &MediaCodecConfig,
            _data_stream_inspect: DataStreamInspect,
        ) -> Result<Box<dyn MediaTask>, Error> {
            let task = TestMediaTask {
                peer_id: peer_id.clone(),
                codec_config: codec_config.clone(),
                current_stream: Arc::new(Mutex::new(None)),
            };
            let _ = self.lock().expect("locking").try_send(task.clone());
            Ok(Box::new(task))
        }
    }

    impl MediaTask for TestMediaTask {
        fn start(&mut self, stream: MediaStream) -> Result<(), Error> {
            let mut lock = self.current_stream.lock().expect("mutex lock");
            if lock.is_some() {
                return Err(format_err!("Test Media Task was already started"));
            }
            *lock = Some(stream);
            Ok(())
        }

        fn stop(&mut self) -> Result<(), Error> {
            let mut lock = self.current_stream.lock().expect("mutex lock");
            *lock = None;
            Ok(())
        }
    }
}
