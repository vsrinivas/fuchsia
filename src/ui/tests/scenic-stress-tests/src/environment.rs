// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        display_provider::{DisplayControllerProviderInjector, DisplayState},
        tap_actor::TapActor,
        Args,
    },
    async_trait::async_trait,
    component_events::{
        events::EventSource,
        injectors::{CapabilityInjector, TestNamespaceInjector},
        matcher::EventMatcher,
    },
    fidl::endpoints::create_proxy,
    fidl_fuchsia_ui_input as finput,
    fuchsia_component::client::connect_to_service_at,
    futures::lock::Mutex,
    log::info,
    std::sync::Arc,
    std::time::Duration,
    stress_test::{actor::ActorRunner, environment::Environment},
    test_utils_lib::opaque_test::OpaqueTest,
};

const ROOT_URL: &str = "fuchsia-pkg://fuchsia.com/scenic-stress-tests#meta/root.cm";

/// Injects a capability at |path| by connecting to a test namespace capability
/// at the same |path|.
async fn inject_from_test_namespace(
    name: &'static str,
    path: &'static str,
    event_source: &EventSource,
) {
    TestNamespaceInjector::new(path)
        .inject(event_source, EventMatcher::ok().capability_name(name))
        .await;
}

/// Starts Scenic in an isolated instance of component manager.
/// Injects required dependencies from the system or from the test, as needed.
/// Waits for the display to update at least |min_updates| times before returning.
async fn init_scenic(min_updates: u64) -> (OpaqueTest, DisplayState) {
    let test = OpaqueTest::default(ROOT_URL).await.unwrap();
    let event_source = test.connect_to_event_source().await.unwrap();
    let display_state = DisplayState::new();

    // This directory is needed to start up Scenic's GFX subsystem.
    inject_from_test_namespace(
        "dev-display-controller",
        "/dev/class/display-controller",
        &event_source,
    )
    .await;

    // These directories provide access to the AEMU GPU, needed by Vulkan.
    inject_from_test_namespace(
        "dev-goldfish-address-space",
        "/dev/class/goldfish-address-space",
        &event_source,
    )
    .await;
    inject_from_test_namespace(
        "dev-goldfish-control",
        "/dev/class/goldfish-control",
        &event_source,
    )
    .await;
    inject_from_test_namespace("dev-goldfish-pipe", "/dev/class/goldfish-pipe", &event_source)
        .await;

    // Vulkan needs this directory for loading dynamic libraries
    inject_from_test_namespace("config-vulkan-icd.d", "/config/vulkan/icd.d", &event_source).await;

    // This is the protocol that is used to load Vulkan
    inject_from_test_namespace(
        "fuchsia.vulkan.loader.Loader",
        "/svc/fuchsia.vulkan.loader.Loader",
        &event_source,
    )
    .await;

    // Used by Vulkan
    inject_from_test_namespace(
        "fuchsia.sysmem.Allocator",
        "/svc/fuchsia.sysmem.Allocator",
        &event_source,
    )
    .await;

    // Scenic looks for a display using this protocol
    Arc::new(DisplayControllerProviderInjector::new(display_state.clone()))
        .inject(&event_source, EventMatcher::ok())
        .await;

    // Start all the components
    event_source.start_component_tree().await;

    info!("Waiting for {} display updates...", min_updates);

    // Wait for the display to update the minimum number of times
    while display_state.get_num_updates() < min_updates {}

    (test, display_state)
}

fn create_touch_device(
    registry: &finput::InputDeviceRegistryProxy,
    width: u32,
    height: u32,
) -> finput::InputDeviceProxy {
    let mut device = finput::DeviceDescriptor {
        device_info: None,
        keyboard: None,
        media_buttons: None,
        mouse: None,
        stylus: None,
        touchscreen: Some(Box::new(finput::TouchscreenDescriptor {
            x: finput::Axis {
                range: finput::Range { min: 0, max: width as i32 },
                resolution: 1,
                scale: finput::AxisScale::Linear,
            },
            y: finput::Axis {
                range: finput::Range { min: 0, max: height as i32 },
                resolution: 1,
                scale: finput::AxisScale::Linear,
            },
            max_finger_id: 255,
        })),
        sensor: None,
    };

    let (input_device_proxy, input_device_server) =
        create_proxy::<finput::InputDeviceMarker>().unwrap();
    registry
        .register_device(&mut device, input_device_server)
        .expect("Could not register touch device with InputDeviceRegistry");
    input_device_proxy
}

/// Contains the running instance of scenic and the actors that operate on it.
/// This object lives for the entire duration of the test.
pub struct ScenicEnvironment {
    args: Args,
    _opaque_test: OpaqueTest,
    _display_state: DisplayState,
    tap_actor: Arc<Mutex<TapActor>>,
}

impl ScenicEnvironment {
    pub async fn new(args: Args) -> Self {
        // Start Scenic and wait for it to be ready
        let (opaque_test, display_state) = init_scenic(1).await;

        // Create a touchscreen input device using the InputDeviceRegistry
        let svc_dir_path =
            opaque_test.get_hub_v2_path().join("children/root_presenter/exec/out/svc");
        let svc_dir_path = svc_dir_path.to_str().unwrap();

        let registry = connect_to_service_at::<finput::InputDeviceRegistryMarker>(svc_dir_path)
            .expect("Could not connect to InputDeviceRegistry");

        let device = create_touch_device(&registry, 640, 480);

        // Create the actor that will use the touchscreen input device to send taps.
        let tap_actor = Arc::new(Mutex::new(TapActor::new(device)));

        Self { args, _opaque_test: opaque_test, _display_state: display_state, tap_actor }
    }
}

impl std::fmt::Debug for ScenicEnvironment {
    fn fmt(&self, fmt: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        fmt.debug_struct("ScenicEnvironment").field("args", &self.args).finish()
    }
}

#[async_trait]
impl Environment for ScenicEnvironment {
    fn target_operations(&self) -> Option<u64> {
        self.args.num_operations
    }

    fn timeout_seconds(&self) -> Option<u64> {
        self.args.time_limit_secs
    }

    fn actor_runners(&mut self) -> Vec<ActorRunner> {
        vec![ActorRunner::new(
            "tap_actor",
            Some(Duration::from_secs(self.args.touch_delay_secs)),
            self.tap_actor.clone(),
        )]
    }

    async fn reset(&mut self) {
        unreachable!("This stress test does not reset");
    }
}
