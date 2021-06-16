// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::error::PowerManagerError;
use crate::message::{Message, MessageReturn};
use crate::node::Node;
use crate::shutdown_request::ShutdownRequest;
use crate::utils::connect_proxy;
use anyhow::{format_err, Error};
use async_trait::async_trait;
use fidl_fuchsia_hardware_input::{DeviceMarker as LidMarker, DeviceProxy as LidProxy};
use fuchsia_inspect::{self as inspect, NumericProperty, Property};
use fuchsia_inspect_contrib::{inspect_log, nodes::BoundedListNode};
use fuchsia_vfs_watcher as vfs;
use fuchsia_zircon::{self as zx, AsHandleRef};
use futures::{
    future::{FutureExt, LocalBoxFuture},
    stream::FuturesUnordered,
    TryStreamExt,
};
use io_util::{open_directory_in_namespace, OPEN_RIGHT_READABLE};
use log::*;
use serde_derive::Deserialize;
use serde_json as json;
use std::{
    cell::RefCell,
    collections::HashMap,
    path::{Path, PathBuf},
    rc::Rc,
};

/// Node: LidShutdown
///
/// Summary: Responds to lid closed events from devices with a lid sensor by waiting for a report
///          using the input FIDL protocol.
///
/// Handles Messages: N/A
///
/// Sends Messages:
///     - SystemShutdown
///
/// FIDL dependencies:
///     - fuchsia.hardware.input: the node uses this protocol to wait on reports from the
///       lid device

/// The lid sensor is not a real HID device however this descriptor is defined in the lid driver
/// and will be used to send lid HID reports for any ACPI lid sensor.
const HID_LID_DESCRIPTOR: [u8; 9] = [
    0x05, 0x01, // Usage Page (Generic Desktop)
    0x09, 0x80, // Usage (System Control)
    0xA1, 0x01, // Collection (Application)
    0x0A, 0xFF, 0x01, //   Usage (0x01FF, unique to lid reports)
];

// Lid closed HID report
const LID_CLOSED: u8 = 0x0;

static INPUT_DEVICES_DIRECTORY: &str = "/dev/class/input";

pub struct LidShutdownBuilder<'a> {
    proxy: Option<LidProxy>,
    lid_report_event: Option<zx::Event>,
    system_shutdown_node: Rc<dyn Node>,
    inspect_root: Option<&'a inspect::Node>,
}

impl<'a> LidShutdownBuilder<'a> {
    pub fn new(system_shutdown_node: Rc<dyn Node>) -> Self {
        LidShutdownBuilder {
            proxy: None,
            lid_report_event: None,
            system_shutdown_node,
            inspect_root: None,
        }
    }

    #[cfg(test)]
    pub fn new_with_event_and_proxy(
        proxy: LidProxy,
        lid_report_event: zx::Event,
        system_shutdown_node: Rc<dyn Node>,
    ) -> Self {
        Self {
            proxy: Some(proxy),
            lid_report_event: Some(lid_report_event),
            system_shutdown_node,
            inspect_root: None,
        }
    }

    pub fn new_from_json(json_data: json::Value, nodes: &HashMap<String, Rc<dyn Node>>) -> Self {
        #[derive(Deserialize)]
        struct Dependencies {
            system_shutdown_node: String,
        }

        #[derive(Deserialize)]
        struct JsonData {
            dependencies: Dependencies,
        }

        let data: JsonData = json::from_value(json_data).unwrap();
        Self::new(nodes[&data.dependencies.system_shutdown_node].clone())
    }

    #[cfg(test)]
    pub fn with_inspect_root(mut self, root: &'a inspect::Node) -> Self {
        self.inspect_root = Some(root);
        self
    }

    pub async fn build<'b>(
        self,
        futures_out: &FuturesUnordered<LocalBoxFuture<'b, ()>>,
    ) -> Result<Rc<LidShutdown>, Error> {
        // In tests use the default proxy.
        let proxy = match self.proxy {
            Some(proxy) => proxy,
            None => Self::find_lid_sensor().await?,
        };

        // In tests use the default event.
        let report_event = match self.lid_report_event {
            Some(report_event) => report_event,
            None => match proxy.get_reports_event().await {
                Ok((_, report_event)) => report_event,
                Err(_e) => return Err(format_err!("Could not get report event.")),
            },
        };

        // In tests use the default inspect root node
        let inspect_root = self.inspect_root.unwrap_or(inspect::component::inspector().root());

        let node = Rc::new(LidShutdown {
            proxy,
            report_event,
            system_shutdown_node: self.system_shutdown_node,
            inspect: InspectData::new(inspect_root, "LidShutdown".to_string()),
        });

        futures_out.push(node.clone().watch_lid());
        Ok(node)
    }

    /// Checks all the input devices until the lid sensor is found.
    async fn find_lid_sensor() -> Result<LidProxy, Error> {
        let dir_proxy = open_directory_in_namespace(INPUT_DEVICES_DIRECTORY, OPEN_RIGHT_READABLE)?;

        let mut watcher = vfs::Watcher::new(dir_proxy).await?;

        while let Some(msg) = watcher.try_next().await? {
            match msg.event {
                vfs::WatchEvent::EXISTING | vfs::WatchEvent::ADD_FILE => {
                    match Self::open_sensor(&msg.filename).await {
                        Ok(device) => return Ok(device),
                        _ => (),
                    }
                }
                _ => (),
            }
        }

        Err(format_err!("No lid device found"))
    }

    /// Opens the sensor's device file. Returns the device if the correct HID
    /// report descriptor is found.
    async fn open_sensor(filename: &PathBuf) -> Result<LidProxy, Error> {
        let path = Path::new(INPUT_DEVICES_DIRECTORY).join(filename);
        let device = connect_proxy::<LidMarker>(&String::from(
            path.to_str().ok_or(format_err!("Could not read path {:?}", path))?,
        ))?;
        if let Ok(device_descriptor) = device.get_report_desc().await {
            if device_descriptor.len() < HID_LID_DESCRIPTOR.len() {
                return Err(format_err!("Short HID header"));
            }
            let device_header = &device_descriptor[0..HID_LID_DESCRIPTOR.len()];
            if device_header == HID_LID_DESCRIPTOR {
                return Ok(device);
            } else {
                return Err(format_err!("Device is not lid sensor"));
            }
        }
        Err(format_err!("Could not get device HID report descriptor"))
    }
}

pub struct LidShutdown {
    proxy: LidProxy,

    /// Event that will signal |USER_0| when a report is in the lid device's report FIFO.
    report_event: zx::Event,

    /// Node to provide the system shutdown functionality via the SystemShutdown message.
    system_shutdown_node: Rc<dyn Node>,

    /// A struct for managing Component Inspection data
    inspect: InspectData,
}

impl LidShutdown {
    fn watch_lid<'a>(self: Rc<Self>) -> LocalBoxFuture<'a, ()> {
        async move {
            loop {
                self.watch_lid_inner().await;
            }
        }
        .boxed_local()
    }

    /// Watches the lid device for reports.
    async fn watch_lid_inner(&self) {
        match self.report_event.wait_handle(zx::Signals::USER_0, zx::Time::INFINITE) {
            Err(e) => error!("Could not wait for lid event: {:?}", e),
            _ => match self.check_report().await {
                Ok(()) => (),
                Err(e) => {
                    self.inspect.read_errors.add(1);
                    self.inspect.last_read_error.set(format!("{}", e).as_str());
                    error!("Failed to read lid report: {}", e)
                }
            },
        };
    }

    /// Reads the report from the lid sensor and sends shutdown signal if lid is closed.
    async fn check_report(&self) -> Result<(), Error> {
        let (status, report, _time) = self.proxy.read_report().await?;
        let status = zx::Status::from_raw(status);
        if status != zx::Status::OK {
            return Err(format_err!("Error reading report {}", status));
        }
        if report.len() != 1 {
            return Err(format_err!("Expected single byte report, found {:?}", report));
        }
        self.inspect.log_lid_report(format!("{:?}", report));
        let report = report[0];

        if report == LID_CLOSED {
            info!("Lid closed. Shutting down...");
            self.send_message(
                &self.system_shutdown_node,
                &Message::SystemShutdown(ShutdownRequest::PowerOff),
            )
            .await
            .map_err(|e| format_err!("Failed to shut down the system: {:?}", e))?;
        }
        Ok(())
    }
}

#[async_trait(?Send)]
impl Node for LidShutdown {
    fn name(&self) -> String {
        "LidShutdown".to_string()
    }

    async fn handle_message(&self, _msg: &Message) -> Result<MessageReturn, PowerManagerError> {
        Err(PowerManagerError::Unsupported)
    }
}

struct InspectData {
    lid_reports: RefCell<BoundedListNode>,
    read_errors: inspect::UintProperty,
    last_read_error: inspect::StringProperty,
}

impl InspectData {
    /// Number of inspect samples to store in the `lid_reports` BoundedListNode.
    // Store the last 60 lid reports
    const NUM_INSPECT_LID_REPORTS: usize = 60;

    fn new(parent: &inspect::Node, name: String) -> Self {
        // Create a local root node and properties
        let root = parent.create_child(name);
        let lid_reports = RefCell::new(BoundedListNode::new(
            root.create_child("lid_reports"),
            Self::NUM_INSPECT_LID_REPORTS,
        ));
        let read_errors = root.create_uint("read_lid_report_error_count", 0);
        let last_read_error = root.create_string("last_read_error", "");

        // Pass ownership of the new node to the parent node, otherwise it'll be dropped
        parent.record(root);

        InspectData { lid_reports, read_errors, last_read_error }
    }

    fn log_lid_report(&self, lid_report: String) {
        inspect_log!(self.lid_reports.borrow_mut(), lid_report: lid_report);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test::mock_node::{create_dummy_node, MessageMatcher, MockNodeMaker};
    use crate::{msg_eq, msg_ok_return};
    use fidl_fuchsia_hardware_input as finput;
    use fuchsia_async as fasync;
    use fuchsia_inspect::testing::TreeAssertion;
    use fuchsia_zircon::HandleBased;
    use inspect::assert_data_tree;

    const LID_OPEN: u8 = 0x1;

    /// Spawns a new task that acts as a fake device driver for testing purposes. The driver only
    /// handles requests for ReadReport - trying to send any other requests to it is a bug.
    /// Each ReadReport responds with the |lid_report| specified.
    fn setup_fake_driver(lid_report: u8) -> LidProxy {
        let (proxy, mut stream) = fidl::endpoints::create_proxy_and_stream::<LidMarker>().unwrap();
        fasync::Task::local(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(finput::DeviceRequest::ReadReport { responder }) => {
                        let _ = responder.send(zx::Status::OK.into_raw(), &[lid_report], 0 as i64);
                    }
                    _ => assert!(false),
                }
            }
        })
        .detach();

        proxy
    }

    /// Tests that well-formed configuration JSON does not panic the `new_from_json` function.
    #[fasync::run_singlethreaded(test)]
    async fn test_new_from_json() {
        let json_data = json::json!({
            "type": "LidShutdown",
            "name": "lid_shutdown",
            "dependencies": {
                "system_shutdown_node": "shutdown",
              },
        });

        let mut nodes: HashMap<String, Rc<dyn Node>> = HashMap::new();
        nodes.insert("shutdown".to_string(), create_dummy_node());
        let _ = LidShutdownBuilder::new_from_json(json_data, &nodes);
    }

    /// Tests that when the node receives a signal on its |report_event|, it checks for a lid
    /// report and, on reception of a lid closed report, it triggers a system shutdown.
    #[fasync::run_singlethreaded(test)]
    async fn test_triggered_shutdown() {
        let mut mock_maker = MockNodeMaker::new();
        let shutdown_node = mock_maker.make(
            "Shutdown",
            vec![(
                msg_eq!(SystemShutdown(ShutdownRequest::PowerOff)),
                msg_ok_return!(SystemShutdown),
            )],
        );

        let event = zx::Event::create().unwrap();
        let node = LidShutdownBuilder::new_with_event_and_proxy(
            setup_fake_driver(LID_CLOSED),
            event.duplicate_handle(zx::Rights::BASIC).unwrap(),
            shutdown_node,
        )
        .build(&FuturesUnordered::new())
        .await
        .unwrap();

        event
            .signal_handle(zx::Signals::NONE, zx::Signals::USER_0)
            .expect("Failed to signal event");

        node.watch_lid_inner().await;

        // When mock_maker goes out of scope, it verifies the ShutdownNode received the shutdown
        // request.
    }

    /// Tests that when the node receives a signal on its |report_event|, it checks for a lid
    /// report and, on reception of a lid open report, it does NOT trigger a system shutdown.
    #[fasync::run_singlethreaded(test)]
    async fn test_event_handling() {
        let mut mock_maker = MockNodeMaker::new();
        let shutdown_node = mock_maker.make(
            "Shutdown",
            vec![], // the shutdown node is not expected to receive any messages
        );

        let event = zx::Event::create().unwrap();
        let node = LidShutdownBuilder::new_with_event_and_proxy(
            setup_fake_driver(LID_OPEN),
            event.duplicate_handle(zx::Rights::BASIC).unwrap(),
            shutdown_node,
        )
        .build(&FuturesUnordered::new())
        .await
        .unwrap();

        event
            .signal_handle(zx::Signals::NONE, zx::Signals::USER_0)
            .expect("Failed to signal event");

        node.watch_lid_inner().await;

        // When mock_maker will verify that the ShutdownNode receives no messages until it goes
        // out of scope.
    }

    /// Tests that an unsupported message is handled gracefully and an error is returned.
    #[fasync::run_singlethreaded(test)]
    async fn test_unsupported_msg() {
        let mut mock_maker = MockNodeMaker::new();
        let shutdown_node = mock_maker.make("Shutdown", vec![]);

        let node_futures = FuturesUnordered::new();
        let node = LidShutdownBuilder::new_with_event_and_proxy(
            setup_fake_driver(LID_CLOSED),
            zx::Event::create().unwrap(),
            shutdown_node,
        )
        .build(&node_futures)
        .await
        .unwrap();
        match node.handle_message(&Message::GetTotalCpuLoad).await {
            Err(PowerManagerError::Unsupported) => {}
            e => panic!("Unexpected return value: {:?}", e),
        }
    }

    /// Tests for the presence and correctness of dynamically-added inspect data
    #[fasync::run_singlethreaded(test)]
    async fn test_inspect_data() {
        let lid_state = LID_OPEN;
        let inspector = inspect::Inspector::new();
        let mut mock_maker = MockNodeMaker::new();
        let shutdown_node = mock_maker.make("Shutdown", vec![]);
        let event = zx::Event::create().unwrap();

        let node_futures = FuturesUnordered::new();
        let node = LidShutdownBuilder::new_with_event_and_proxy(
            setup_fake_driver(lid_state),
            event.duplicate_handle(zx::Rights::BASIC).unwrap(),
            shutdown_node,
        )
        .with_inspect_root(inspector.root())
        .build(&node_futures)
        .await
        .unwrap();

        // The node will read the current temperature and log the sample into Inspect. Read enough
        // samples to test that the correct number of samples are logged and older ones are dropped.
        for _ in 0..InspectData::NUM_INSPECT_LID_REPORTS + 10 {
            event
                .signal_handle(zx::Signals::NONE, zx::Signals::USER_0)
                .expect("Failed to signal event");
            node.watch_lid_inner().await;
        }

        let mut root = TreeAssertion::new("LidShutdown", false);
        let mut lid_reports = TreeAssertion::new("lid_reports", true);

        // Since we read 10 more samples than our limit allows, the first 10 should be dropped. So
        // test that the sample numbering starts at 10 and continues for the expected number of
        // samples.
        for i in 10..InspectData::NUM_INSPECT_LID_REPORTS + 10 {
            let mut sample_child = TreeAssertion::new(&i.to_string(), true);
            sample_child
                .add_property_assertion("lid_report", Box::new(format!("{:?}", [lid_state])));
            sample_child.add_property_assertion("@time", Box::new(inspect::testing::AnyProperty));
            lid_reports.add_child_assertion(sample_child);
        }

        root.add_child_assertion(lid_reports);
        assert_data_tree!(inspector, root: { root, });
    }
}
