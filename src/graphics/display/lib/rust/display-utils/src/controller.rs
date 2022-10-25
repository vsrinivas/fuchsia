// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::{ClientEnd, Proxy, ServerEnd},
    fidl_fuchsia_hardware_display::{self as display, ControllerEvent},
    fidl_fuchsia_io as fio,
    fuchsia_async::{self as fasync, DurationExt, TimeoutExt},
    fuchsia_vfs_watcher::{WatchEvent, Watcher},
    fuchsia_zircon::{self as zx, HandleBased},
    futures::{channel::mpsc, future, TryStreamExt},
    parking_lot::RwLock,
    std::{
        ffi::OsStr,
        fmt,
        fs::File,
        path::{Path, PathBuf},
        sync::Arc,
    },
};

use crate::{
    config::{DisplayConfig, LayerConfig},
    error::{ConfigError, Error, Result},
    types::{CollectionId, DisplayId, DisplayInfo, Event, EventId, ImageId, LayerId},
};

const DEV_DIR_PATH: &str = "/dev/class/display-controller";
const TIMEOUT: zx::Duration = zx::Duration::from_seconds(2);

/// Client abstraction for the `fuchsia.hardware.display.Controller` protocol. Instances can be
/// safely cloned and passed across threads.
#[derive(Clone)]
pub struct Controller {
    inner: Arc<RwLock<ControllerInner>>,
}

struct ControllerInner {
    displays: Vec<DisplayInfo>,
    proxy: display::ControllerProxy,
    events: Option<display::ControllerEventStream>,

    // All subscribed vsync listeners and their optional ID filters.
    vsync_listeners: Vec<(mpsc::UnboundedSender<VsyncEvent>, Option<DisplayId>)>,

    // Simple counter to generate client-assigned integer identifiers.
    id_counter: u64,
}

/// A vsync event payload.
#[derive(Debug)]
pub struct VsyncEvent {
    /// The ID of the display that generated the vsync event.
    pub id: DisplayId,

    /// The monotonic timestamp of the vsync event.
    pub timestamp: zx::Time,

    /// The stamp of the latest fully applied display configuration.
    pub config: display::ConfigStamp,
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
            .on_timeout(TIMEOUT.after_now(), || Err(Error::DeviceNotFound))
            .await?;
        let proxy = connect_controller(&path).await?;
        Self::init_with_proxy(proxy).await
    }

    /// Initialize a `Controller` instance from a pre-established channel.
    ///
    /// Returns an error if
    /// - An initial OnDisplaysChanged event is not received from the display driver within
    ///   `TIMEOUT` seconds.
    // TODO(fxbug.dev/87469): This will currently result in an error if no displays are present on
    // the system (or if one is not attached within `TIMEOUT`). It wouldn't be neceesary to rely on
    // a timeout if the display driver sent en event with no displays.
    pub async fn init_with_proxy(proxy: display::ControllerProxy) -> Result<Controller> {
        let mut events = proxy.take_event_stream();
        let displays = wait_for_initial_displays(&mut events)
            .on_timeout(TIMEOUT.after_now(), || Err(Error::NoDisplays))
            .await?
            .into_iter()
            .map(DisplayInfo)
            .collect::<Vec<_>>();
        Ok(Controller {
            inner: Arc::new(RwLock::new(ControllerInner {
                proxy,
                events: Some(events),
                displays,
                vsync_listeners: Vec::new(),
                id_counter: 0,
            })),
        })
    }

    /// Returns a copy of the list of displays that are currently known to be present on the system.
    pub fn displays(&self) -> Vec<DisplayInfo> {
        self.inner.read().displays.clone()
    }

    /// Returns a clone of the underlying FIDL client proxy.
    ///
    /// Note: This can be helpful to prevent holding the inner RwLock when awaiting a chained FIDL
    /// call over a proxy.
    pub fn proxy(&self) -> display::ControllerProxy {
        self.inner.read().proxy.clone()
    }

    /// Tell the driver to enable vsync notifications and register a channel to listen to vsync events.
    pub fn add_vsync_listener(
        &self,
        id: Option<DisplayId>,
    ) -> Result<mpsc::UnboundedReceiver<VsyncEvent>> {
        self.inner.read().proxy.enable_vsync(true)?;

        // TODO(armansito): Switch to a bounded channel instead.
        let (sender, receiver) = mpsc::unbounded::<VsyncEvent>();
        self.inner.write().vsync_listeners.push((sender, id));
        Ok(receiver)
    }

    /// Returns a Future that represents the FIDL event handling task. Once scheduled on an
    /// executor, this task will continuously handle incoming FIDL events from the display stack
    /// and the returned Future will not terminate until the FIDL channel is closed.
    ///
    /// This task can be scheduled safely on any thread.
    pub async fn handle_events(&self) -> Result<()> {
        let inner = self.inner.clone();
        let mut events = inner.write().events.take().ok_or(Error::AlreadyRequested)?;
        while let Some(msg) = events.try_next().await? {
            match msg {
                ControllerEvent::OnDisplaysChanged { added, removed } => {
                    inner.read().handle_displays_changed(added, removed);
                }
                ControllerEvent::OnVsync {
                    display_id,
                    timestamp,
                    applied_config_stamp,
                    cookie,
                } => {
                    inner.write().handle_vsync(
                        display_id,
                        timestamp,
                        applied_config_stamp,
                        cookie,
                    )?;
                }
                _ => continue,
            }
        }
        Ok(())
    }

    /// Allocates a new virtual hardware layer that is not associated with any display and has no
    /// configuration.
    pub async fn create_layer(&self) -> Result<LayerId> {
        let (result, id) = self.proxy().create_layer().await?;
        let _ = zx::Status::ok(result)?;
        Ok(LayerId(id))
    }

    /// Creates and registers a zircon event with the display driver. The returned event can be
    /// used as a fence in a display configuration.
    pub fn create_event(&self) -> Result<Event> {
        let event = zx::Event::create()?;
        let remote = event.duplicate_handle(zx::Rights::SAME_RIGHTS)?;
        let id = self.inner.write().next_free_event_id()?;

        self.inner.read().proxy.import_event(zx::Event::from(remote), id.0)?;
        Ok(Event::new(id, event))
    }

    /// Apply a display configuration. The client is expected to receive a vsync event once the
    /// configuration is successfully applied. Returns an error if the FIDL message cannot be sent.
    pub async fn apply_config(
        &self,
        configs: &[DisplayConfig],
    ) -> std::result::Result<(), ConfigError> {
        let proxy = self.proxy();
        for config in configs {
            proxy.set_display_layers(
                config.id.0,
                &config.layers.iter().map(|l| l.id.0).collect::<Vec<u64>>(),
            )?;
            for layer in &config.layers {
                match &layer.config {
                    LayerConfig::Color { pixel_format, color_bytes } => {
                        proxy.set_layer_color_config(
                            layer.id.0,
                            pixel_format.into(),
                            &color_bytes,
                        )?;
                    }
                    LayerConfig::Primary {
                        image_id,
                        mut image_config,
                        unblock_event,
                        retirement_event,
                    } => {
                        proxy.set_layer_primary_config(layer.id.0, &mut image_config)?;
                        proxy.set_layer_image(
                            layer.id.0,
                            image_id.0,
                            unblock_event.map_or(0, |id| id.0),
                            retirement_event.map_or(0, |id| id.0),
                        )?;
                    }
                    _ => (),
                }
            }
        }

        let (result, ops) = proxy.check_config(false).await?;
        if result != display::ConfigResult::Ok {
            return Err(ConfigError::invalid(result, ops));
        }

        proxy.apply_config().map_err(ConfigError::from)
    }

    /// Import a sysmem buffer collection. The returned `CollectionId` can be used in future API
    /// calls to refer to the imported collection.
    pub(crate) async fn import_buffer_collection(
        &self,
        token: ClientEnd<fidl_fuchsia_sysmem::BufferCollectionTokenMarker>,
    ) -> Result<CollectionId> {
        let id = self.inner.write().next_free_collection_id()?;
        let proxy = self.proxy();

        // First import the token.
        let _ = zx::Status::ok(proxy.import_buffer_collection(id.0, token).await?)?;

        // Tell the driver to assign any device-specific constraints.
        // TODO(fxbug.dev/85320): These fields are effectively unused except for `type` in the case
        // of IMAGE_TYPE_CAPTURE.
        let _ = zx::Status::ok(
            proxy
                .set_buffer_collection_constraints(
                    id.0,
                    &mut display::ImageConfig { width: 0, height: 0, pixel_format: 0, type_: 0 },
                )
                .await?,
        )?;
        Ok(id)
    }

    /// Notify the display driver to release its handle on a previously imported buffer collection.
    pub(crate) fn release_buffer_collection(&self, id: CollectionId) -> Result<()> {
        self.inner.read().proxy.release_buffer_collection(id.0).map_err(Error::from)
    }

    /// Register a sysmem buffer collection backed image to the display driver.
    pub(crate) async fn import_image(
        &self,
        id: CollectionId,
        mut config: display::ImageConfig,
    ) -> Result<ImageId> {
        let (result, id) = self.proxy().import_image(&mut config, id.0, 0).await?;
        let _ = zx::Status::ok(result)?;
        Ok(ImageId(id))
    }
}

// fmt::Debug implementation to allow a `Controller` instance to be used with a debug format
// specifier. We use a custom implementation as not all `Controller` members derive fmt::Debug.
impl fmt::Debug for Controller {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Controller").field("displays", &self.displays()).finish()
    }
}

impl ControllerInner {
    fn next_free_collection_id(&mut self) -> Result<CollectionId> {
        self.id_counter = self.id_counter.checked_add(1).ok_or(Error::IdsExhausted)?;
        Ok(CollectionId(self.id_counter))
    }

    fn next_free_event_id(&mut self) -> Result<EventId> {
        self.id_counter = self.id_counter.checked_add(1).ok_or(Error::IdsExhausted)?;
        Ok(EventId(self.id_counter))
    }

    fn handle_displays_changed(&self, _added: Vec<display::Info>, _removed: Vec<u64>) {
        // TODO(armansito): update the displays list and notify clients. Terminate vsync listeners
        // that are attached to a removed display.
    }

    fn handle_vsync(
        &mut self,
        display_id: u64,
        timestamp: u64,
        applied_config_stamp: display::ConfigStamp,
        cookie: u64,
    ) -> Result<()> {
        self.proxy.acknowledge_vsync(cookie)?;

        let mut listeners_to_remove = Vec::new();
        for (pos, (sender, filter)) in self.vsync_listeners.iter().enumerate() {
            // Skip the listener if it has a filter that does not match `display_id`.
            if filter.as_ref().map_or(false, |id| id.0 != display_id) {
                continue;
            }
            let payload = VsyncEvent {
                id: DisplayId(display_id),
                timestamp: zx::Time::from_nanos(timestamp as i64),
                config: applied_config_stamp,
            };
            if let Err(e) = sender.unbounded_send(payload) {
                if e.is_disconnected() {
                    listeners_to_remove.push(pos);
                } else {
                    return Err(e.into());
                }
            }
        }

        // Clean up disconnected listeners.
        listeners_to_remove.into_iter().for_each(|pos| {
            self.vsync_listeners.swap_remove(pos);
        });

        Ok(())
    }
}

// Asynchronously returns the path to the first file found under the given directory path. The
// returned future does not resolve until either an entry is found or there is an error while
// watching the directory.
async fn watch_first_file<P: AsRef<Path> + AsRef<OsStr>>(dir: P) -> Result<PathBuf> {
    let path = Path::new(&dir);
    let dir: fio::DirectoryProxy = {
        let raw_dir = File::open(&path)?;
        let zx_channel = fdio::clone_channel(&raw_dir)?;
        let fasync_channel = fasync::Channel::from_channel(zx_channel)?;
        fio::DirectoryProxy::from_channel(fasync_channel)
    };

    let mut watcher = Watcher::new(dir).await.map_err(|_| Error::VfsWatcherError)?;
    while let Some(msg) = watcher.try_next().await? {
        match msg.event {
            WatchEvent::EXISTING | WatchEvent::ADD_FILE => {
                if msg.filename == Path::new(".") {
                    continue;
                }
                return Ok(path.join(msg.filename));
            }
            _ => continue,
        }
    }
    Err(Error::DeviceNotFound)
}

// Establishes a fuchsia.hardware.display.Controller protocol channel to the display-controller
// device with the given `dev_path` by connecting as a primary controller.
// TODO(armansito): Consider supporting virtcon client connections.
async fn connect_controller<P: AsRef<Path>>(dev_path: P) -> Result<display::ControllerProxy> {
    let device = File::open(dev_path)?;
    let provider = {
        let channel = fdio::clone_channel(&device)?;
        display::ProviderProxy::new(fasync::Channel::from_channel(channel)?)
    };

    let (local, remote) = zx::Channel::create()?;
    let _ = zx::Status::ok(provider.open_controller(ServerEnd::new(remote)).await?)?;

    Ok(display::ControllerProxy::new(fasync::Channel::from_channel(local)?))
}

// Waits for a single fuchsia.hardware.display.Controller.OnDisplaysChanged event and returns the
// reported displays. By API contract, this event will fire at least once upon initial channel
// connection if any displays are present. If no displays are present, then the returned Future
// will not resolve until a display is plugged in.
async fn wait_for_initial_displays(
    events: &mut display::ControllerEventStream,
) -> Result<Vec<display::Info>> {
    let mut stream = events.try_filter_map(|event| match event {
        ControllerEvent::OnDisplaysChanged { added, removed: _ } => future::ok(Some(added)),
        _ => future::ok(None),
    });
    stream.try_next().await?.ok_or(Error::NoDisplays)
}

#[cfg(test)]
mod tests {
    use super::{Controller, DisplayId, VsyncEvent};
    use {
        anyhow::{format_err, Context, Result},
        assert_matches::assert_matches,
        display_mocks::{create_proxy_and_mock, MockController},
        fidl_fuchsia_hardware_display as display,
        fuchsia_async::TestExecutor,
        futures::{pin_mut, select, task::Poll, FutureExt, StreamExt},
    };

    async fn init_with_proxy(proxy: display::ControllerProxy) -> Result<Controller> {
        Controller::init_with_proxy(proxy).await.context("failed to initialize Controller")
    }

    // Returns a Controller and a connected mock FIDL server. This function sets up the initial
    // "OnDisplaysChanged" event with the given list of `displays`, which `Controller` requires
    // before it can resolve its initialization Future.
    async fn init_with_displays(
        displays: &[display::Info],
    ) -> Result<(Controller, MockController)> {
        let (proxy, mut mock) = create_proxy_and_mock()?;
        mock.assign_displays(displays.to_vec())?;

        Ok((init_with_proxy(proxy).await?, mock))
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
        assert!(controller.displays().is_empty());

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

    #[test]
    fn test_vsync_listener_single() -> Result<()> {
        // Drive an executor directly for this test to avoid having to rely on timeouts for cases
        // in which no events are received.
        let mut executor = TestExecutor::new()?;
        let (controller, mock) = executor.run_singlethreaded(init_with_displays(&[]))?;
        let mut vsync = controller.add_vsync_listener(None)?;

        const ID: DisplayId = DisplayId(1);
        const STAMP: display::ConfigStamp = display::ConfigStamp { value: 1 };
        let event_handlers = async {
            select! {
                event = vsync.next().fuse() => event.ok_or(format_err!("did not receive vsync event")),
                result = controller.handle_events().fuse() => {
                    result.context("FIDL event handler failed")?;
                    Err(format_err!("FIDL event handler completed before client vsync event"))
                },
            }
        };
        pin_mut!(event_handlers);

        // Send a single event.
        mock.emit_vsync_event(ID.0, STAMP)?;
        let vsync_event = executor.run_until_stalled(&mut event_handlers);
        assert_matches!(
            vsync_event,
            Poll::Ready(Ok(VsyncEvent { id: ID, timestamp: _, config: STAMP }))
        );

        Ok(())
    }

    #[test]
    fn test_vsync_listener_multiple() -> Result<()> {
        // Drive an executor directly for this test to avoid having to rely on timeouts for cases
        // in which no events are received.
        let mut executor = TestExecutor::new()?;
        let (controller, mock) = executor.run_singlethreaded(init_with_displays(&[]))?;
        let mut vsync = controller.add_vsync_listener(None)?;

        let fidl_server = controller.handle_events().fuse();
        pin_mut!(fidl_server);

        const ID1: DisplayId = DisplayId(1);
        const ID2: DisplayId = DisplayId(2);
        const STAMP: display::ConfigStamp = display::ConfigStamp { value: 1 };

        // Queue multiple events.
        mock.emit_vsync_event(ID1.0, STAMP)?;
        mock.emit_vsync_event(ID2.0, STAMP)?;
        mock.emit_vsync_event(ID1.0, STAMP)?;

        // Process the FIDL events. The FIDL server Future should not complete as it runs
        // indefinitely.
        let fidl_server_result = executor.run_until_stalled(&mut fidl_server);
        assert_matches!(fidl_server_result, Poll::Pending);

        // Process the vsync listener.
        let vsync_event = executor.run_until_stalled(&mut Box::pin(async { vsync.next().await }));
        assert_matches!(
            vsync_event,
            Poll::Ready(Some(VsyncEvent { id: ID1, timestamp: _, config: STAMP }))
        );

        let vsync_event = executor.run_until_stalled(&mut Box::pin(async { vsync.next().await }));
        assert_matches!(
            vsync_event,
            Poll::Ready(Some(VsyncEvent { id: ID2, timestamp: _, config: STAMP }))
        );

        let vsync_event = executor.run_until_stalled(&mut Box::pin(async { vsync.next().await }));
        assert_matches!(
            vsync_event,
            Poll::Ready(Some(VsyncEvent { id: ID1, timestamp: _, config: STAMP }))
        );

        Ok(())
    }

    #[test]
    fn test_vsync_listener_display_id_filter() -> Result<()> {
        // Drive an executor directly for this test to avoid having to rely on timeouts for cases
        // in which no events are received.
        let mut executor = TestExecutor::new()?;
        let (controller, mock) = executor.run_singlethreaded(init_with_displays(&[]))?;

        const ID1: DisplayId = DisplayId(1);
        const ID2: DisplayId = DisplayId(2);
        const STAMP: display::ConfigStamp = display::ConfigStamp { value: 1 };

        // Listen to events from ID2.
        let mut vsync = controller.add_vsync_listener(Some(ID2))?;
        let event_handlers = async {
            select! {
                event = vsync.next().fuse() => event.ok_or(format_err!("did not receive vsync event")),
                result = controller.handle_events().fuse() => {
                    result.context("FIDL event handler failed")?;
                    Err(format_err!("FIDL event handler completed before client vsync event"))
                },
            }
        };
        pin_mut!(event_handlers);

        // Event from ID1 should get filtered out and the client should not receive any events.
        mock.emit_vsync_event(ID1.0, STAMP)?;
        let vsync_event = executor.run_until_stalled(&mut event_handlers);
        assert_matches!(vsync_event, Poll::Pending);

        // Event from ID2 should be received.
        mock.emit_vsync_event(ID2.0, STAMP)?;
        let vsync_event = executor.run_until_stalled(&mut event_handlers);
        assert_matches!(
            vsync_event,
            Poll::Ready(Ok(VsyncEvent { id: ID2, timestamp: _, config: STAMP }))
        );

        Ok(())
    }
}
