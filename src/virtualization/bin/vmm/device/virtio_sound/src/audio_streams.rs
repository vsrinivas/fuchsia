// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::notification::Notification,
    crate::reply::*,
    crate::sequencer,
    crate::sequencer::Sequencer,
    crate::throttled_log,
    crate::wire,
    crate::wire_convert,
    anyhow::{anyhow, Context, Error},
    async_trait::async_trait,
    fidl_fuchsia_media,
    fuchsia_async::{DurationExt, TimeoutExt},
    fuchsia_zircon::{self as zx, AsHandleRef},
    futures::FutureExt,
    futures::TryStreamExt,
    mapped_vmo::Mapping,
    scopeguard,
    std::cell::RefCell,
    std::collections::{HashMap, VecDeque},
    std::ffi::CString,
    std::ops::Range,
    tracing,
};

/// Parameters needed to construct an AudioStream.
#[derive(Debug, Copy, Clone)]
pub struct AudioStreamParams {
    pub stream_type: fidl_fuchsia_media::AudioStreamType,
    pub buffer_bytes: usize,
    pub period_bytes: usize,
}

/// Wraps a connection to the Fuchsia audio service. This abstract API covers
/// both output streams ("renderers") and input streams ("capturers").
#[async_trait(?Send)]
pub trait AudioStream<'a> {
    /// Creates a connection to the Fuchsia audio service.
    /// The stream must be disconnected.
    async fn connect(&self, params: AudioStreamParams) -> Result<(), Error>;

    /// Closes an existing connection.
    /// The stream must be connected.
    async fn disconnect(&self) -> Result<(), Error>;

    /// Starts streaming audio. Must have a valid connection.
    /// The stream must be connected.
    async fn start(&self) -> Result<(), Error>;

    /// Stops streaming audio. Must have a valid connection.
    /// The stream must be connected.
    async fn stop(&self) -> Result<(), Error>;

    /// Callback invoked each time a buffer of audio data is received from the guest driver.
    /// For output streams, the buffer contains audio to play.
    /// For input streams, the buffer is empty and should be filled.
    ///
    /// This method consumes `chain` and fully writes the response.
    ///
    /// This method will release `lock` just before the packet is transferred over FIDL. This
    /// ensures that FIDL packet delivery happens in the correct order while allowing us to
    /// wait concurrently on multiple pending packets.
    async fn on_receive_data<'b, 'c>(
        &self,
        chain: ReadableChain<'b, 'c>,
        lock: sequencer::Lock,
    ) -> Result<(), Error>;

    /// Performs background work. The returned future may never complete. To stop performing
    /// background work, drop the returned future.
    async fn do_background_work(&self) -> Result<(), Error>;
}

/// Construct an AudioStream that renders audio.
pub fn create_audio_output<'a>(
    audio: &'a fidl_fuchsia_media::AudioProxy,
) -> Box<dyn AudioStream<'a> + 'a> {
    // There is exactly one bg job per connect(), so a buffer size of 1 should be sufficient.
    let (send, recv) = tokio::sync::mpsc::channel(1);
    Box::new(AudioOutput {
        audio,
        conn: RefCell::new(None),
        bg_jobs_send: RefCell::new(send),
        bg_jobs_recv: RefCell::new(recv),
    })
}

/// Construct an AudioStream that captures audio.
pub fn create_audio_input<'a>(
    audio: &'a fidl_fuchsia_media::AudioProxy,
) -> Box<dyn AudioStream<'a> + 'a> {
    let (s, r) = tokio::sync::watch::channel(false);
    Box::new(AudioInput {
        audio,
        inner: RefCell::new(None),
        running_send: s,
        running_recv: r,
        reply_sequencer: RefCell::new(Sequencer::new()),
    })
}

/// Represents a payload buffer used by an AudioRenderer or AudioCapturer.
/// Each PayloadBuffer is subdivided into fixed-sized packets.
///
/// Note on fragmentation: The virtio-sound spec has the following text:
///
///   5.14.6.8.1.2 Driver Requirements: Output Stream
///   The driver SHOULD populate the tx queue with period_bytes sized buffers.
///   The only exception is the end of the stream [which can be smaller].
///
///   5.14.6.8.2.2 Driver Requirements: Input Stream
///   The driver SHOULD populate the rx queue with period_bytes sized empty
///   buffers before starting the stream.
///
/// If both of these suggestions are followed, then every buffer received from
/// the driver will have size period_bytes (except for the very last packet
/// just before a STOP command). Packet allocation is simple: we pre-populate
/// a free list of fixed-sized packets.
///
/// Linux follows both suggestions. Hence, for simplicity, we assume that all
/// drivers behave like Linux's. In general, drivers don't need to behave like
/// Linux, so we can theoretically run into problems:
///
/// * The driver might send a buffer larger than period_bytes. If this happens,
///   we currently reject the buffer.
///
/// * The driver might send many buffers smaller than period_bytes. If this
///   happens, we may run out of packets and be forced to reject the buffer.
///
/// TODO(fxbug.dev/90032): We can support the general case if necessary.
struct PayloadBuffer {
    mapping: Mapping,
    packets_avail: VecDeque<Range<usize>>, // ranges with mapping
}

impl PayloadBuffer {
    /// Create a PayloadBuffer containing `buffer_bytes` / `packet_bytes` packets.
    pub fn new(
        buffer_bytes: usize,
        packet_bytes: usize,
        name: &str,
    ) -> Result<(Self, zx::Vmo), anyhow::Error> {
        // 5.14.6.6.3.1 Device Requirements: Stream Parameters
        // "If the device has an intermediate buffer, its size MUST be no less
        // than the specified buffer_bytes value."
        let cname = CString::new(name)?;
        let vmo = zx::Vmo::create(buffer_bytes as u64).context("failed to allocate VMO")?;
        vmo.set_name(&cname)?;
        let flags = zx::VmarFlags::PERM_READ
            | zx::VmarFlags::PERM_WRITE
            | zx::VmarFlags::MAP_RANGE
            | zx::VmarFlags::REQUIRE_NON_RESIZABLE;
        let mut pb = PayloadBuffer {
            mapping: Mapping::create_from_vmo(&vmo, buffer_bytes, flags)
                .context("failed to create Mapping from VMO")?,
            packets_avail: VecDeque::new(),
        };

        // Initially, all packets are available.
        let num_packets = buffer_bytes / packet_bytes;
        for k in 0..num_packets {
            let offset = k * packet_bytes;
            pb.packets_avail.push_back(Range { start: offset, end: offset + packet_bytes });
        }

        Ok((pb, vmo))
    }
}

/// State common to AudioRenderer and AudioCapturer connections.
/// Borrowed references to this object should not be held across an await.
//
// In the data path between virtio-sound driver, virtio-sound device and AudioRenderer/Capturer,
// we use the term "buffers" when referring to the data transport between the virtio-sound driver
// and virtio-sound device. We use the term "packets" when referring to the data transport between
// virtio-sound device and AudioRenderer/Capturer.
// The AudioRenderer/Capturer APIs also have to an overall payload buffer, but we always include
// "payload" when referring to that entity.
struct AudioStreamConn<T> {
    fidl_proxy: T,
    params: AudioStreamParams,
    payload_buffer: PayloadBuffer,
    lead_time: tokio::sync::watch::Receiver<zx::Duration>,
    buffers_received: u32,
    packets_pending: HashMap<u32, Notification>, // signalled when we're done with the packet
    closing: Notification,                       // signalled when we've started disconnecting
}

impl<T> AudioStreamConn<T> {
    // Returns the current latency of this stream, in bytes.
    fn latency_bytes(&self) -> u32 {
        let lead_time = *self.lead_time.borrow();
        // bytes_for_duration fails only if the lead_time is negative, which should
        // never happen unless audio_core is buggy.
        match wire_convert::bytes_for_duration(lead_time, self.params.stream_type) {
            // The virtio-sound wire protocol encodes latency_bytes with u32. In practice that
            // gives a maximum lead time of 5592s (93 minutes) for a 96kHz mono stream with 4-byte
            // (float) samples. This is more than enough for all practical scenarios. If this cast
            // fails, there's probably a bug.
            Some(bytes) => match num_traits::cast::cast::<usize, u32>(bytes) {
                Some(bytes) => bytes,
                None => {
                    throttled_log::warning!(
                        "got unexpectedly large lead time: {}ns ({} bytes)",
                        lead_time.into_nanos(),
                        bytes
                    );
                    u32::max_value()
                }
            },
            None => {
                throttled_log::warning!("got negative lead time: {}ns", lead_time.into_nanos());
                0
            }
        }
    }

    fn validate_buffer<'b, 'c>(&self, data_buffer_size: usize) -> Result<(), Error> {
        // See comments at PayloadBuffer: we assume the total size of each data buffer
        // is no bigger than period_bytes, allowing each buffer to fit in one packet.
        if data_buffer_size > self.params.period_bytes {
            return Err(anyhow!(
                "received buffer with {} bytes, want < {} bytes",
                data_buffer_size,
                self.params.period_bytes
            ));
        }

        // Must have an integral number of frames per packet.
        let frame_bytes = wire_convert::bytes_per_frame(self.params.stream_type);
        if data_buffer_size % frame_bytes != 0 {
            return Err(anyhow!(
                "received buffer with {} bytes, want multiple of frame size ({} bytes)",
                data_buffer_size,
                frame_bytes,
            ));
        }

        Ok(())
    }
}

type AudioOutputConn = AudioStreamConn<fidl_fuchsia_media::AudioRendererProxy>;
type AudioInputConn = AudioStreamConn<fidl_fuchsia_media::AudioCapturerProxy>;

/// Wraps a connection to a FIDL AudioRenderer.
struct AudioOutput<'a> {
    audio: &'a fidl_fuchsia_media::AudioProxy,
    conn: RefCell<Option<AudioOutputConn>>,
    bg_jobs_recv: RefCell<tokio::sync::mpsc::Receiver<AudioOutputBgJob>>,
    bg_jobs_send: RefCell<tokio::sync::mpsc::Sender<AudioOutputBgJob>>,
}

struct AudioOutputBgJob {
    event_stream: fidl_fuchsia_media::AudioRendererEventStream,
    lead_time: tokio::sync::watch::Sender<zx::Duration>,
}

#[async_trait(?Send)]
impl<'a> AudioStream<'a> for AudioOutput<'a> {
    async fn connect(&self, mut params: AudioStreamParams) -> Result<(), Error> {
        // Allocate a payload buffer.
        let (payload_buffer, payload_vmo) =
            PayloadBuffer::new(params.buffer_bytes, params.period_bytes, "AudioRendererBuffer")
                .context("failed to create payload buffer for AudioRenderer")?;

        // Configure the renderer.
        let (client_end, server_end) =
            fidl::endpoints::create_endpoints::<fidl_fuchsia_media::AudioRendererMarker>()?;
        self.audio.create_audio_renderer(server_end)?;
        let fidl_proxy = client_end.into_proxy()?;
        fidl_proxy.set_usage(fidl_fuchsia_media::AudioRenderUsage::Media)?;
        fidl_proxy.set_pcm_stream_type(&mut params.stream_type)?;
        fidl_proxy.enable_min_lead_time_events(true)?;
        fidl_proxy.add_payload_buffer(0, payload_vmo)?;

        // Start a bg job to watch for lead time changes.
        let (lead_time_send, mut lead_time_recv) =
            tokio::sync::watch::channel(zx::Duration::from_nanos(0));
        if let Err(_) = self
            .bg_jobs_send
            .borrow_mut()
            .send(AudioOutputBgJob {
                event_stream: fidl_proxy.take_event_stream(),
                lead_time: lead_time_send,
            })
            .await
        {
            return Err(anyhow!("AudioOutput could not send BgJob: channel closed"));
        }

        // Wait until the renderer is configured. This is signalled by a non-zero lead time.
        loop {
            let deadline = zx::Duration::from_seconds(5).after_now();
            if lead_time_recv.changed().map(|res| res.is_ok()).on_timeout(deadline, || false).await
            {
                let t = lead_time_recv.borrow_and_update();
                if *t > zx::Duration::from_nanos(0) {
                    // TODO(fxbug.dev/101220): temporary for debugging
                    throttled_log::info!(
                        "AudioOutput received non-zero lead_time {} ns",
                        t.into_nanos()
                    );
                    break;
                }
            } else {
                return Err(anyhow!(
                    "failed to create AudioRenderer: did not receive lead time before timeout"
                ));
            }
        }

        *self.conn.borrow_mut() = Some(AudioOutputConn {
            fidl_proxy,
            params,
            payload_buffer,
            lead_time: lead_time_recv,
            buffers_received: 0,
            packets_pending: HashMap::new(),
            closing: Notification::new(),
        });
        Ok(())
    }

    async fn disconnect(&self) -> Result<(), Error> {
        // 5.14.6.6.5.1 Device Requirements: Stream Release
        // - The device MUST complete all pending I/O messages for the specified stream ID.
        // - The device MUST NOT complete the control request while there are pending I/O
        //   messages for the specified stream ID.
        //
        // To implement this requirement, we set the `closing` notification, which will cause
        // all pending and future on_receive_data calls to fail with an IO_ERR. Then, we wait
        // until all pending on_receive_data calls have completed before disconnecting.
        let futs = match &mut *self.conn.borrow_mut() {
            Some(conn) => {
                conn.closing.set();
                // TODO(fxbug.dev/101220): temporary for debugging
                throttled_log::info!("AudioOutput is disconnecting");
                futures::future::join_all(
                    conn.packets_pending.iter().map(|(_, n)| n.clone().when_set()),
                )
            }
            None => panic!("called disconnect() without a connection"),
        };
        futs.await;
        // TODO(fxbug.dev/101220): temporary for debugging
        throttled_log::info!("AudioOutput disconnect has completed");
        // Writing None here will deallocate all per-connection state, including the
        // FIDL channel and the payload buffer mapping.
        *self.conn.borrow_mut() = None;
        Ok(())
    }

    async fn start(&self) -> Result<(), Error> {
        let fut = match &*self.conn.borrow() {
            Some(conn) => {
                // TODO(fxbug.dev/101220): temporary for debugging
                throttled_log::info!("AudioOutput start calling renderer.play");

                conn.fidl_proxy
                    .play(fidl_fuchsia_media::NO_TIMESTAMP, fidl_fuchsia_media::NO_TIMESTAMP)
            }
            None => panic!("AudioOutput called start without a connection"),
        };
        // Relinquish our borrow of conn while waiting for the renderer.play() response.
        fut.await?;

        // TODO(fxbug.dev/101220): temporary for debugging
        throttled_log::info!("AudioOutput start (renderer.play) has completed");

        Ok(())
    }

    async fn stop(&self) -> Result<(), Error> {
        // Deterministically return all packets after we pause.
        let fut = match &*self.conn.borrow() {
            Some(conn) => {
                // TODO(fxbug.dev/101220): temporary for debugging
                throttled_log::info!("AudioOutput stop calling pause_no_reply+discard_all_packets");

                conn.fidl_proxy.pause_no_reply()?;
                conn.fidl_proxy.discard_all_packets()
            }
            None => panic!("AudioOutput called stop without a connection"),
        };
        // Relinquish our borrow of conn while waiting for the discard_all_packets() response.
        fut.await?;

        // TODO(fxbug.dev/101220): temporary for debugging
        throttled_log::info!("AudioOutput stop (renderer.discard_all_packets) has completed");

        Ok(())
    }

    async fn on_receive_data<'b, 'c>(
        &self,
        mut chain: ReadableChain<'b, 'c>,
        lock: sequencer::Lock,
    ) -> Result<(), Error> {
        let mut conn_option = self.conn.borrow_mut();
        let conn = match &mut *conn_option {
            Some(conn) => conn,
            None => panic!("AudioOutput called on_receive_data() without a connection"),
        };

        if conn.closing.get() {
            tracing::warn!("AudioOutput received buffer while connection is closing");
            return reply_txq::err(chain, wire::VIRTIO_SND_S_IO_ERR, 0);
        }

        if let Err(err) = conn.validate_buffer(chain.remaining()?.bytes) {
            tracing::warn!("AudioOutput validate_buffer failed: {}", err);
            return reply_txq::err(chain, wire::VIRTIO_SND_S_BAD_MSG, conn.latency_bytes());
        }

        let buffer_size = chain.remaining()?.bytes;
        let packet_range = match conn.payload_buffer.packets_avail.pop_front() {
            Some(packet) => packet,
            None => {
                tracing::warn!(
                    "AudioOutput ran out of available packet space (buffer from driver has size {} bytes, period is {} bytes)",
                    buffer_size,
                    conn.params.period_bytes
                );
                return reply_txq::err(chain, wire::VIRTIO_SND_S_IO_ERR, conn.latency_bytes());
            }
        };

        // Always return this packet when we're done.
        scopeguard::defer!(
            // Need to reacquire this borrow since we don't hold it across the await.
            match &mut *self.conn.borrow_mut() {
                Some(conn) => {
                    conn.payload_buffer.packets_avail.push_back(packet_range.clone());
                    ()
                }
                None => {
                    // TODO(fxbug.dev/101220): temporary for debugging
                    throttled_log::info!(
                    "conn.borrow on packet completion (to add to packets_avail) failed - disconnected with outstanding packets");
                    // ignore: disconnected before our await completed
                    ()
                }
            }
        );

        // Copy the data into the packet.
        let packet = conn.payload_buffer.mapping.slice_mut(packet_range.clone());
        let mut buffer_offset = 0;

        while let Some(range) = chain.next().transpose()? {
            // This fails only if the buffer is empty, in which case we can ignore the buffer.
            let ptr = match range.try_ptr::<u8>() {
                Some(ptr) => ptr,
                None => continue,
            };

            // Cast to a &[u8], then copy to the packet.
            //
            // SAFETY: The range comes from a chain, so by construction it refers to a valid
            // range of memory and we are appropriately synchronized with a well-behaved driver.
            // `try_ptr` verifies the pointer is correctly aligned. The worst a buggy driver
            // could do is write to this buffer concurrently, causing us to read garbage bytes,
            // which would produce garbage audio at the speaker.
            let buf = unsafe { std::slice::from_raw_parts(ptr, range.len()) };
            buffer_offset += packet.write_at(buffer_offset, buf);
        }

        // A notification that is signalled when the packet is done.
        let when_done = Notification::new();
        scopeguard::defer!(when_done.set());

        // Add to the pending set.
        let packet_id = conn.buffers_received;
        conn.buffers_received += 1;
        conn.packets_pending.insert(packet_id, when_done.clone());
        scopeguard::defer!(
            // Need to reacquire this borrow since we don't hold it across the await.
            match &mut *self.conn.borrow_mut() {
                Some(conn) => {
                    conn.packets_pending.remove(&packet_id);
                    ()
                }
                None => {
                    // TODO(fxbug.dev/101220): temporary for debugging
                    throttled_log::info!(
                  "conn.borrow on completion (removing from packets_pending) failed - disconnected with outstanding packets?");
                    // ignore: disconnected before our await completed
                    ()
                }
            }
        );

        // Send this packet.
        let resp_fut = conn.fidl_proxy.send_packet(&mut fidl_fuchsia_media::StreamPacket {
            pts: fidl_fuchsia_media::NO_TIMESTAMP,
            payload_buffer_id: 0,
            payload_offset: packet_range.start as u64,
            payload_size: buffer_offset as u64, // total bytes copied from buffer to packet
            flags: 0,
            buffer_config: 0,
            stream_segment_id: 0,
        });

        // A future notified if the connection starts to close.
        let closing = conn.closing.clone();
        let closing_fut = closing.when_set();

        // Before we await, drop these borrowed refs so that other async tasks can borrow
        // the conn while we're waiting.
        std::mem::drop(packet);
        #[allow(clippy::drop_ref)] // TODO(fxbug.dev/95075)
        std::mem::drop(conn);
        std::mem::drop(conn_option);

        // Before we await, release the sequencer lock so that other packets can be processed
        // concurrently.
        std::mem::drop(lock);

        // Wait until the packet is complete or the connection is closed.
        // We wait until the packet is complete so the return of this packet can act
        // as an "elapsed period" notification. See discussion in 5.14.6.8.
        futures::select! {
            resp = resp_fut.fuse() =>
                match &*self.conn.borrow() {
                    Some(conn) => match resp {
                        Ok(_) => reply_txq::success(chain, conn.latency_bytes())?,
                        Err(err) =>{
                            tracing::warn!("AudioRenderer SendPacket[{}] failed: {}", conn.buffers_received, err);
                            reply_txq::err(chain, wire::VIRTIO_SND_S_IO_ERR, conn.latency_bytes())?
                        },
                    },
                    None => {
                        // TODO(fxbug.dev/101220): temporary for debugging
                        throttled_log::info!("AudioRenderer disconnected before the packet completed (1)");
                        // Disconnected before the packet completed. We may hit this case instead
                        // of the closing_fut case below because both futures can be ready at the
                        // same time and select! is not deterministic.
                        reply_txq::err(chain, wire::VIRTIO_SND_S_IO_ERR, 0)?
                    },
                },
            _ = closing_fut.fuse() => {
                // TODO(fxbug.dev/101220): temporary for debugging
                throttled_log::info!("AudioRenderer disconnected before the packet completed (2)");
                // Disconnected before the packet completed.
                reply_txq::err(chain, wire::VIRTIO_SND_S_IO_ERR, 0)?
            }
        };

        Ok(())
    }

    async fn do_background_work(&self) -> Result<(), Error> {
        while let Some(mut job) = self.bg_jobs_recv.borrow_mut().recv().await {
            loop {
                futures::select! {
                    event = job.event_stream.try_next().fuse() =>
                        match event {
                            Ok(event) => {
                                use fidl_fuchsia_media::AudioRendererEvent::OnMinLeadTimeChanged;
                                match event {
                                    Some(OnMinLeadTimeChanged { min_lead_time_nsec }) => {
                                        // Include our deadline in the lead time.
                                        // This is an upper-bound: see discussion in fxbug.dev/90564.
                                        let lead_time = zx::Duration::from_nanos(min_lead_time_nsec)
                                            + super::DEADLINE_PROFILE.period;
                                        // TODO(fxbug.dev/101220): temporary for debugging
                                        throttled_log::info!("AudioRenderer lead_time {} ns", lead_time.into_nanos());
                                        job.lead_time.send(lead_time)?;
                                    },
                                    None => {
                                        // TODO(fxbug.dev/101220): temporary for debugging
                                        throttled_log::info!("AudioRenderer lead_time FIDL connection closed by peer");
                                        // FIDL connection closed by peer.
                                        break;
                                    }
                                }
                            },
                            Err(err) => {
                                match err {
                                    fidl::Error::ClientChannelClosed{..} => (),
                                    _ => tracing::warn!("AudioRenderer event stream: unexpected error: {}", err),
                                }
                                // TODO(fxbug.dev/101220): temporary for debugging
                                throttled_log::info!("AudioRenderer lead_time FIDL connection broken");
                                // FIDL connection broken.
                                break;
                            }
                    },
                    _ = job.lead_time.closed().fuse() => {
                        // TODO(fxbug.dev/101220): temporary for debugging
                        throttled_log::info!("AudioRenderer has been disconnected");
                        // The AudioOutput has been disconnected.
                        break;
                    },
                }
            }
        }

        Ok(())
    }
}

/// Wraps a connection to a FIDL AudioCapturer.
struct AudioInput<'a> {
    audio: &'a fidl_fuchsia_media::AudioProxy,
    inner: RefCell<Option<AudioInputInner>>,
    running_send: tokio::sync::watch::Sender<bool>,
    running_recv: tokio::sync::watch::Receiver<bool>,
    reply_sequencer: RefCell<Sequencer>,
}

struct AudioInputInner {
    conn: AudioInputConn,
    lead_time: tokio::sync::watch::Sender<zx::Duration>,
}

#[async_trait(?Send)]
impl<'a> AudioStream<'a> for AudioInput<'a> {
    async fn connect(&self, mut params: AudioStreamParams) -> Result<(), Error> {
        // Allocate a payload buffer.
        let (payload_buffer, payload_vmo) =
            PayloadBuffer::new(params.buffer_bytes, params.period_bytes, "AudioCapturerBuffer")
                .context("failed to create payload buffer for AudioCapturer")?;

        // Configure the capturer.
        // Must call SetPcmStreamType before AddPayloadBuffer.
        let (client_end, server_end) =
            fidl::endpoints::create_endpoints::<fidl_fuchsia_media::AudioCapturerMarker>()?;
        self.audio.create_audio_capturer(server_end, false /* not loopback */)?;
        let fidl_proxy = client_end.into_proxy()?;
        fidl_proxy.set_usage(fidl_fuchsia_media::AudioCaptureUsage::Foreground)?;
        fidl_proxy.set_pcm_stream_type(&mut params.stream_type)?;
        fidl_proxy.add_payload_buffer(0, payload_vmo)?;

        let (lead_time_send, lead_time_recv) =
            tokio::sync::watch::channel(zx::Duration::from_nanos(0));

        *self.inner.borrow_mut() = Some(AudioInputInner {
            conn: AudioInputConn {
                fidl_proxy,
                params,
                payload_buffer,
                lead_time: lead_time_recv,
                buffers_received: 0,
                packets_pending: HashMap::new(),
                closing: Notification::new(),
            },
            lead_time: lead_time_send,
        });
        Ok(())
    }

    async fn disconnect(&self) -> Result<(), Error> {
        // 5.14.6.6.5.1 Device Requirements: Stream Release
        // - The device MUST complete all pending I/O messages for the specified stream ID.
        // - The device MUST NOT complete the control request while there are pending I/O
        //   messages for the specified stream ID.
        //
        // To implement this requirement, we set the `closing` notification, which will cause
        // all pending and future on_receive_data calls to fail with an IO_ERR. Then, we wait
        // until all pending on_receive_data calls have completed before disconnecting.
        let futs = match &mut *self.inner.borrow_mut() {
            Some(AudioInputInner { conn, .. }) => {
                conn.closing.set();
                futures::future::join_all(
                    conn.packets_pending.iter().map(|(_, n)| n.clone().when_set()),
                )
            }
            None => panic!("AudioInput called disconnect() without a connection"),
        };
        futs.await;
        // Writing None here will deallocate all per-connection state, including the
        // FIDL channel and the payload buffer mapping.
        *self.inner.borrow_mut() = None;
        Ok(())
    }

    async fn start(&self) -> Result<(), Error> {
        // This will cause on_receive_data to start sending capture requests.
        self.running_send.send(true)?;
        Ok(())
    }

    async fn stop(&self) -> Result<(), Error> {
        // This will cause on_receive_data to stop sending capture requests.
        self.running_send.send(false)?;
        Ok(())
    }

    async fn on_receive_data<'b, 'c>(
        &self,
        chain: ReadableChain<'b, 'c>,
        lock: sequencer::Lock,
    ) -> Result<(), Error> {
        let mut inner_option = self.inner.borrow_mut();
        let conn = match &mut *inner_option {
            Some(AudioInputInner { conn, .. }) => conn,
            None => panic!("AudioInput called on_receive_data() without a connection"),
        };

        let mut chain = WritableChain::from_readable(chain)?;
        let buffer_size = reply_rxq::buffer_size(&chain)?;
        if let Err(err) = conn.validate_buffer(buffer_size) {
            tracing::warn!("{}", err);
            return reply_rxq::err_from_writable(
                chain,
                wire::VIRTIO_SND_S_BAD_MSG,
                conn.latency_bytes(),
            );
        }

        let closing = conn.closing.clone();
        let mut running = self.running_recv.clone();

        // Before we await, drop these borrowed refs so that other async tasks can borrow
        // the conn while we're waiting.
        #[allow(clippy::drop_ref)] // TODO(fxbug.dev/95075)
        std::mem::drop(conn);
        std::mem::drop(inner_option);

        // Wait until capture starts or the connection closes.
        loop {
            let closing = closing.clone();
            futures::select! {
                res = running.changed().fuse() => {
                    // Cannot happen: Error means the sender was dropped, but self
                    // must outlive the sender.
                    res.expect("sender to not be dropped");
                    if *running.borrow_and_update() {
                        break;
                    } else {
                        continue;
                    }
                },
                _ = closing.when_set().fuse() => {
                    // Disconnected before capture started.
                    return reply_rxq::err_from_writable(chain, wire::VIRTIO_SND_S_IO_ERR, 0);
                }
            };
        }

        let mut inner_option = self.inner.borrow_mut();
        let conn = match &mut *inner_option {
            Some(AudioInputInner { conn, .. }) => conn,
            None => {
                // This can happen when the driver starts capture then immediately disconnects:
                // We may hit this case instead of the closing_fut case above because both futures
                // can be ready at the same time and select! is not deterministic.
                return reply_rxq::err_from_writable(chain, wire::VIRTIO_SND_S_IO_ERR, 0);
            }
        };

        // Get a packet to capture into.
        let packet_range = match conn.payload_buffer.packets_avail.pop_front() {
            Some(range) => range,
            None => {
                tracing::warn!(
                    "AudioInput ran out of packets (latest buffer has size {} bytes, period is {} bytes)",
                    buffer_size,
                    conn.params.period_bytes
                );
                return reply_rxq::err_from_writable(
                    chain,
                    wire::VIRTIO_SND_S_IO_ERR,
                    conn.latency_bytes(),
                );
            }
        };

        // Always return this packet when we're done.
        scopeguard::defer!(
            // Need to reacquire this borrow since we don't hold it across the await.
            match &mut *self.inner.borrow_mut() {
                Some(AudioInputInner { conn, .. }) => {
                    conn.payload_buffer.packets_avail.push_back(packet_range.clone());
                    ()
                }
                None => (), // ignore: disconnected while before our await completed
            }
        );

        // A notification that is signalled when the packet is done.
        let when_done = Notification::new();
        scopeguard::defer!(when_done.set());

        // Add to the pending set.
        let packet_id = conn.buffers_received;
        conn.buffers_received += 1;
        conn.packets_pending.insert(packet_id, when_done.clone());
        scopeguard::defer!(
            // Need to reacquire this borrow since we don't hold it across the await.
            match &mut *self.inner.borrow_mut() {
                Some(AudioInputInner { conn, .. }) => {
                    conn.packets_pending.remove(&packet_id);
                    ()
                }
                None => (), // ignore: disconnected while before our await completed
            }
        );

        // Capture data into this packet.
        let bytes_per_frame = wire_convert::bytes_per_frame(conn.params.stream_type);
        let resp_fut = conn.fidl_proxy.capture_at(
            0,                                             // payload_buffer_id
            (packet_range.start / bytes_per_frame) as u32, // offset in frames
            (buffer_size / bytes_per_frame) as u32,        // num frames
        );

        // Before we await, drop these borrowed refs so that other async tasks can borrow
        // the conn while we're waiting.
        #[allow(clippy::drop_ref)] // TODO(fxbug.dev/95075)
        std::mem::drop(conn);
        std::mem::drop(inner_option);

        // We need to send CaptureAt requests in order, then process those replies in
        // the same order. Holding `lock` ensures that we send requests in the correct order.
        // We release `lock` here so that other packets can be queued concurrently.
        //
        // To ensure we process replies in the correct order, we obtain a reply sequencer token.
        // This reply token is often not necessary in practice: the audio server will respond to
        // CaptureAt calls in the expected order, at the rate of one response per period. Hence,
        // as long as our thread is scheduled in a timely manner, there should be exactly one
        // outsanding reply at any time. However, there may be multiple replies if we are scheduled
        // late, and also in tests, which don't use real time and hence can reply to multiple
        // CaptureAt calls simultaneously.
        let reply_sequence = self.reply_sequencer.borrow_mut().next();
        std::mem::drop(lock);

        // Wait until the packet capture completes or the connection is closed.
        let resp = {
            let closing = closing.clone();
            futures::select! {
                resp = resp_fut.fuse() =>
                    match &*self.inner.borrow() {
                        Some(AudioInputInner {conn, ..}) => match resp {
                            Ok(resp) => resp,
                            Err(err) =>{
                                tracing::warn!("AudioInput failed to capture packet: {}", err);
                                return reply_rxq::err_from_writable(chain, wire::VIRTIO_SND_S_IO_ERR, conn.latency_bytes());
                            },
                        },
                        None => {
                            // Disconnected before the packet completed. We may hit this case
                            // instead of the closing_fut case below because both futures can
                            // be ready at the same time and select! is not deterministic.
                            return reply_rxq::err_from_writable(chain, wire::VIRTIO_SND_S_IO_ERR, 0);
                        },
                    },
                _ = closing.when_set().fuse() => {
                    // Disconnected before the packet completed.
                    return reply_rxq::err_from_writable(chain, wire::VIRTIO_SND_S_IO_ERR, 0);
                }
            }
        };

        // Wait for our turn to reply.
        // As discussed above, in most cases this should be instantaneous.
        let _lock = futures::select! {
            lock = reply_sequence.wait_turn().fuse() => lock,
            _ = closing.when_set().fuse() => {
                // Disconnected.
                return reply_rxq::err_from_writable(chain, wire::VIRTIO_SND_S_IO_ERR, 0);
            }
        };

        // Validate that the response matches our expected packet.
        if resp.payload_buffer_id != 0
            || resp.payload_offset != (packet_range.start as u64)
            || resp.payload_size != (buffer_size as u64)
        {
            tracing::warn!("skipping captured packet {:?}, expected {{.payload_buffer_id=0, .payload_offset={}, .payload_size={}}}",
                resp, packet_range.start, buffer_size);
            return Ok(());
        }

        let inner_option = self.inner.borrow();
        let inner = inner_option.as_ref().unwrap();

        // Copy the captured packet into the audio buffer.
        let mut buffer_offset = 0;
        let packet = inner.conn.payload_buffer.mapping.slice(packet_range.clone());

        while let Some(range) = chain.next_with_limit(buffer_size - buffer_offset).transpose()? {
            // This fails only if the buffer is empty, in which case we can ignore the buffer.
            let ptr = match range.try_mut_ptr::<u8>() {
                Some(ptr) => ptr,
                None => continue,
            };

            // Cast to a &mut [u8], then copy from the packet.
            //
            // SAFETY: The range comes from a chain, so by construction it refers to a valid
            // range of memory and we are appropriately synchronized with a well-behaved driver.
            // `try_ptr_mut` verifies the pointer is correctly aligned. The worst a buggy driver
            // could do is write to this buffer concurrently, which may garble the captured audio.
            let buf = unsafe { std::slice::from_raw_parts_mut(ptr, range.len()) };
            let written = packet.read_at(buffer_offset, buf);
            chain.add_written(written as u32);
            buffer_offset += written;
            if buffer_offset > buffer_size {
                panic!("wrote past the end of the buffer: {} > {}", buffer_offset, buffer_size);
            }
            if buffer_offset == buffer_size {
                break;
            }
        }

        let latency = zx::Time::get_monotonic() - zx::Time::from_nanos(resp.pts);
        inner.lead_time.send(latency)?;
        reply_rxq::success(chain, inner.conn.latency_bytes())?;
        Ok(())
    }

    async fn do_background_work(&self) -> Result<(), Error> {
        // No-op.
        Ok(())
    }
}
