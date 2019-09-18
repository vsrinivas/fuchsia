// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_manager_lib::{
        elf_runner::{ElfRunner, ProcessLauncherConnector},
        framework::RealmServiceHost,
        model::{
            self,
            hooks::{DestroyInstanceHook, Hook},
            testing::test_hook::{Lifecycle, TestHook},
            AbsoluteMoniker, Model, ModelError, ModelParams, Realm,
        },
        startup,
    },
    failure::{Error, ResultExt},
    fuchsia_async as fasync, fuchsia_syslog as syslog,
    futures::{channel::mpsc, future::BoxFuture, lock::Mutex, sink::SinkExt, StreamExt},
    std::sync::Arc,
};

// TODO: This is a white box test so that we can use hooks. Really this should be a black box test,
// but we need to implement stopping and/or external hooks for that to be possible.
#[fasync::run_singlethreaded(test)]
async fn destruction() -> Result<(), Error> {
    syslog::init_with_tags(&[]).context("could not initialize logging")?;

    // Set up model and hooks.
    let root_component_url =
        "fuchsia-pkg://fuchsia.com/destruction_integration_test#meta/collection_realm.cm"
            .to_string();
    let args = startup::Arguments {
        use_builtin_process_launcher: false,
        use_builtin_vmex: false,
        root_component_url,
    };
    let builtin_services = Arc::new(startup::BuiltinRootServices::new(&args)?);
    let launcher_connector = ProcessLauncherConnector::new(&args, builtin_services);
    let resolver_registry = startup::available_resolvers()?;
    let runner = ElfRunner::new(launcher_connector);
    let params = ModelParams {
        root_component_url: args.root_component_url.clone(),
        root_resolver_registry: resolver_registry,
        root_default_runner: Arc::new(runner),
        config: model::ModelConfig::default(),
        builtin_services: Arc::new(startup::BuiltinRootServices::new(&args).unwrap()),
    };
    let model = Arc::new(Model::new(params));

    let realm_service_host = RealmServiceHost::new((*model).clone());
    let test_hook = TestHook::new();
    let (destroy_hook, mut destroy_recv) = DestroyHook::new(vec!["coll:root:1"].into());
    model.root_realm.hooks.install(test_hook.hooks()).await;
    model.root_realm.hooks.install(DestroyHook::hooks(destroy_hook)).await;
    model.root_realm.hooks.install(realm_service_host.hooks()).await;

    model.look_up_and_bind_instance(model::AbsoluteMoniker::root()).await?;

    // Wait for `coll:root` to be destroyed.
    destroy_recv.next().await.expect("failed to destroy notification");

    // Assert that root component has no children.
    let children: Vec<_> = model
        .root_realm
        .lock_state()
        .await
        .get()
        .all_child_realms()
        .keys()
        .map(|m| m.clone())
        .collect();
    assert!(children.is_empty());

    // Assert the expected lifecycle events. The leaves can be stopped/destroyed in either order.
    let mut events: Vec<_> = test_hook
        .lifecycle()
        .into_iter()
        .filter_map(|e| match e {
            Lifecycle::Stop(_) | Lifecycle::Destroy(_) => Some(e),
            _ => None,
        })
        .collect();

    let mut next: Vec<_> = events.drain(0..2).collect();
    next.sort_unstable();
    let expected: Vec<_> = vec![
        Lifecycle::Stop(vec!["coll:root:1", "trigger_a:0"].into()),
        Lifecycle::Stop(vec!["coll:root:1", "trigger_b:0"].into()),
    ];
    assert_eq!(next, expected);
    let next: Vec<_> = events.drain(0..1).collect();
    assert_eq!(next, vec![Lifecycle::Stop(vec!["coll:root:1"].into())]);

    let mut next: Vec<_> = events.drain(0..2).collect();
    next.sort_unstable();
    let expected: Vec<_> = vec![
        Lifecycle::Destroy(vec!["coll:root:1", "trigger_a:0"].into()),
        Lifecycle::Destroy(vec!["coll:root:1", "trigger_b:0"].into()),
    ];
    assert_eq!(next, expected);
    assert_eq!(events, vec![Lifecycle::Destroy(vec!["coll:root:1"].into())]);

    Ok(())
}

struct DestroyHook {
    /// Realm for which to block `on_stop_instance`.
    moniker: AbsoluteMoniker,
    /// Receiver on which `on_destroy_instance` is signalled.
    destroy_send: Mutex<mpsc::Sender<()>>,
}

impl DestroyHook {
    /// Returns `DestroyHook` and channel on which to be signalled for `on_destroy_instance`.
    fn new(moniker: AbsoluteMoniker) -> (Arc<Self>, mpsc::Receiver<()>) {
        let (destroy_send, destroy_recv) = mpsc::channel(0);
        (Arc::new(Self { moniker, destroy_send: Mutex::new(destroy_send) }), destroy_recv)
    }

    async fn on_destroy_instance_async(&self, realm: Arc<Realm>) -> Result<(), ModelError> {
        if realm.abs_moniker == self.moniker {
            let mut send = self.destroy_send.lock().await;
            send.send(()).await.expect("failed to send destroy signal");
        }
        Ok(())
    }

    fn hooks(hook: Arc<DestroyHook>) -> Vec<Hook> {
        vec![Hook::DestroyInstance(hook.clone())]
    }
}

impl DestroyInstanceHook for DestroyHook {
    fn on(&self, realm: Arc<Realm>) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(self.on_destroy_instance_async(realm))
    }
}
