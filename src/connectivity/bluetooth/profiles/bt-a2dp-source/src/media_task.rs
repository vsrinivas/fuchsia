// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    bt_a2dp::codec::MediaCodecConfig,
    bt_avdtp::MediaStream,
    fidl_fuchsia_media::{AudioChannelId, AudioPcmMode, PcmFormat},
    fuchsia_async as fasync,
    fuchsia_bluetooth::types::PeerId,
    fuchsia_syslog::{self, fx_log_info, fx_log_warn},
    fuchsia_trace as trace,
    futures::{
        future::{AbortHandle, Abortable},
        AsyncWriteExt, TryStreamExt,
    },
};

use crate::encoding::{EncodedStream, RtpPacketBuilder};
use crate::sources;

/// MediaTasks are configured with information about the media codec when either peer in a
/// conversation configures a stream endpoint.  When successfully configured, a handle is provided
/// to the caller which will accept a MediaStream and provides or consume data on that stream until
/// stopped.
///
/// A builder that will make media tasks when congfigured correctly, or return an error if the
/// configuration is not possible to complete.
pub trait MediaTaskBuilder {
    /// Set up to stream based on the given `codec_config` parameters.
    /// Configuring a stream task is only allowed while not started.
    fn configure(
        &self,
        peer_id: &PeerId,
        codec_config: &MediaCodecConfig,
    ) -> Result<Box<dyn MediaTask>, Error>;
}

/// StreamTask represents a task that is performed to consume or provide the data for a A2DP
/// stream.  They are usually built by the MediaTaskBuilder associated with a stream when
/// configured.
pub trait MediaTask {
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

/// SourceTaskBuilder is a MediaTaskBuilder will build `ConfiguredSourceTask`s when configured.
/// `source_type` determines where the source of audio is provided.
/// When configured, a test stream is created to confirm that it is possible to stream audio using
/// the configuration.  This stream is discarded and the stream is restarted when the resulting
/// `ConfiguredSourceTask` is started.
#[derive(Clone)]
pub struct SourceTaskBuilder {
    /// The type of source audio.
    source_type: sources::AudioSourceType,
}

impl MediaTaskBuilder for SourceTaskBuilder {
    fn configure(
        &self,
        peer_id: &PeerId,
        codec_config: &MediaCodecConfig,
    ) -> Result<Box<dyn MediaTask>, Error> {
        // all sinks must support these options
        let pcm_format = PcmFormat {
            pcm_mode: AudioPcmMode::Linear,
            bits_per_sample: 16,
            frames_per_second: 48000,
            channel_map: vec![AudioChannelId::Lf, AudioChannelId::Rf],
        };

        let source_stream = sources::build_stream(&peer_id, pcm_format.clone(), self.source_type)?;
        let _test_encoded_stream =
            EncodedStream::build(pcm_format.clone(), source_stream, codec_config)?;
        Ok(Box::new(ConfiguredSourceTask::build(
            pcm_format,
            self.source_type,
            peer_id.clone(),
            codec_config,
        )))
    }
}

impl SourceTaskBuilder {
    /// Make a new builder that will source audio from `source_type`.  See `sources::build_stream`
    /// for documentation on the types of streams that are available.
    pub fn new(source_type: sources::AudioSourceType) -> Self {
        Self { source_type }
    }
}

/// Provides audio from this to the MediaStream when started.  Streams are created and started when
/// this task is started, and destoyed when stopped.
struct ConfiguredSourceTask {
    /// The type of source audio.
    source_type: sources::AudioSourceType,
    /// Format the source audio should be produced in.
    pcm_format: PcmFormat,
    /// Id of the peer that will be receiving the stream.  Used to distinguish sources for Fuchsia
    /// Media.
    peer_id: PeerId,
    /// Configuration providing the format of encoded audio requested by the peer.
    codec_config: MediaCodecConfig,
    /// Handle used to stop the streaming task when stopped.
    stop_sender: Option<AbortHandle>,
}

impl ConfiguredSourceTask {
    /// The main streaming task. Reads encoded audio from the encoded_stream and packages into RTP
    /// packets, sending the resulting RTP packets using `media_stream`.
    async fn stream_task(
        codec_config: MediaCodecConfig,
        mut encoded_stream: EncodedStream,
        mut media_stream: MediaStream,
    ) -> Result<(), Error> {
        let frames_per_encoded = codec_config.pcm_frames_per_encoded_frame() as u32;
        let mut packet_builder = RtpPacketBuilder::new(
            codec_config.frames_per_packet() as u8,
            codec_config.rtp_frame_header().to_vec(),
        );
        loop {
            let encoded = match encoded_stream.try_next().await? {
                None => continue,
                Some(encoded) => encoded,
            };
            let packet = match packet_builder.push_frame(encoded, frames_per_encoded)? {
                None => continue,
                Some(packet) => packet,
            };

            trace::duration_begin!("bt-a2dp-source", "Media:PacketSent");
            if let Err(e) = media_stream.write(&packet).await {
                fx_log_info!("Failed sending packet to peer: {}", e);
                trace::duration_end!("bt-a2dp-source", "Media:PacketSent");
                return Ok(());
            }
            trace::duration_end!("bt-a2dp-source", "Media:PacketSent");
        }
    }

    /// Build a new ConfiguredSourceTask.  Usually only called by SourceTaskBuilder.
    /// `ConfiguredSourceTask::start` will only return errors if the settings here can not produce a
    /// stream.  No checks are done when building.
    fn build(
        pcm_format: PcmFormat,
        source_type: sources::AudioSourceType,
        peer_id: PeerId,
        codec_config: &MediaCodecConfig,
    ) -> Self {
        Self {
            pcm_format,
            source_type,
            peer_id,
            codec_config: codec_config.clone(),
            stop_sender: None,
        }
    }
}

impl MediaTask for ConfiguredSourceTask {
    fn start(&mut self, stream: MediaStream) -> Result<(), Error> {
        if self.stop_sender.is_some() {
            return Err(format_err!("Already started, can't start again"));
        }
        let source_stream =
            sources::build_stream(&self.peer_id, self.pcm_format.clone(), self.source_type)?;
        let encoded_stream =
            EncodedStream::build(self.pcm_format.clone(), source_stream, &self.codec_config)?;
        let stream_fut = Self::stream_task(self.codec_config.clone(), encoded_stream, stream);
        let (stop_handle, stop_registration) = AbortHandle::new_pair();
        let stream_fut = Abortable::new(stream_fut, stop_registration);
        fasync::spawn(async move {
            trace::instant!("bt-a2dp-source", "Media:Start", trace::Scope::Thread);
            match stream_fut.await {
                Err(_) | Ok(Ok(())) => {}
                Ok(Err(e)) => fx_log_warn!("ConfiguredSourceTask ended with error: {:?}", e),
            };
        });
        self.stop_sender = Some(stop_handle);
        Ok(())
    }

    fn stop(&mut self) -> Result<(), Error> {
        trace::instant!("bt-a2dp-source", "Media:Stop", trace::Scope::Thread);
        self.stop_sender.take().map(|x| x.abort());
        Ok(())
    }
}

impl Drop for ConfiguredSourceTask {
    fn drop(&mut self) {
        let _ = self.stop();
    }
}

#[cfg(test)]
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
