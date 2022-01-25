// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::{Proxy, ServerEnd},
    fidl_fuchsia_hardware_display as display,
    fuchsia_async::{self as fasync, futures::TryStreamExt, DurationExt, TimeoutExt},
    fuchsia_vfs_watcher::{WatchEvent, Watcher},
    fuchsia_zircon as zx,
    futures::future,
    std::{
        ffi::OsStr,
        fmt,
        fs::File,
        path::{Path, PathBuf},
    },
    thiserror::Error,
};

use crate::types::DisplayInfo;

const DEV_DIR_PATH: &str = "/dev/class/display-controller";
const TIMEOUT: zx::Duration = zx::Duration::from_seconds(2);

#[derive(Error, Debug)]
pub enum ControllerError {
    #[error("could not find a display-controller device")]
    DeviceNotFound,

    #[error("device did not enumerate initial displays")]
    NoDisplays,

    #[error("failed to watch files in device directory")]
    VfsWatcherError,

    #[error("FIDL error: {0}")]
    FidlError(#[from] fidl::Error),

    #[error("OS I/O error: {0}")]
    IoError(#[from] std::io::Error),

    #[error("zircon error: {0}")]
    ZxError(#[from] zx::Status),
}

pub type Result<T> = std::result::Result<T, ControllerError>;

/// Client abstraction for the `fuchsia.hardware.display.Controller` protocol.
pub struct Controller {
    displays: Vec<DisplayInfo>,

    #[allow(unused)]
    proxy: display::ControllerProxy,
    #[allow(unused)]
    events: display::ControllerEventStream,

    // TODO(fxbug.dev/33675): This channel is currently necessary to maintain a FIDL client
    // connection to the display-controller device. It doesn't have any other meaningful purpose
    // and we should remove it.
    _redundant_device_channel: zx::Channel,
}

impl Controller {
    /// Establishes a connection to the display-controller device and initialize a `Controller`
    /// instance with the initial set of available displays. The returned `Controller` will
    /// maintain FIDL connection to the underlying device as long as it is alive or the connection
    /// is closed by the peer.
    ///
    /// Returns an error if
    /// - No display-controller device is found within `TIMEOUT`.
    /// - An initial OnDisplaysChanged event is not received from the display driver within
    ///   `TIMEOUT` seconds.
    ///
    /// Current limitations:
    ///   - This function connects to the first display-controller device that it observes. It
    ///   currently does not support selection of a specific device if multiple display-controller
    ///   devices are present.
    // TODO(fxbug.dev/87469): This will currently result in an error if no displays are present on
    // the system (or if one is not attached within `TIMEOUT`). It wouldn't be neceesary to rely on
    // a timeout if the display driver sent en event with no displays.
    pub async fn init() -> Result<Controller> {
        let path = watch_first_file(DEV_DIR_PATH)
            .on_timeout(TIMEOUT.after_now(), || Err(ControllerError::DeviceNotFound))
            .await?;
        let (proxy, redundant_device_channel) = connect_controller(&path).await?;
        Self::init_with_proxy(proxy, redundant_device_channel).await
    }

    /// Initialize a `Controller` instance from a pre-established channel.
    ///
    /// Returns an error if
    /// - An initial OnDisplaysChanged event is not received from the display driver within
    ///   `TIMEOUT` seconds.
    // TODO(fxbug.dev/87469): This will currently result in an error if no displays are present on
    // the system (or if one is not attached within `TIMEOUT`). It wouldn't be neceesary to rely on
    // a timeout if the display driver sent en event with no displays.
    pub async fn init_with_proxy(
        proxy: display::ControllerProxy,
        _redundant_device_channel: zx::Channel,
    ) -> Result<Controller> {
        let mut events = proxy.take_event_stream();
        let displays = wait_for_initial_displays(&mut events)
            .on_timeout(TIMEOUT.after_now(), || Err(ControllerError::NoDisplays))
            .await?
            .into_iter()
            .map(DisplayInfo)
            .collect::<Vec<_>>();
        Ok(Controller { proxy, events, displays, _redundant_device_channel })
    }

    /// Returns the list of displays that are currently known to be present on the system.
    pub fn displays(&self) -> &Vec<DisplayInfo> {
        &self.displays
    }

    // TODO(armansito): Add a mechanism for clients to observe individual events over the same
    // underlying `display::ControllerEventStream`.
}

// fmt::Debug implementation to allow a `Controller` instance to be used with a debug format
// specifier. We use a custom implementation as not all `Controller` members derive fmt::Debug.
impl fmt::Debug for Controller {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Controller").field("displays", &self.displays).finish()
    }
}

// Asynchronously returns the path to the first file found under the given directory path. The
// returned future does not resolve until either an entry is found or there is an error while
// watching the directory.
async fn watch_first_file<P: AsRef<Path> + AsRef<OsStr>>(dir: P) -> Result<PathBuf> {
    let path = Path::new(&dir);
    let dir: fidl_fuchsia_io::DirectoryProxy = {
        let raw_dir = File::open(&path)?;
        let zx_channel = fdio::clone_channel(&raw_dir)?;
        let fasync_channel = fasync::Channel::from_channel(zx_channel)?;
        fidl_fuchsia_io::DirectoryProxy::from_channel(fasync_channel)
    };

    let mut watcher = Watcher::new(dir).await.map_err(|_| ControllerError::VfsWatcherError)?;
    while let Some(msg) = watcher.try_next().await? {
        match msg.event {
            WatchEvent::EXISTING | WatchEvent::ADD_FILE => {
                return Ok(path.join(msg.filename));
            }
            _ => continue,
        }
    }
    Err(ControllerError::DeviceNotFound)
}

// Establishes a fuchsia.hardware.display.Controller protocol channel to the display-controller
// device with the given `dev_path` by connecting as a primary controller.
// TODO(fxbug.dev/33675): Remove the extra zx::Channel argument once the FIDL protocol has been
// simplified.
// TODO(armansito): Consider supporting virtcon client connections.
async fn connect_controller<P: AsRef<Path>>(
    dev_path: P,
) -> Result<(display::ControllerProxy, zx::Channel)> {
    let device = File::open(dev_path)?;
    let provider = {
        let channel = fdio::clone_channel(&device)?;
        display::ProviderProxy::new(fasync::Channel::from_channel(channel)?)
    };

    let (redundant_local, redundant_remote) = zx::Channel::create()?;
    let (local, remote) = zx::Channel::create()?;
    let _ =
        zx::Status::ok(provider.open_controller(redundant_remote, ServerEnd::new(remote)).await?)?;

    Ok((display::ControllerProxy::new(fasync::Channel::from_channel(local)?), redundant_local))
}

// Waits for a single fuchsia.hardware.display.Controller.OnDisplaysChanged event and returns the
// reported displays. By API contract, this event will fire at least once upon initial channel
// connection if any displays are present. If no displays are present, then the returned Future
// will not resolve until a display is plugged in.
async fn wait_for_initial_displays(
    events: &mut display::ControllerEventStream,
) -> Result<Vec<display::Info>> {
    let mut stream = events.try_filter_map(|event| match event {
        display::ControllerEvent::OnDisplaysChanged { added, removed: _ } => {
            future::ok(Some(added))
        }
        _ => future::ok(None),
    });
    stream.try_next().await?.ok_or(ControllerError::NoDisplays)
}

#[cfg(test)]
mod tests {
    use super::Controller;
    use {
        anyhow::{Context, Result},
        display_mocks::create_proxy_and_mock,
        fidl_fuchsia_hardware_display as display, fuchsia_zircon as zx,
        matches::assert_matches,
    };

    async fn init_with_proxy(proxy: display::ControllerProxy) -> Result<Controller> {
        let (_, remote) = zx::Channel::create()?;
        Controller::init_with_proxy(proxy, remote).await.context("failed to initialize Controller")
    }

    #[fuchsia::test]
    async fn test_init_fails_with_no_device_dir() {
        let result = Controller::init().await;
        assert_matches!(result, Err(_));
    }

    #[fuchsia::test]
    async fn test_init_with_no_displays() -> Result<()> {
        let (proxy, mut mock) = create_proxy_and_mock()?;
        mock.assign_displays([].to_vec())?;

        let controller = init_with_proxy(proxy).await?;
        assert!(controller.displays.is_empty());

        Ok(())
    }

    #[fuchsia::test]
    async fn test_init_with_displays() -> Result<()> {
        let displays = [
            display::Info {
                id: 1,
                modes: Vec::new(),
                pixel_format: Vec::new(),
                cursor_configs: Vec::new(),
                manufacturer_name: "Foo".to_string(),
                monitor_name: "what".to_string(),
                monitor_serial: "".to_string(),
                horizontal_size_mm: 0,
                vertical_size_mm: 0,
                using_fallback_size: false,
            },
            display::Info {
                id: 2,
                modes: Vec::new(),
                pixel_format: Vec::new(),
                cursor_configs: Vec::new(),
                manufacturer_name: "Bar".to_string(),
                monitor_name: "who".to_string(),
                monitor_serial: "".to_string(),
                horizontal_size_mm: 0,
                vertical_size_mm: 0,
                using_fallback_size: false,
            },
        ]
        .to_vec();
        let (proxy, mut mock) = create_proxy_and_mock()?;
        mock.assign_displays(displays.clone())?;

        let controller = init_with_proxy(proxy).await?;
        assert_eq!(controller.displays().len(), 2);
        assert_eq!(controller.displays()[0].0, displays[0]);
        assert_eq!(controller.displays()[1].0, displays[1]);

        Ok(())
    }
}
