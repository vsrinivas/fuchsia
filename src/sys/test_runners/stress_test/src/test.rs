// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Result},
    fidl::endpoints::create_proxy,
    fidl::Error as FidlError,
    fidl_fuchsia_component_runner::ComponentNamespaceEntry,
    fidl_fuchsia_data::{DictionaryEntry, DictionaryValue},
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_stresstest::{ActorMarker, ActorProxy, Error},
    fidl_fuchsia_sys2::{
        ChildDecl, ChildRef, CollectionRef, CreateChildArgs, RealmMarker, RealmProxy, StartupMode,
    },
    fuchsia_async::{Task, TimeoutExt},
    fuchsia_component::client::connect_to_protocol_at_dir_root,
    futures::FutureExt,
    log::{debug, info},
    rand::{rngs::SmallRng, seq::SliceRandom, FromEntropy, Rng, SeedableRng},
    std::str::FromStr,
    std::time::Duration,
};

/// Stress tests assume that this collection exists and actors can be created in it.
static ACTOR_COLLECTION_NAME: &'static str = "actors";

/// Describes a running actor that has not yet started doing work.
pub struct ActorInstance {
    /// The name of this actor type.
    /// When actor component instances are created, they will include this name.
    pub name: String,

    /// FIDL proxy that controls the actor
    actor_proxy: ActorProxy,

    /// Represents the pending result of the previous operation, if any
    previous_result: Option<Task<Result<Option<String>, FidlError>>>,
}

impl ActorInstance {
    async fn create(name: String, url: String, realm_proxy: RealmProxy) -> Result<Self> {
        let decl = ChildDecl {
            name: Some(name.clone()),
            url: Some(url),
            startup: Some(StartupMode::Lazy),
            ..ChildDecl::EMPTY
        };
        let mut collection = CollectionRef { name: ACTOR_COLLECTION_NAME.to_string() };
        realm_proxy
            .create_child(&mut collection, decl, CreateChildArgs::EMPTY)
            .await
            .context("Could not send FIDL request to Realm.CreateChild")?
            .map_err(|e| {
                format_err!("Realm.CreateChild failed to create {} with error: {:?}", name, e)
            })?;

        let (exposed_dir, server_end) = create_proxy::<DirectoryMarker>()
            .context("Could not create endpoints for exposed dir")?;
        let mut child_ref =
            ChildRef { name: name.clone(), collection: Some(ACTOR_COLLECTION_NAME.to_string()) };
        realm_proxy
            .open_exposed_dir(&mut child_ref, server_end)
            .await
            .context("Could not send FIDL request to Realm.BindChild")?
            .map_err(|e| {
                format_err!("Realm.BindChild failed to create {} with error: {:?}", name, e)
            })?;

        let actor_proxy = connect_to_protocol_at_dir_root::<ActorMarker>(&exposed_dir)
            .context("Could not connect to Actor protocol in exposed dir")?;

        Ok(Self { name, actor_proxy, previous_result: None })
    }

    /// Runs the given `action` with the given seed. Returns the result of the previous operation.
    /// If there was no previous operation, `Ok(())` is returned.
    async fn run(&mut self, action: &str, seed: u64, action_timeout: Duration) -> Result<()> {
        let previous_result = if let Some(pending_result) = self.previous_result.take() {
            // There is a previous operation pending. Await on it and get its result first.
            pending_result.await
        } else {
            // No previous operation. Assume success.
            Ok(None)
        };

        // Start the new operation
        let actor_proxy = self.actor_proxy.clone();
        let action = action.to_string();
        self.previous_result = Some(Task::spawn(async move {
            actor_proxy
                .run(&action, seed)
                .map(|r| {
                    r.map(|o| {
                        o.map(|e| match *e {
                            Error::ErrorString(s) => s,
                            _ => "Unknown fuchsia.stresstest.Error type".to_string(),
                        })
                    })
                })
                .on_timeout(action_timeout, || Ok(Some(format!("Action `{}` timed out", action))))
                .await
        }));

        match previous_result {
            Ok(None) => Ok(()),
            Ok(Some(err_string)) => Err(format_err!("[{}]: {}", self.name, err_string)),
            Err(e) => Err(format_err!("[{}] FIDL error during `run` call: {}", self.name, e)),
        }
    }

    async fn get_actions(&self) -> Result<Vec<String>> {
        let action_iterator = self.actor_proxy.get_actions().await?;
        let action_iterator = action_iterator.into_proxy()?;
        let mut actions = vec![];

        loop {
            let iteration = action_iterator.get_next().await?;
            if iteration.is_empty() {
                break;
            }
            for action in iteration {
                let action_name =
                    action.name.ok_or(format_err!("Name was not specified in action"))?;
                actions.push(action_name);
            }
        }

        Ok(actions)
    }
}

fn get_string(key: &'static str, entries: &Vec<DictionaryEntry>) -> Option<String> {
    for entry in entries {
        if entry.key == key {
            if let Some(value) = &entry.value {
                if let DictionaryValue::Str(value) = &**value {
                    return Some(value.clone());
                } else {
                    return None;
                }
            } else {
                return None;
            }
        }
    }
    return None;
}

fn get_and_parse<F: FromStr>(key: &'static str, entries: &Vec<DictionaryEntry>) -> Option<F> {
    let string = get_string(key, entries)?;
    string.parse::<F>().ok()
}

fn connect_to_realm_proxy(ns: Vec<ComponentNamespaceEntry>) -> Result<RealmProxy> {
    for entry in ns {
        if let Some(path) = entry.path {
            if path == "/svc" {
                let dir =
                    entry.directory.ok_or(format_err!("No directory for 'svc' namespace entry"))?;
                let dir = dir.into_proxy()?;
                return connect_to_protocol_at_dir_root::<RealmMarker>(&dir);
            }
        }
    }
    return Err(format_err!("Could not find Realm protocol in component's incoming namespace."));
}

/// Defines a stress test including all actor configurations and exit criteria.
#[derive(Clone)]
pub struct StressTest {
    actor_url: String,
    num_instances: u64,
    realm_proxy: RealmProxy,
    rng: SmallRng,
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
    num_retries: u64,
    action_timeout: Duration,
    test_duration: Duration,
}

impl StressTest {
    pub fn new(dictionary: Vec<DictionaryEntry>, ns: Vec<ComponentNamespaceEntry>) -> Result<Self> {
        // Required
        let actor_url = get_string("actor_url", &dictionary)
            .ok_or(format_err!("Could not get `actor_url` string from program dictionary"))?;
        let num_instances = get_and_parse::<u64>("num_instances", &dictionary)
            .ok_or(format_err!("Could not get `num_instances` u64 from program dictionary"))?;
        let realm_proxy = connect_to_realm_proxy(ns)?;

        // Optional
        let rng = if let Some(seed) = get_and_parse::<u128>("seed", &dictionary) {
            SmallRng::from_seed(seed.to_le_bytes())
        } else {
            SmallRng::from_entropy()
        };

        let num_retries = get_and_parse::<u64>("num_retries", &dictionary).unwrap_or(0);

        // Default for test_duration is 22 hours
        let test_duration =
            get_and_parse::<u64>("test_duration", &dictionary).unwrap_or(22 * 60 * 60);
        let test_duration = Duration::from_secs(test_duration);

        // Default for action_timeout is 10 minutes
        let action_timeout = get_and_parse::<u64>("action_timeout", &dictionary).unwrap_or(10 * 60);
        let action_timeout = Duration::from_secs(action_timeout);

        Ok(Self {
            actor_url,
            num_instances,
            realm_proxy,
            rng,
            num_retries,
            action_timeout,
            test_duration,
        })
    }

    /// Start the stress test for the `test_duration` specified.
    /// If any errors occur in actor creation/running, this method will return an error.
    pub async fn start(mut self) -> Result<()> {
        let test_duration = self.test_duration.clone();
        async move {
            let mut instances = vec![];

            // Create all actors
            for i in 1..=self.num_instances {
                let name = format!("instance_{}", i);
                let instance =
                    ActorInstance::create(name, self.actor_url.clone(), self.realm_proxy.clone())
                        .await?;
                instances.push(instance);
            }

            // Get the actions offered by the actor
            let instance = instances.get(0).unwrap();
            let actions = instance.get_actions().await?;
            assert!(actions.len() > 0);

            info!("Running stress test actions: {:?}", actions);

            loop {
                // Pick a action, actor and seed to run next
                let action = actions.choose(&mut self.rng).unwrap();
                let actor = instances.choose_mut(&mut self.rng).unwrap();
                let seed = self.rng.gen::<u64>();
                debug!("[action:{}][actor:{}][seed:{}]", action, actor.name, seed);

                // This will block if the actor is currently performing an operation
                let previous_result = actor.run(action, seed, self.action_timeout.clone()).await;

                // The last operation this actor performed has failed.
                // Return the error.
                if previous_result.is_err() {
                    return previous_result;
                }
            }
        }
        .on_timeout(test_duration, || Ok(()))
        .await
    }
}
