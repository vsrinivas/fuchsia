// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ctap_device::CtapDevice,
    crate::ctap_hid::CtapHidDevice,
    anyhow::{format_err, Error},
    fidl_fuchsia_io as fio,
    fidl_fuchsia_io::DirectoryProxy,
    fuchsia_async as fasync,
    fuchsia_inspect::{self as inspect, NumericProperty, Property},
    fuchsia_vfs_watcher::{WatchEvent, Watcher},
    futures::lock::Mutex,
    futures::TryStreamExt,
    std::path::PathBuf,
    std::sync::Arc,
    tracing::{error, info, warn},
};

/// Contains a reference to the currently connected key.
// TODO(fxbug.dev/109408): Handle multiple security keys connected at once instead of ignoring new
// keys if one is already bound and still accessible.
type DeviceBinding = Arc<Mutex<Option<CtapHidDevice>>>;

/// The absolute path at which CTAPHID devices are exposed.
static CTAPHID_PATH: &str = "/dev/class/ctap";

#[derive(Default)]
struct CtapAgentMetrics {
    /// Inspect data.
    _node: inspect::Node,
    /// The number of devices that have been bound.
    bindings_count: Arc<inspect::UintProperty>,
    /// Timestamp of when the last time a device was bound.
    last_bound: Arc<inspect::IntProperty>,
    /// The number of observed devices to appear in CTAPHID_PATH.
    devices_count: Arc<inspect::UintProperty>,
    /// Timestamp of when the last time a appeared in CTAPHID_PATH.
    time_device_seen: Arc<inspect::IntProperty>,
}

impl CtapAgentMetrics {
    fn new(node: inspect::Node) -> Self {
        let bindings_count = Arc::new(node.create_uint("bindings_count", 0));
        let last_bound = Arc::new(node.create_int("last_bound", 0));
        let devices_count = Arc::new(node.create_uint("devices_count", 0));
        let time_device_seen = Arc::new(node.create_int("time_device_seen", 0));
        Self { _node: node, bindings_count, devices_count, last_bound, time_device_seen }
    }
}

pub struct DeviceWatcher {
    /// Contains the ctap device currently bound to the ctap agent.
    device_binding: DeviceBinding,
    watcher: Watcher,
    dir_proxy: DirectoryProxy,
}

pub struct CtapAgent {
    device_watcher: Arc<Mutex<Option<DeviceWatcher>>>,
    /// Metrics used for inspecting current state.
    metrics: Arc<CtapAgentMetrics>,
}

impl CtapAgent {
    /// Creates a new CtapAgent and starts the watcher.
    pub fn new(node: inspect::Node) -> Arc<Self> {
        let agent = Arc::new(CtapAgent {
            metrics: Arc::new(CtapAgentMetrics::new(node)),
            device_watcher: Arc::new(Mutex::new(None)),
        });

        let agent_ref = agent.clone();

        fasync::Task::local(async move {
            let watcher = match Self::get_watcher().await {
                Ok(watcher) => watcher,
                Err(err) => {
                    error!(
                        "Ctap Agent is unable to watch the ctap device directory. New devices will \
                        not be supported. {:?}",
                        err
                    );
                    return;
                }
            };

            let dir_proxy = match fuchsia_fs::directory::open_in_namespace(
                CTAPHID_PATH,
                fio::OpenFlags::RIGHT_READABLE,
            ) {
                Ok(dir_proxy) => dir_proxy,
                Err(err) => {
                    error!("Unable to open fido report directory. {:?}", err);
                    return;
                }
            };

            let mut device_watcher = agent_ref.device_watcher.lock().await;
            *device_watcher = Some(DeviceWatcher {
                device_binding: Arc::new(Mutex::new(None)),
                watcher,
                dir_proxy,
            });

            match agent_ref.watch_for_devices(false).await {
                Ok(()) => (),
                Err(err) => {
                    error!("Failed to watch for devices. {:?}", err);
                    return;
                }
            };
        })
        .detach();

        agent
    }

    /// Creates a new CtapAgent containing `watcher` and `dir_proxy` without starting to watch for
    /// devices.
    pub fn new_for_test(
        node: inspect::Node,
        watcher: Watcher,
        dir_proxy: DirectoryProxy,
    ) -> Arc<Self> {
        let device_watcher = Arc::new(Mutex::new(Some(DeviceWatcher {
            device_binding: Arc::new(Mutex::new(None)),
            watcher,
            dir_proxy,
        })));

        Arc::new(CtapAgent { metrics: Arc::new(CtapAgentMetrics::new(node)), device_watcher })
    }

    /// Returns a `fuchsia_vfs_watcher::Watcher` to the fido report directory.
    async fn get_watcher() -> Result<Watcher, Error> {
        let fido_report_dir_proxy = fuchsia_fs::directory::open_in_namespace(
            CTAPHID_PATH,
            fuchsia_fs::OpenFlags::RIGHT_READABLE,
        )?;

        Watcher::new(fido_report_dir_proxy).await
    }

    /// Watches the fido report directory for new security key devices. Populates the supplied
    /// `DeviceBinding` with a CtapHidDevice upon availability of a device.
    async fn watch_for_devices(self: &Arc<Self>, break_on_idle: bool) -> Result<(), Error> {
        let watcher = &mut *self.device_watcher.lock().await;
        let watcher = if let Some(ref mut watcher) = watcher {
            watcher
        } else {
            return Err(format_err!("watch_for_devices called with no watcher available."));
        };

        while let Some(msg) = watcher.watcher.try_next().await? {
            let filename = match msg.filename.into_os_string().into_string() {
                Ok(f) if f == "." => continue,
                Ok(f) => f,
                Err(e) => {
                    warn!("Unable to parse filename {:?}", e);
                    continue;
                }
            };

            match msg.event {
                WatchEvent::EXISTING | WatchEvent::ADD_FILE => {
                    let curr_timestamp = fasync::Time::now().into_nanos();
                    self.metrics.devices_count.add(1);
                    self.metrics.time_device_seen.set(curr_timestamp);

                    let pathbuf = PathBuf::from(filename);
                    let device_binding = watcher.device_binding.clone();
                    let mut device_binding = device_binding.lock().await;

                    // Check if a device is already bound, and if so, check if it is still active.
                    if let Some(existing_device) = &*device_binding {
                        match existing_device.ping().await {
                            Ok(_) => {
                                info!("Security key device connected but one was already bound.");
                                continue;
                            }
                            Err(_) => {
                                info!("Attempting to bind to new security key device.")
                            }
                        }
                    }

                    *device_binding =
                        Some(CtapHidDevice::device(&(watcher.dir_proxy), &pathbuf).await?);
                    self.metrics.bindings_count.add(1);
                    self.metrics.last_bound.set(curr_timestamp);
                }
                WatchEvent::IDLE => {
                    // So tests don't hang.
                    if break_on_idle {
                        break;
                    }
                }
                _ => (),
            }
        }
        return Err(format_err!("Ctap agent stopped watching for new security key devices."));
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        bytes::{BufMut, BytesMut},
        fidl::endpoints::create_proxy,
        fidl_fuchsia_fido_report::SecurityKeyDeviceRequest,
        fuchsia_inspect::{self as inspect, assert_data_tree},
        futures::FutureExt,
        std::collections::VecDeque,
        vfs::{
            directory::entry::DirectoryEntry,
            directory::immutable::connection::io1::ImmutableConnection, directory::simple::Simple,
            execution_scope::ExecutionScope, path::Path, pseudo_directory,
            service as pseudo_fs_service,
        },
    };

    const INIT_CHANNEL: u32 = 0xFFFFFFFF;
    const TEST_CHANNEL_1: u32 = 0xDEADBEEF;
    const TEST_CHANNEL_2: u32 = 0x12345678;

    /// A fake device where responses to the SecurityKeyDeviceProxy can be manually set.
    pub struct FakeDevice {
        /// A queue of responses to be returned from a `GetMessage` request.
        responses: Mutex<VecDeque<fidl_fuchsia_fido_report::Message>>,
        /// The channel assigned to be returned from the Init response.
        assigned_channel: u32,
        /// Whether or not a ping response will be queued upon receiving a `SendMessage` request for
        /// Init.
        expects_ping: bool,
    }

    impl FakeDevice {
        /// Constructs a new Fake SecurityKeyDeviceProxy.
        pub fn new(assigned_channel: u32, expects_ping: bool) -> FakeDevice {
            FakeDevice { responses: Mutex::new(VecDeque::new()), assigned_channel, expects_ping }
        }

        /// Adds `message` to the front of the responses queue.
        pub fn expect_response(&self, message: fidl_fuchsia_fido_report::Message) {
            self.responses.try_lock().unwrap().push_back(message);
        }

        /// Adds a constructed Init response to the front of the responses queue.
        fn expect_init_response(&self, nonce: Vec<u8>) {
            // Set up some arbitrary Init response data.
            let mut response_payload = BytesMut::with_capacity(17);
            response_payload.put(&nonce[..]);
            response_payload.put_u32(self.assigned_channel);
            response_payload.put_u8(1); /* CTAPHID protocol version */
            response_payload.put_u8(0xe1); /* Unused major version */
            response_payload.put_u8(0xe2); /* Unused minor version */
            response_payload.put_u8(0xe3); /* Unused build version */
            response_payload.put_u8(0x04); /* Aribitrary capabilities */
            self.responses.try_lock().unwrap().push_back(fidl_fuchsia_fido_report::Message {
                channel_id: Some(INIT_CHANNEL),
                command_id: Some(fidl_fuchsia_fido_report::CtapHidCommand::Init),
                payload_len: Some(response_payload.len() as u16),
                data: Some(response_payload.to_vec()),
                ..fidl_fuchsia_fido_report::Message::EMPTY
            });
        }

        /// Handles any requests sent to the fake device. Will queue a response for Init and Ping
        /// commands.
        async fn handle_device_request(&self, fido_device_request: SecurityKeyDeviceRequest) {
            match fido_device_request {
                fidl_fuchsia_fido_report::SecurityKeyDeviceRequest::SendMessage {
                    responder,
                    payload,
                } => {
                    if payload.command_id == Some(fidl_fuchsia_fido_report::CtapHidCommand::Init) {
                        self.expect_init_response(payload.data.as_ref().unwrap().to_vec());
                    } else if payload.command_id
                        == Some(fidl_fuchsia_fido_report::CtapHidCommand::Ping)
                    {
                        if self.expects_ping {
                            self.expect_response(fidl_fuchsia_fido_report::Message {
                                channel_id: payload.channel_id,
                                command_id: payload.command_id,
                                payload_len: Some(payload.data.as_ref().unwrap().len() as u16),
                                data: payload.data,
                                ..fidl_fuchsia_fido_report::Message::EMPTY
                            });
                        }
                    }
                    let _ = responder.send(&mut Ok(()));
                }
                fidl_fuchsia_fido_report::SecurityKeyDeviceRequest::GetMessage {
                    responder,
                    ..
                } => {
                    let response = self.responses.lock().await.pop_front().unwrap();
                    let _ = responder.send(&mut Ok(response));
                }
            }
        }
    }

    /// Returns a tuple containing a watcher for the given directory `dir` and a proxy to the
    /// directory.
    async fn setup_watchers(
        dir: std::sync::Arc<Simple<ImmutableConnection>>,
    ) -> (Watcher, DirectoryProxy) {
        // Create a watcher on the pseudo directory.
        let pseudo_dir_clone = dir.clone();
        let (dir_proxy_for_watcher, dir_server_for_watcher) =
            create_proxy::<fio::DirectoryMarker>().unwrap();
        let server_end_for_watcher = dir_server_for_watcher.into_channel().into();
        let scope_for_watcher = ExecutionScope::new();
        dir.open(
            scope_for_watcher,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            0,
            Path::dot(),
            server_end_for_watcher,
        );
        let device_watcher = Watcher::new(dir_proxy_for_watcher).await.unwrap();

        // Get a proxy to the pseudo directory for the ctap agent. The ctap agent may use this
        // proxy to get connections to ctaphid devices.
        let (dir_proxy_for_agent, dir_server_for_agent) =
            create_proxy::<fio::DirectoryMarker>().unwrap();
        let server_end_for_agent = dir_server_for_agent.into_channel().into();
        let scope_for_agent = ExecutionScope::new();
        pseudo_dir_clone.open(
            scope_for_agent,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            0,
            Path::dot(),
            server_end_for_agent,
        );

        (device_watcher, dir_proxy_for_agent)
    }

    // Test one device connects.
    #[fuchsia::test]
    async fn watch_devices_one_exists() {
        let inspector = inspect::Inspector::new();

        // Create a file in a pseudo directory that represents a ctaphid device.
        let mut count: i8 = 0;
        let dir = pseudo_directory! {
            "001" => pseudo_fs_service::host(
                move |mut request_stream: fidl_fuchsia_fido_report::SecurityKeyDeviceRequestStream| {
                    let fake_device_proxy = FakeDevice::new(TEST_CHANNEL_1, false);
                    async move {
                        while count < 2 {
                                if let Some(request) =
                                request_stream.try_next().await.unwrap()
                            {
                                fake_device_proxy.handle_device_request(request).await;
                                count += 1;
                            }
                        }
                    }.boxed()
                },
            )
        };

        let (watcher, dir_proxy_for_agent) = setup_watchers(dir).await;
        let node = inspector.root().create_child("ctap_agent_service");

        let agent = CtapAgent::new_for_test(node, watcher, dir_proxy_for_agent);
        let _ = agent.watch_for_devices(true).await;

        assert_data_tree!(inspector, root: {
            ctap_agent_service: {
                bindings_count: 1u64,
                devices_count: 1u64,
                last_bound: inspect::testing::AnyProperty,
                time_device_seen: inspect::testing::AnyProperty,
            }
        });

        // Assert that a device was found with the correct channel.
        let device_watcher = agent.device_watcher.lock().await;
        let current_binding = device_watcher.as_ref().unwrap().device_binding.lock().await;
        assert!(current_binding.is_some());
        assert_eq!(current_binding.as_ref().unwrap().channel(), TEST_CHANNEL_1);
    }

    // Tests a second device becomes available but the first is still connected.
    #[fuchsia::test]
    async fn watch_devices_second_failed() {
        let inspector = inspect::Inspector::new();

        // Create the multiple files in a pseudo directory that represent ctaphid devices.
        let mut count: i8 = 0;
        let dir = pseudo_directory! {
            "001" => pseudo_fs_service::host(
                move |mut request_stream: fidl_fuchsia_fido_report::SecurityKeyDeviceRequestStream| {
                    // `expect_ping` set to true so that the device appears visible when the watcher
                    // checks the second.
                    let fake_device_proxy = FakeDevice::new(TEST_CHANNEL_1, true);
                    async move {
                        while count < 4 {
                                if let Some(request) =
                                request_stream.try_next().await.unwrap()
                            {
                                fake_device_proxy.handle_device_request(request).await;
                                count += 1;
                            }
                        }
                    }.boxed()
                },
            ),
            "002" => pseudo_fs_service::host(
                move |mut request_stream: fidl_fuchsia_fido_report::SecurityKeyDeviceRequestStream| {
                    let fake_device_proxy = FakeDevice::new(TEST_CHANNEL_2, false);
                    async move {
                        while count < 2 {
                                if let Some(request) =
                                request_stream.try_next().await.unwrap()
                            {
                                fake_device_proxy.handle_device_request(request).await;
                                count += 1;
                            }
                        }
                    }.boxed()
                },
            )
        };

        let (watcher, dir_proxy_for_agent) = setup_watchers(dir).await;
        let node = inspector.root().create_child("ctap_agent_service");

        let agent = CtapAgent::new_for_test(node, watcher, dir_proxy_for_agent);
        let _ = agent.watch_for_devices(true).await;

        assert_data_tree!(inspector, root: {
            ctap_agent_service: {
                bindings_count: 1u64,
                devices_count: 2u64,
                last_bound: inspect::testing::AnyProperty,
                time_device_seen: inspect::testing::AnyProperty,
            }
        });

        // Assert that a device was found with the correct channel.
        // Channel should be that of the first device that was connected.
        let device_watcher = agent.device_watcher.lock().await;
        let current_binding = device_watcher.as_ref().unwrap().device_binding.lock().await;
        assert!(current_binding.is_some());
        assert_eq!(current_binding.as_ref().unwrap().channel(), TEST_CHANNEL_1);
    }

    // Tests a second device becomes available after the first has already disconnected.
    #[fuchsia::test]
    async fn watch_devices_second_success() {
        let inspector = inspect::Inspector::new();

        // Create the multiple files in a pseudo directory that represent ctaphid devices.
        let mut count: i8 = 0;
        let dir = pseudo_directory! {
            "001" => pseudo_fs_service::host(
                move |mut request_stream: fidl_fuchsia_fido_report::SecurityKeyDeviceRequestStream| {
                    let fake_device_proxy = FakeDevice::new(TEST_CHANNEL_1, false);
                    async move {
                        while count < 2 {
                                if let Some(request) =
                                request_stream.try_next().await.unwrap()
                            {
                                fake_device_proxy.handle_device_request(request).await;
                                count += 1;
                            }
                        }
                    }.boxed()
                },
            ),
            "002" => pseudo_fs_service::host(
                move |mut request_stream: fidl_fuchsia_fido_report::SecurityKeyDeviceRequestStream| {
                    let fake_device_proxy = FakeDevice::new(TEST_CHANNEL_2, false);
                    async move {
                        while count < 2 {
                                if let Some(request) =
                                request_stream.try_next().await.unwrap()
                            {
                                fake_device_proxy.handle_device_request(request).await;
                                count += 1;
                            }
                        }
                    }.boxed()
                },
            )
        };

        let (watcher, dir_proxy_for_agent) = setup_watchers(dir).await;
        let node = inspector.root().create_child("ctap_agent_service");

        let agent = CtapAgent::new_for_test(node, watcher, dir_proxy_for_agent);
        let _ = agent.watch_for_devices(true).await;

        assert_data_tree!(inspector, root: {
            ctap_agent_service: {
                bindings_count: 2u64,
                devices_count: 2u64,
                last_bound: inspect::testing::AnyProperty,
                time_device_seen: inspect::testing::AnyProperty,
            }
        });

        // Assert that a device was found with the correct channel.
        // Channel should be that of the second device that was connected.
        let device_watcher = agent.device_watcher.lock().await;
        let current_binding = device_watcher.as_ref().unwrap().device_binding.lock().await;
        assert!(current_binding.is_some());
        assert_eq!(current_binding.as_ref().unwrap().channel(), TEST_CHANNEL_2);
    }
}
