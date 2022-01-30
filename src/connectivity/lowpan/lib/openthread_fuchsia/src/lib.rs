// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This crate contains the rust OpenThread platform implementation for Fuchsia.

#![warn(rust_2018_idioms)]
#![warn(clippy::all)]

mod backing;
mod binding;
mod logging;

use futures::prelude::*;
use openthread::prelude::*;

use backing::*;
use binding::*;

use fuchsia_async as fasync;
use futures::channel::mpsc as fmpsc;
use futures::Stream;
use log::*;
use lowpan_driver_common::spinel::*;
use std::cell::{Cell, RefCell};
use std::sync::mpsc;
use std::task::{Context, Poll};

// Number of entries in the timer wakeup buffer.
// This value was chosen somewhat arbitrarily, with the only
// requirement being that it be larger than what should happen
// during normal operation.
const TIMER_BUFFER_LEN: usize = 20;

// Number of entries in the frame-ready channel.
// This length doesn't need to be very large, as it
// is effectively just a wakeup flag.
const FRAME_READY_BUFFER_LEN: usize = 2;

/// OpenThread Singleton Platform Implementation.
///
/// An instance of this type must be passed to
/// [`ot::Instance::new()`](openthread_rust::ot::Instance::new).
///
/// This type is a singleton. Attempting to init more than a
/// single instance of `Platform` at a time will cause a panic.
pub struct Platform {
    timer_receiver: fmpsc::Receiver<usize>,
    rcp_to_ot_frame_ready_receiver: fmpsc::Receiver<()>,
    ot_to_rcp_task: fasync::Task<()>,
    rcp_to_ot_task: fasync::Task<()>,
}

impl Platform {
    /// Initializes the OpenThread platform.
    ///
    /// The instance returned by this method must be passed to
    /// [`ot::Instance::new()`](openthread_rust::ot::Instance::new).
    ///
    /// The returned object is a singleton. Attempting to have more than one instance
    /// around at a time will cause a panic.
    pub fn init<SpinelSink, SpinelStream>(
        mut spinel_sink: SpinelSink,
        mut spinel_stream: SpinelStream,
    ) -> Self
    where
        SpinelSink: SpinelDeviceClient + 'static,
        SpinelStream: Stream<Item = Result<Vec<u8>, anyhow::Error>> + 'static + Unpin + Send,
    {
        ot::set_logging_level(ot::LogLevel::Info);

        // OpenThread to RCP data-pump and related machinery.
        let (timer_sender, timer_receiver) = fmpsc::channel(TIMER_BUFFER_LEN);
        let (ot_to_rcp_sender, ot_to_rcp_receiver) = mpsc::channel::<Vec<u8>>();
        let ot_to_rcp_task = fasync::Task::spawn(async move {
            spinel_sink.open().await.expect("Unable to open spinel stream");
            loop {
                trace!(target: "ot_to_rcp_task", "waiting on frame from OpenThread");

                let frame = match ot_to_rcp_receiver.recv() {
                    Ok(frame) => frame,
                    Err(e) => {
                        warn!(target: "ot_to_rcp_task", "ot_to_rcp_receiver.recv() failed with {:?}", e);
                        break;
                    }
                };

                trace!(target: "ot_to_rcp_task", "sending frame from OpenThread to RCP");
                if let Err(e) = spinel_sink.send(frame.as_slice()).await {
                    warn!(target: "ot_to_rcp_task", "spinel_sink.send() failed with {:?}", e);
                    break;
                }
            }
        });

        // RCP to OpenThread data-pump and related machinery.
        let (mut rcp_to_ot_frame_ready_sender, rcp_to_ot_frame_ready_receiver) =
            fmpsc::channel(FRAME_READY_BUFFER_LEN);
        let (rcp_to_ot_sender, rcp_to_ot_receiver) = mpsc::channel::<Vec<u8>>();
        let rcp_to_ot_task = fasync::Task::spawn(async move {
            while let Some(frame_result) = spinel_stream.next().await {
                match frame_result {
                    Ok(frame) => {
                        trace!(target: "rcp_to_ot_task", "sending frame from RCP to OpenThread");

                        if let Err(e) = rcp_to_ot_sender.send(frame) {
                            warn!(target: "rcp_to_ot_task", "rcp_to_ot_sender.send() failed with {:?}", e);
                            break;
                        }

                        // Notify our `process_poll` that it needs to call `platformRadioProcess`.
                        match rcp_to_ot_frame_ready_sender.try_send(()) {
                            Ok(()) => {}
                            Err(e) if e.is_full() => {}
                            Err(e) => {
                                warn!(target: "rcp_to_ot_task", "rcp_to_ot_frame_ready_sender.send() failed with {:?}", e);
                                break;
                            }
                        }
                    }
                    Err(e) => {
                        warn!(target: "rcp_to_ot_task", "spinel_stream.next() failed with {:?}", e);
                        break;
                    }
                }
            }
            trace!(target: "rcp_to_ot_task", "Stream ended.");
        });

        unsafe {
            // Initialize our singleton
            PlatformBacking::set_singleton(PlatformBacking {
                ot_to_rcp_sender: RefCell::new(ot_to_rcp_sender),
                rcp_to_ot_receiver: RefCell::new(rcp_to_ot_receiver),
                task_alarm: Cell::new(None),
                timer_sender,
            });

            // Initialize the lower-level platform implementation
            otSysInit(&mut otPlatformConfig { m_speedup_factor: 1, reset_rcp: false }
                as *mut otPlatformConfig);
        };

        Platform { timer_receiver, rcp_to_ot_frame_ready_receiver, ot_to_rcp_task, rcp_to_ot_task }
    }
}

impl Drop for Platform {
    fn drop(&mut self) {
        trace!("Dropping Platform");
        unsafe {
            // SAFETY: Both calls below must only be called from Drop.
            otSysDeinit();
            PlatformBacking::drop_singleton()
        }
    }
}

impl ot::Platform for Platform {
    unsafe fn process_poll(self: &mut Self, instance: &ot::Instance, cx: &mut Context<'_>) {
        let instance_ptr = instance.as_ot_ptr();

        while let Poll::Ready(Some(ptr_usize)) = self.timer_receiver.poll_next_unpin(cx) {
            trace!("Firing platformAlarmProcess");
            assert_eq!(instance_ptr as usize, ptr_usize, "Got wrong pointer from timer receiver");

            // SAFETY: Must be called with a valid pointer to otInstance,
            //         must also only be called from the main OpenThread thread,
            //         which is a guarantee of this method.
            platformAlarmProcess(instance_ptr);
        }

        while let Poll::Ready(Some(())) = self.rcp_to_ot_frame_ready_receiver.poll_next_unpin(cx) {
            trace!("Firing platformRadioProcess");

            // SAFETY: Must be called with a valid pointer to otInstance,
            //         must also only be called from the main OpenThread thread,
            //         which is a guarantee of this method.
            platformRadioProcess(instance_ptr);
        }

        if let Poll::Ready(_) = self.rcp_to_ot_task.poll_unpin(cx) {
            panic!("Platform: rcp_to_ot_task finished unexpectedly");
        }

        if let Poll::Ready(_) = self.ot_to_rcp_task.poll_unpin(cx) {
            panic!("Platform: ot_to_rcp_task finished unexpectedly");
        }
    }
}

// Disabled for now due to flakiness. The test was of questionable utility anyway.
// #[cfg(test)]
// mod tests {
//     use super::*;
//     use lowpan_driver_common::spinel::mock::*;
//
//     #[fasync::run(10, test)]
//     async fn test_runner() {
//         test_init_and_drop().await;
//         test_init_and_drop().await;
//     }
//
//     async fn test_init_and_drop() {
//         fuchsia_syslog::LOGGER.set_severity(fuchsia_syslog::levels::DEBUG);
//
//         let (sink, stream, ncp_task) = new_fake_spinel_pair();
//
//         let ncp_task = fasync::Task::spawn(ncp_task);
//
//         let instance = ot::Instance::new(Platform::init(sink, stream));
//
//         ncp_task.cancel().await;
//
//         std::mem::drop(instance);
//     }
// }
