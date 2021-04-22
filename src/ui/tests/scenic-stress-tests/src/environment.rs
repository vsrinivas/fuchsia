// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        display_provider::{DisplayControllerProviderInjector, DisplayState},
        input_actor::InputActor,
        session::Session,
        session_actor::SessionActor,
        Args,
    },
    async_trait::async_trait,
    component_events::{
        events::{Event, EventMode, EventSource, EventSubscription, Started},
        injectors::{CapabilityInjector, TestNamespaceInjector},
        matcher::EventMatcher,
    },
    fidl_fuchsia_ui_scenic as fscenic,
    fuchsia_component::client::connect_to_service_at,
    futures::lock::Mutex,
    rand::{rngs::SmallRng, Rng, SeedableRng},
    std::sync::Arc,
    std::time::Duration,
    stress_test::{actor::ActorRunner, environment::Environment, random_seed},
    test_utils_lib::opaque_test::OpaqueTest,
};

const ROOT_URL: &str = "fuchsia-pkg://fuchsia.com/scenic-stress-tests#meta/scenic.cm";

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
/// Waits for the scenic to start before returning.
async fn init_scenic() -> (OpaqueTest, DisplayState) {
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

    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Started::NAME], EventMode::Async)])
        .await
        .unwrap();

    // Start all the components
    event_source.start_component_tree().await;

    // Wait for scenic to start
    event_stream.next().await.unwrap();

    (test, display_state)
}

/// Contains the running instance of scenic and the actors that operate on it.
/// This object lives for the entire duration of the test.
pub struct ScenicEnvironment {
    args: Args,
    opaque_test: OpaqueTest,
    _display_state: DisplayState,
}

impl ScenicEnvironment {
    pub async fn new(args: Args) -> Self {
        // Start Scenic and wait for it to be ready
        let (opaque_test, display_state) = init_scenic().await;

        Self { args, opaque_test, _display_state: display_state }
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
        let seed = random_seed();
        let mut rng = SmallRng::from_seed(seed.to_le_bytes());

        // Connect to the Scenic protocol
        let svc_dir_path = self.opaque_test.get_hub_v2_path().join("exec/out/svc");
        let svc_dir_path = svc_dir_path.to_str().unwrap();
        let scenic_proxy = connect_to_service_at::<fscenic::ScenicMarker>(svc_dir_path)
            .expect("Could not connect to InputDeviceRegistry");
        let scenic_proxy = Arc::new(scenic_proxy);

        // Create the root session
        let (root_session, compositor_id, session_ptr) =
            Session::initialize_as_root(&mut rng, scenic_proxy);

        let input_runner = {
            // Create the input actor
            let seed = rng.gen::<u128>();
            let rng = SmallRng::from_seed(seed.to_le_bytes());
            let input_actor =
                Arc::new(Mutex::new(InputActor::new(rng, session_ptr, compositor_id)));
            ActorRunner::new("input_actor", Some(Duration::from_millis(16)), input_actor)
        };

        let session_runner = {
            // Create the session actor
            let seed = rng.gen::<u128>();
            let rng = SmallRng::from_seed(seed.to_le_bytes());
            let session_actor = Arc::new(Mutex::new(SessionActor::new(rng, root_session)));
            ActorRunner::new("session_actor", Some(Duration::from_millis(250)), session_actor)
        };

        vec![session_runner, input_runner]
    }

    async fn reset(&mut self) {
        unreachable!("This stress test does not reset");
    }
}
