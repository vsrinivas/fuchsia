// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    fidl::endpoints::Proxy,
    fidl_fuchsia_hardware_pty as fpty, fidl_fuchsia_io as fio,
    fuchsia_zircon::{self as zx, HandleBased},
    std::os::unix::io::AsRawFd,
};

/// This function attempts to determine whether the given `fd` is a pty. If it is a pty then the
/// corresponding fuchsia.hardware.pty.Device proxy is returned as well as an eventpair for
/// receiving notifications of any out-of-band events associated with the pty. These oob events can
/// be read using the Device.ReadEvents method.
///
/// Returns Ok(None) when the `fd` has been conclusively determined to not be a pty and Err(...) if
/// we were unable to find out more due to some failure.
pub async fn get_pty(fd: &impl AsRawFd) -> Result<Option<(fpty::DeviceProxy, zx::EventPair)>> {
    // If clone_channel succeeds that means we have a channel speaking fuchsia.io.Node which
    // *might* also be a pty. Otherwise this fd might be connected to a Socket or something else
    // which is definitely not a pty.
    let chan = match fdio::clone_channel(fd) {
        Ok(chan) => chan,
        Err(_) => return Ok(None),
    };
    let async_chan = fidl::handle::AsyncChannel::from_channel(chan)
        .context("Failed to create AsyncChannel from cloned fd")?;
    let node = fio::NodeProxy::new(async_chan);

    let node_type = node.query().await.context("Query failed")?;
    if node_type != fpty::DEVICE_PROTOCOL_NAME.as_bytes() {
        return Ok(None);
    }

    let device = fpty::DeviceProxy::new(
        node.into_channel().expect("There should be no remaining active users of this proxy"),
    );
    let eventpair = device
        .describe()
        .await
        .context("Call to Describe failed")?
        .event
        .expect("Device/Describe did not contain an event");

    Ok(Some((device, eventpair)))
}

/// Wrapper around Device.get_window_size, collapsing the multiple failure modes into one level.
pub async fn get_window_size(pty: &fpty::DeviceProxy) -> Result<fpty::WindowSize> {
    pty.get_window_size().await.context("GetWindowSize call failed").and_then(
        |(status, win_size)| {
            zx::Status::ok(status).context("GetWindowSize status not OK")?;
            Ok(win_size)
        },
    )
}

/// This type represents a successfully initialized pty and its initial geometry. When dropped, the
/// pty features are restored to their original values.
pub struct RawPty {
    cols: i32,
    rows: i32,
    pty: zx::Channel,
    previous_feature: u32,
}

impl RawPty {
    /// Creates a RawPty from stdin if possible, returning Ok(None) if stdin is not a pty.
    pub async fn new() -> Result<Option<Self>> {
        match get_pty(&std::io::stdin()).await? {
            None => Ok(None),
            Some((pty, _)) => Self::create_from_pty(pty).await.map(Some),
        }
    }

    /// The initial width (in characters) of the pty.
    pub fn cols(&self) -> i32 {
        self.cols
    }

    /// The initial height (in characters) of the pty.
    pub fn rows(&self) -> i32 {
        self.rows
    }

    async fn create_from_pty(pty: fpty::DeviceProxy) -> Result<Self> {
        let win_size = get_window_size(&pty)
            .await
            .context("Unable to determine shell geometry from pty, defaulting to 80x24.")?;
        let cols = win_size.width.try_into()?;
        let rows = win_size.height.try_into()?;

        // Query and store original feature flags.
        let previous_feature = pty
            .clr_set_feature(0, 0)
            .await
            .context("ClrSetFeature call failed")
            .and_then(|(status, feature)| {
                zx::Status::ok(status).context("ClrSetFeature status not OK")?;
                Ok(feature)
            })
            .context("Failed to query feature flags.")?;
        // Enable raw mode on pty so that inputs such as ctrl-c are passed on faithfully to us for
        // forwarding to the remote shell (instead of closing the client side).
        pty.clr_set_feature(0, fpty::FEATURE_RAW)
            .await
            .context("ClrSetFeature call failed")
            .and_then(|(status, _)| zx::Status::ok(status).context("ClrSetFeature status not OK"))
            .context("Failed to set pty to raw mode.")?;
        let pty_chan = pty
            .into_channel()
            .expect("There should be no remaining active users of this proxy")
            .into_zx_channel();
        Ok(RawPty { cols, rows, pty: pty_chan, previous_feature })
    }
}

impl Drop for RawPty {
    fn drop(&mut self) {
        // If previous mode wasn't already raw, reset it.
        if (self.previous_feature & fpty::FEATURE_RAW) != fpty::FEATURE_RAW {
            let pty_chan =
                std::mem::replace(&mut self.pty, zx::Channel::from_handle(zx::Handle::invalid()));
            let pty = fpty::DeviceSynchronousProxy::new(pty_chan);
            let (status, _) =
                pty.clr_set_feature(fpty::FEATURE_RAW, 0, zx::Time::INFINITE).unwrap();
            if zx::Status::from_raw(status) != zx::Status::OK {
                // eprintln instead of tracing here since the user might want to know more
                // immediately why their terminal is not acting as expected.
                eprintln!("Failed to disable pty raw mode: {}", status);
            }
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl::endpoints::create_proxy_and_stream;
    use fuchsia_async as fasync;
    use futures::FutureExt;
    use futures::StreamExt;

    const DEFAULT_WIN_SIZE: fpty::WindowSize = fpty::WindowSize { width: 99, height: 29 };

    struct MockPty {
        features: u32,
        request_stream: fpty::DeviceRequestStream,
    }

    impl MockPty {
        fn new(features: u32, request_stream: fpty::DeviceRequestStream) -> Self {
            Self { features, request_stream }
        }

        async fn run(&mut self) -> Result<()> {
            while let Some(req) = self.request_stream.next().await {
                let req = req.context("failed to get next pty request from RequestStream")?;
                match req {
                    fpty::DeviceRequest::GetWindowSize { responder } => {
                        let mut window_size = DEFAULT_WIN_SIZE.clone();
                        responder
                            .send(0, &mut window_size)
                            .context("failed to send GetWindowSize response")?;
                    }
                    fpty::DeviceRequest::ClrSetFeature { clr, set, responder } => {
                        self.features &= !clr;
                        self.features |= set;
                        responder
                            .send(0, self.features)
                            .context("failed to send ClrSetFeature response")?;
                    }
                    rest => {
                        anyhow::bail!("MockPty unprepared to handle request {:?}", rest);
                    }
                }
            }
            Ok(())
        }
    }

    async fn create_and_drop_raw_pty(original_feature: u32) {
        let (pty_device_proxy, pty_device_stream) = create_proxy_and_stream::<fpty::DeviceMarker>()
            .expect("failed to create pty proxy and stream");

        let mut mock_pty = MockPty::new(original_feature, pty_device_stream);
        assert_eq!(mock_pty.features, original_feature);

        let raw_pty = futures::select_biased! {
            maybe_raw_pty = RawPty::create_from_pty(pty_device_proxy).fuse() => {
                maybe_raw_pty.expect("failed to create RawPty")
            }
            result = mock_pty.run().fuse() => {
                // The RequestStream should not be finished yet so we should not enter this branch
                panic!("MockPty RequestStream terminated unexpectedly with: {result:?}")
            }
        };

        assert_eq!(u32::try_from(raw_pty.cols).unwrap(), DEFAULT_WIN_SIZE.width);
        assert_eq!(u32::try_from(raw_pty.rows).unwrap(), DEFAULT_WIN_SIZE.height);
        assert_eq!(mock_pty.features, fpty::FEATURE_RAW);

        // Need to send raw_pty to a new thread to be dropped since it communicates over its
        // channel synchronously. The MockPty RequestStream should terminate when raw_pty drops its
        // end of the channel, which should allow the test to terminate.
        std::thread::spawn(move || drop(raw_pty));

        const TIMEOUT: i64 = 5000;
        let mut timeout = fasync::Timer::new(fasync::Duration::from_millis(TIMEOUT)).fuse();

        futures::select_biased! {
            result = mock_pty.run().fuse() => result.expect("MockPty::run terminated with failure"),
            _ = timeout => panic!("MockPty::run did not terminate after {TIMEOUT} milliseconds"),
        }

        assert_eq!(mock_pty.features, original_feature);
    }

    #[fuchsia::test]
    async fn feature_none_restored_on_drop() {
        create_and_drop_raw_pty(0).await;
    }

    #[fuchsia::test]
    async fn feature_raw_restored_on_drop() {
        create_and_drop_raw_pty(fpty::FEATURE_RAW).await;
    }

    #[fuchsia::test]
    async fn get_window_size_test() {
        let (pty_device_proxy, pty_device_stream) = create_proxy_and_stream::<fpty::DeviceMarker>()
            .expect("failed to create pty proxy and stream");

        let mut mock_pty = MockPty::new(0, pty_device_stream);

        let win_size = futures::select_biased! {
            maybe_win_size = get_window_size(&pty_device_proxy).fuse() => {
                maybe_win_size.expect("failed to query WindowSize")
            }
            result = mock_pty.run().fuse() => {
                // The RequestStream should not be finished yet so we should not enter this branch
                panic!("MockPty RequestStream terminated unexpectedly with: {result:?}")
            }
        };

        assert_eq!(win_size.width, DEFAULT_WIN_SIZE.width);
        assert_eq!(win_size.height, DEFAULT_WIN_SIZE.height);
    }

    #[fuchsia::test]
    async fn no_pty_found_for_non_pty() {
        // Conveniently the tests are given non-pty stdio.
        assert!(get_pty(&std::io::stdin()).await.unwrap().is_none());
    }
}
