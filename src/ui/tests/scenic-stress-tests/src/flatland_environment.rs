// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::flatland_instance::{DISPLAY_HEIGHT, DISPLAY_WIDTH},
    crate::{
        flatland_actor::FlatlandActor, flatland_instance::FlatlandInstance,
        input_actor::InputActor, Args,
    },
    async_trait::async_trait,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_ui_composition as flatland, fidl_fuchsia_ui_pointerinjector as pointerinjector,
    fuchsia_component_test::{
        Capability, ChildOptions, DirectoryContents, RealmBuilder, RealmInstance, Ref, Route,
    },
    futures::lock::Mutex,
    rand::{rngs::SmallRng, SeedableRng},
    std::{sync::Arc, time::Duration},
    stress_test::{actor::ActorRunner, environment::Environment},
};

/// Contains the running instance of scenic and the actors that operate on it.
/// This object lives for the entire duration of the test.
pub struct FlatlandEnvironment {
    args: Args,
    realm_instance: Arc<RealmInstance>,
}

impl FlatlandEnvironment {
    pub async fn new(args: Args) -> Self {
        let builder = RealmBuilder::new().await.unwrap();
        let hdcp = builder.add_child("hdcp", "#meta/hdcp.cm", ChildOptions::new()).await.unwrap();
        let scenic =
            builder.add_child("scenic", "#meta/scenic.cm", ChildOptions::new()).await.unwrap();

        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.sysmem.Allocator"))
                    .capability(Capability::protocol_by_name("fuchsia.tracing.provider.Registry"))
                    .from(Ref::parent())
                    .to(&scenic)
                    .to(&hdcp),
            )
            .await
            .unwrap();

        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                    .capability(Capability::protocol_by_name("fuchsia.vulkan.loader.Loader"))
                    .from(Ref::parent())
                    .to(&scenic),
            )
            .await
            .unwrap();

        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.hardware.display.Provider"))
                    .from(&hdcp)
                    .to(&scenic),
            )
            .await
            .unwrap();

        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<flatland::FlatlandMarker>())
                    .capability(Capability::protocol::<flatland::FlatlandDisplayMarker>())
                    .capability(Capability::protocol::<pointerinjector::RegistryMarker>())
                    .from(&scenic)
                    .to(Ref::parent()),
            )
            .await
            .unwrap();

        // Override scenic's "i_can_haz_flatland" flag.
        builder
            .read_only_directory(
                "config-data",
                vec![&scenic],
                DirectoryContents::new()
                    .add_file("scenic_config", r#"{ "i_can_haz_flatland": true }"#),
            )
            .await
            .unwrap();
        let realm_instance = Arc::new(builder.build().await.expect("Failed to create realm"));
        Self { args, realm_instance }
    }
}

impl std::fmt::Debug for FlatlandEnvironment {
    fn fmt(&self, fmt: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        fmt.debug_struct("FlatlandEnvironment").field("args", &self.args).finish()
    }
}

#[async_trait]
impl Environment for FlatlandEnvironment {
    fn target_operations(&self) -> Option<u64> {
        self.args.num_operations
    }

    fn timeout_seconds(&self) -> Option<u64> {
        self.args.time_limit_secs
    }

    async fn actor_runners(&mut self) -> Vec<ActorRunner> {
        // We want deterministic but randomly spread values, so we hardcode the seed.
        let mut rng = SmallRng::seed_from_u64(0);

        let (root_flatland_instance, context_view_ref, target_view_ref) =
            FlatlandInstance::new_root(&self.realm_instance.root).await;

        let flatland_instance_runner = {
            // Create the flatland_instance actor
            let rng = SmallRng::from_rng(&mut rng).unwrap();
            let flatland_instance_actor = Arc::new(Mutex::new(FlatlandActor::new(
                rng,
                root_flatland_instance,
                Arc::clone(&self.realm_instance),
            )));
            ActorRunner::new(
                "flatland_instance_actor",
                Some(Duration::from_millis(250)),
                flatland_instance_actor,
            )
        };

        let input_runner = {
            let injector_registry_proxy = self
                .realm_instance
                .root
                .connect_to_protocol_at_exposed_dir::<pointerinjector::RegistryMarker>()
                .expect("Failed to connect to pointerinjector registry");
            let identity_matrix = [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0];
            let viewport = pointerinjector::Viewport {
                extents: Some([[0.0, 0.0], [DISPLAY_WIDTH as f32, DISPLAY_HEIGHT as f32]]),
                viewport_to_context_transform: Some(identity_matrix),
                ..pointerinjector::Viewport::EMPTY
            };
            let config = pointerinjector::Config {
                device_id: Some(0),
                device_type: Some(pointerinjector::DeviceType::Touch),
                context: Some(pointerinjector::Context::View(context_view_ref)),
                target: Some(pointerinjector::Target::View(target_view_ref)),
                viewport: Some(viewport),
                dispatch_policy: Some(pointerinjector::DispatchPolicy::TopHitAndAncestorsInTarget),
                scroll_v_range: None,
                scroll_h_range: None,
                buttons: None,
                ..pointerinjector::Config::EMPTY
            };
            let (device_proxy, device_server) = create_proxy::<pointerinjector::DeviceMarker>()
                .expect("Failed to create DeviceProxy.");
            injector_registry_proxy
                .register(config, device_server)
                .await
                .expect("Failed to register injector");

            // Create the input actor.
            let input_actor =
                Arc::new(Mutex::new(InputActor::new(device_proxy, DISPLAY_WIDTH, DISPLAY_HEIGHT)));
            ActorRunner::new("input_actor", Some(Duration::from_millis(16)), input_actor)
        };

        vec![flatland_instance_runner, input_runner]
    }

    async fn reset(&mut self) {
        unreachable!("This stress test does not reset");
    }
}
