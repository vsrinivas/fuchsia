// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bt_avdtp::MediaStream,
    fuchsia_bluetooth::types::PeerId,
    futures::{
        future::{BoxFuture, Shared},
        FutureExt,
    },
    thiserror::Error,
};

use crate::codec::MediaCodecConfig;
use crate::inspect::DataStreamInspect;

#[derive(Debug, Error, Clone)]
#[non_exhaustive]
pub enum MediaTaskError {
    #[error("Configuration not supported by Builder")]
    ConfigurationNotSupported,
    #[error("Peer closed the media stream")]
    PeerClosed,
    #[error("Resources needed are already being used")]
    ResourcesInUse,
    #[error("Other Media Task Error: {}", _0)]
    Other(String),
}

impl From<bt_avdtp::Error> for MediaTaskError {
    fn from(error: bt_avdtp::Error) -> Self {
        Self::Other(format!("AVDTP Error: {}", error))
    }
}

/// MediaTaskRunners are configured with information about the media codec when either peer in a
/// conversation configures a stream endpoint.  When successfully configured, they can start
/// MediaTasks by accepting a MediaStream, which will provide or consume media on that stream until
/// dropped or stopped.
///
/// A builder that will make media task runners from requested configurations.
pub trait MediaTaskBuilder: Send + Sync {
    /// Set up to stream based on the given `codec_config` parameters.
    /// Returns a MediaTaskRunner if the configuration is supported, and
    /// MediaTaskError::ConfigurationNotSupported otherwise.
    fn configure(
        &self,
        peer_id: &PeerId,
        codec_config: &MediaCodecConfig,
        data_stream_inspect: DataStreamInspect,
    ) -> Result<Box<dyn MediaTaskRunner>, MediaTaskError>;
}

/// MediaTaskRunners represent an ability of the media system to start streaming media.
/// They are configured for a specific codec by `MediaTaskBuilder::configure`
/// Typically a MediaTaskRunner can start multiple streams without needing to be reconfigured,
/// although possibly not simultaneously.
pub trait MediaTaskRunner: Send {
    /// Start a MediaTask using the MediaStream given.
    /// If the task started, returns a MediaTask which will finish if the stream ends or an
    /// error occurs, and can be stopped using `MediaTask::stop` or by dropping the MediaTask.
    /// This can fail with MediaTaskError::ResourcesInUse if a MediaTask cannot be started because
    /// one is already running.
    fn start(&mut self, stream: MediaStream) -> Result<Box<dyn MediaTask>, MediaTaskError>;
}

/// MediaTasks represent a media stream being actively processed (sent or received from a peer).
/// They are are created by `MediaTaskRunner::start`.
/// Typically a MediaTask will run a background task that is active until dropped or
/// `MediaTask::stop` is called.
pub trait MediaTask: Send {
    /// Returns a Future that finishes when the running media task finshes for any reason.
    /// Should return a future that immediately resolves if this task is finished.
    fn finished(&mut self) -> BoxFuture<'static, Result<(), MediaTaskError>>;

    /// Returns the result if this task has finished, and None otherwise
    fn result(&mut self) -> Option<Result<(), MediaTaskError>> {
        self.finished().now_or_never()
    }

    /// Stops the task normally, signalling to all waiters Ok(()).
    /// Returns the result sent to MediaTask::finished futures, which may be different from Ok(()).
    /// When this function returns, is is good practice to ensure the MediaStream that started
    /// this task is also dropped.
    fn stop(&mut self) -> Result<(), MediaTaskError>;
}

pub mod tests {
    use super::*;

    use futures::{
        channel::{mpsc, oneshot},
        stream::StreamExt,
        Future, TryFutureExt,
    };
    use std::fmt;
    use std::sync::{Arc, Mutex};

    #[derive(Clone)]
    pub struct TestMediaTask {
        /// The PeerId that was used to make this Task
        pub peer_id: PeerId,
        /// The configuration used to make this task
        pub codec_config: MediaCodecConfig,
        /// If still started, this holds the MediaStream.
        pub stream: Arc<Mutex<Option<MediaStream>>>,
        /// Sender for the shared result future. None if already sent.
        sender: Arc<Mutex<Option<oneshot::Sender<Result<(), MediaTaskError>>>>>,
        /// Shared result future.
        result: Shared<BoxFuture<'static, Result<(), MediaTaskError>>>,
    }

    impl fmt::Debug for TestMediaTask {
        fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            f.debug_struct("TestMediaTask")
                .field("peer_id", &self.peer_id)
                .field("codec_config", &self.codec_config)
                .field("result", &self.result.clone().now_or_never())
                .finish()
        }
    }

    impl TestMediaTask {
        pub fn new(peer_id: PeerId, codec_config: MediaCodecConfig, stream: MediaStream) -> Self {
            let (sender, receiver) = oneshot::channel();
            let result = receiver
                .map_ok_or_else(
                    |_err| Err(MediaTaskError::Other(format!("Nothing sent"))),
                    |result| result,
                )
                .boxed()
                .shared();
            Self {
                peer_id,
                codec_config,
                stream: Arc::new(Mutex::new(Some(stream))),
                sender: Arc::new(Mutex::new(Some(sender))),
                result,
            }
        }

        /// Return true if the background media task is running.
        pub fn is_started(&self) -> bool {
            // The stream being held represents the task running.
            self.stream.lock().expect("stream lock").is_some()
        }

        /// End the streaming task without an external stop().
        /// Sends an optional result from the task.
        pub fn end_prematurely(&self, task_result: Option<Result<(), MediaTaskError>>) {
            let _removed_stream = self.stream.lock().expect("mutex").take();
            let mut lock = self.sender.lock().expect("sender lock");
            let sender = lock.take();
            if let (Some(result), Some(sender)) = (task_result, sender) {
                sender.send(result).expect("send ok");
            }
        }
    }

    impl MediaTask for TestMediaTask {
        fn finished(&mut self) -> BoxFuture<'static, Result<(), MediaTaskError>> {
            self.result.clone().boxed()
        }

        fn stop(&mut self) -> Result<(), MediaTaskError> {
            let _ = self.stream.lock().expect("stream lock").take();
            {
                let mut lock = self.sender.lock().expect("sender lock");
                if let Some(sender) = lock.take() {
                    let _ = sender.send(Ok(()));
                    return Ok(());
                }
            }
            // Result should be available.
            self.finished().now_or_never().unwrap()
        }
    }

    pub struct TestMediaTaskRunner {
        /// The peer_id this was started with.
        pub peer_id: PeerId,
        /// The config that this runner will start tasks for
        pub codec_config: MediaCodecConfig,
        /// The Sender that will send a clone of the started tasks to the builder.
        pub sender: mpsc::Sender<TestMediaTask>,
    }

    impl MediaTaskRunner for TestMediaTaskRunner {
        fn start(&mut self, stream: MediaStream) -> Result<Box<dyn MediaTask>, MediaTaskError> {
            let task = TestMediaTask::new(self.peer_id.clone(), self.codec_config.clone(), stream);
            // Don't particularly care if the receiver got dropped.
            let _ = self.sender.try_send(task.clone());
            Ok(Box::new(task))
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
            self.sender.lock().expect("locking").clone()
        }

        /// Gets a future that will return a handle to the next TestMediaTask that gets started
        /// from a Runner that was retrieved from this builder.
        /// The TestMediaTask, can tell you when it's started and give you a handle to the MediaStream.
        pub fn next_task(&mut self) -> impl Future<Output = Option<TestMediaTask>> + '_ {
            self.receiver.next()
        }
    }

    impl MediaTaskBuilder for mpsc::Sender<TestMediaTask> {
        fn configure(
            &self,
            peer_id: &PeerId,
            codec_config: &MediaCodecConfig,
            _data_stream_inspect: DataStreamInspect,
        ) -> Result<Box<dyn MediaTaskRunner>, MediaTaskError> {
            let task = TestMediaTaskRunner {
                peer_id: peer_id.clone(),
                codec_config: codec_config.clone(),
                sender: self.clone(),
            };
            Ok(Box::new(task))
        }
    }
}
