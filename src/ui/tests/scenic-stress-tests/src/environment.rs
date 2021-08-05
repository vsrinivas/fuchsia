// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{input_actor::InputActor, session::Session, session_actor::SessionActor, Args},
    async_trait::async_trait,
    fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys, fidl_fuchsia_ui_scenic as fscenic,
    fuchsia_component::client::{connect_to_protocol, connect_to_protocol_at_dir_root},
    futures::lock::Mutex,
    rand::{rngs::SmallRng, Rng, SeedableRng},
    std::sync::Arc,
    std::time::Duration,
    stress_test::{actor::ActorRunner, environment::Environment, random_seed},
};

/// Contains the running instance of scenic and the actors that operate on it.
/// This object lives for the entire duration of the test.
pub struct ScenicEnvironment {
    args: Args,
    scenic_exposed_dir: fio::DirectoryProxy,
}

impl ScenicEnvironment {
    pub async fn new(args: Args) -> Self {
        // Bind to the scenic component, causing it to start
        let realm_svc =
            connect_to_protocol::<fsys::RealmMarker>().expect("Could not connect to Realm service");
        let mut child = fsys::ChildRef { name: "scenic".to_string(), collection: None };

        // Create endpoints for the fuchsia.io.Directory protocol.
        // Component manager will connect us to the exposed directory of the component we bound to.
        let (scenic_exposed_dir, server_end) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        realm_svc
            .open_exposed_dir(&mut child, server_end)
            .await
            .expect("Could not send open_exposed_dir command")
            .expect("open_exposed_dir command did not succeed");

        Self { args, scenic_exposed_dir }
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
        let scenic_proxy =
            connect_to_protocol_at_dir_root::<fscenic::ScenicMarker>(&self.scenic_exposed_dir)
                .expect(
                    "Could not connect to Scenic protocol from scenic component exposed directory",
                );
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
