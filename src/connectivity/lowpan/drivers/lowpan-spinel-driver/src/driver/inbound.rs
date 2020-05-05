// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::prelude::*;
use crate::spinel::*;
use futures::prelude::*;

use anyhow::Error;

impl<DS: SpinelDeviceClient> SpinelDriver<DS> {
    /// Method for handling inbound frames and pushing them
    /// to where they need to go.
    fn on_inbound_frame(&self, frame: &[u8]) -> Result<(), Error> {
        fx_log_info!("on_inbound_frame: {:?}", frame);

        // Parse the header.
        let frame = SpinelFrameRef::try_unpack_from_slice(frame).context("on_inbound_frame")?;

        // We only support NLI zero for the foreseeable future.
        if frame.header.nli() != 0 {
            fx_log_info!("on_inbound_frame: Skipping unexpected NLI for frame {:?}", frame);
            return Ok(());
        }

        // TODO: Update internal state

        // Finally pass the frame to the response frame handler.
        self.frame_handler.handle_inbound_frame(frame)
    }

    /// Wraps the given inbound frame stream with the
    /// logic to parse and handle the inbound frames.
    /// The resulting stream should be asynchronously
    /// drained to process inbound frames.
    ///
    /// In most cases you will want to call `take_inbound_stream` instead,
    /// but if it is unavailable (or you want to add filters or whatnot)
    /// then this is what you will use.
    ///
    /// You may only wrap an inbound stream once. Calling this
    /// method twice will cause a panic.
    pub fn wrap_inbound_stream<'a, T>(
        &'a self,
        device_stream: T,
    ) -> impl Stream<Item = Result<(), Error>> + Unpin + Send + 'a
    where
        T: futures::stream::Stream<Item = Result<Vec<u8>, Error>> + Unpin + Send + 'a,
    {
        let wrapped_future = device_stream
            .and_then(move |f| futures::future::ready(self.on_inbound_frame(&f)))
            .or_else(move |err| {
                futures::future::ready(match err {
                    // I/O errors are fatal.
                    err if err.is::<std::io::Error>() => Err(err),

                    // Other error cases may be added here in the future.

                    // Non-I/O errors cause a reset.
                    err => {
                        fx_log_err!("inbound_frame_stream: Error: {:?}", err);
                        Ok(())
                    }
                })
            });
        futures::stream::select(wrapped_future, self.take_main_task().boxed().into_stream())
    }
}

impl SpinelDriver<SpinelDeviceSink<fidl_fuchsia_lowpan_spinel::DeviceProxy>> {
    /// Takes the inbound frame stream from `SpinelDeviceSink::take_stream()`,
    /// adds the frame-handling logic, and returns the resulting stream.
    ///
    /// Since `SpinelDeviceSink::take_stream()` can only be called once, this
    /// method likewise can only be called once. Subsequent calls will panic.
    ///
    /// This method is only available if the type of `DS` is
    /// `SpinelDeviceSink<fidl_fuchsia_lowpan_spinel::DeviceProxy>`.
    /// In all other cases you must use `wrap_inbound_stream()` instead.
    pub fn take_inbound_stream(&self) -> impl Stream<Item = Result<(), Error>> + Unpin + Send + '_ {
        self.wrap_inbound_stream(self.device_sink.take_stream())
    }
}
