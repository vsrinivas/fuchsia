// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        directory_broker,
        framework::FrameworkCapability,
        model::{addable_directory::AddableDirectory, *},
    },
    cm_rust::FrameworkCapabilityDecl,
    fidl::endpoints::{ClientEnd, ServerEnd},
    fidl_fuchsia_io::DirectoryMarker,
    fuchsia_async as fasync,
    fuchsia_vfs_pseudo_fs::directory,
    fuchsia_vfs_pseudo_fs::directory::entry::DirectoryEntry,
    fuchsia_zircon as zx,
    futures::{executor::block_on, future::BoxFuture, lock::Mutex, prelude::*},
    std::{cmp::Eq, collections::HashMap, fmt, ops::Deref, pin::Pin, sync::Arc},
};

struct ComponentInstance {
    pub abs_moniker: AbsoluteMoniker,
    pub children: Mutex<Vec<Arc<ComponentInstance>>>,
}

impl Clone for ComponentInstance {
    // This is used by TestHook. ComponentInstance is immutable so when a change
    // needs to be made, TestHook clones ComponentInstance and makes the change
    // in the new copy.
    fn clone(&self) -> Self {
        let children = block_on(self.children.lock());
        return ComponentInstance {
            abs_moniker: self.abs_moniker.clone(),
            children: Mutex::new(children.clone()),
        };
    }
}

impl PartialEq for ComponentInstance {
    fn eq(&self, other: &Self) -> bool {
        self.abs_moniker == other.abs_moniker
    }
}

impl Eq for ComponentInstance {}

impl ComponentInstance {
    pub async fn print(&self) -> String {
        let mut s: String = self.abs_moniker.path().last().map_or("", |m| m.as_str()).to_string();
        let mut children = self.children.lock().await;
        if children.is_empty() {
            return s;
        }

        // The position of a child in the children vector is a function of timing.
        // In order to produce stable topology strings across runs, we sort the set
        // of children here by moniker.
        children.sort_by(|a, b| a.abs_moniker.cmp(&b.abs_moniker));

        s.push('(');
        let mut count = 0;
        for child in children.iter() {
            // If we've seen a previous child, then add a comma to separate children.
            if count > 0 {
                s.push(',');
            }
            s.push_str(&child.boxed_print().await);
            count += 1;
        }
        s.push(')');
        s
    }

    fn boxed_print<'a>(&'a self) -> Pin<Box<dyn Future<Output = String> + 'a>> {
        Box::pin(self.print())
    }
}

pub struct TestHook {
    instances: Mutex<HashMap<AbsoluteMoniker, Arc<ComponentInstance>>>,
}

impl fmt::Display for TestHook {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        writeln!(f, "{}", self.print())?;
        Ok(())
    }
}

/// TestHook is a Hook that generates a strings representing the component
/// topology.
impl TestHook {
    pub fn new() -> TestHook {
        let abs_moniker = AbsoluteMoniker::root();
        let instance =
            ComponentInstance { abs_moniker: abs_moniker.clone(), children: Mutex::new(vec![]) };
        let mut instances = HashMap::new();
        instances.insert(abs_moniker, Arc::new(instance));
        TestHook { instances: Mutex::new(instances) }
    }

    // Recursively traverse the Instance tree to generate a string representing the component
    // topology.
    pub fn print(&self) -> String {
        let instances = block_on(self.instances.lock());
        let abs_moniker = AbsoluteMoniker::root();
        let root_instance =
            instances.get(&abs_moniker).map(|x| x.clone()).expect("Unable to find root instance.");
        block_on(root_instance.print())
    }

    pub async fn on_bind_instance_async<'a>(
        &'a self,
        realm: Arc<Realm>,
        realm_state: &'a RealmState,
        _routing_facade: RoutingFacade,
    ) -> Result<(), ModelError> {
        self.create_instance_if_necessary(realm.abs_moniker.clone()).await?;
        for child_realm in realm_state.get_child_realms().values() {
            self.create_instance_if_necessary(child_realm.abs_moniker.clone()).await?;
        }
        Ok(())
    }

    pub async fn create_instance_if_necessary(
        &self,
        abs_moniker: AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        let mut instances = self.instances.lock().await;
        if let Some(parent_moniker) = abs_moniker.parent() {
            // If the parent isn't available yet then opt_parent_instance will have a value
            // of None.
            let opt_parent_instance = instances.get(&parent_moniker).map(|x| x.clone());
            let new_instance = match instances.get(&abs_moniker) {
                Some(old_instance) => Arc::new((old_instance.deref()).clone()),
                None => Arc::new(ComponentInstance {
                    abs_moniker: abs_moniker.clone(),
                    children: Mutex::new(vec![]),
                }),
            };
            instances.insert(abs_moniker.clone(), new_instance.clone());
            // If the parent is available then add this instance as a child to it.
            if let Some(parent_instance) = opt_parent_instance {
                let mut children = parent_instance.children.lock().await;
                let opt_index =
                    children.iter().position(|c| c.abs_moniker == new_instance.abs_moniker);
                if let Some(index) = opt_index {
                    children.remove(index);
                }
                children.push(new_instance.clone());
            }
        }
        Ok(())
    }

    pub async fn remove_instance(&self, abs_moniker: AbsoluteMoniker) -> Result<(), ModelError> {
        let mut instances = self.instances.lock().await;
        if let Some(parent_moniker) = abs_moniker.parent() {
            instances.remove(&abs_moniker);
            let parent_instance = instances
                .get(&parent_moniker)
                .expect(&format!("parent instance {} not found", parent_moniker));
            let mut children = parent_instance.children.lock().await;
            let opt_index = children.iter().position(|c| c.abs_moniker == abs_moniker);
            if let Some(index) = opt_index {
                children.remove(index);
            }
        }
        Ok(())
    }
}

impl Hook for TestHook {
    fn on_bind_instance<'a>(
        &'a self,
        realm: Arc<Realm>,
        realm_state: &'a RealmState,
        routing_facade: RoutingFacade,
    ) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(self.on_bind_instance_async(realm, &realm_state, routing_facade))
    }

    fn on_add_dynamic_child(&self, realm: Arc<Realm>) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(self.create_instance_if_necessary(realm.abs_moniker.clone()))
    }

    fn on_remove_dynamic_child(&self, realm: Arc<Realm>) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(self.remove_instance(realm.abs_moniker.clone()))
    }

    fn on_route_framework_capability<'a>(
        &'a self,
        _realm: Arc<Realm>,
        _capability_decl: &'a FrameworkCapabilityDecl,
        capability: Option<Box<dyn FrameworkCapability>>,
    ) -> BoxFuture<Result<Option<Box<dyn FrameworkCapability>>, ModelError>> {
        Box::pin(async move { Ok(capability) })
    }
}

pub struct HubInjectionTestHook {}

impl HubInjectionTestHook {
    pub fn new() -> Self {
        HubInjectionTestHook {}
    }

    pub async fn on_route_framework_capability_async<'a>(
        &'a self,
        realm: Arc<Realm>,
        capability_decl: &'a FrameworkCapabilityDecl,
        mut capability: Option<Box<dyn FrameworkCapability>>,
    ) -> Result<Option<Box<dyn FrameworkCapability>>, ModelError> {
        // This Hook is about injecting itself between the Hub and the Model.
        // If the Hub hasn't been installed, then there's nothing to do here.
        let mut relative_path = match (&capability, capability_decl) {
            (Some(_), FrameworkCapabilityDecl::Directory(source_path)) => source_path.split(),
            _ => return Ok(capability),
        };

        if relative_path.is_empty() || relative_path.remove(0) != "hub" {
            return Ok(capability);
        }

        Ok(Some(Box::new(HubInjectionCapability::new(
            realm.abs_moniker.clone(),
            relative_path,
            capability.take().expect("Unable to take original capability."),
        ))))
    }
}

impl Hook for HubInjectionTestHook {
    fn on_bind_instance<'a>(
        &'a self,
        _realm: Arc<Realm>,
        _realm_state: &'a RealmState,
        _routing_facade: RoutingFacade,
    ) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(async { Ok(()) })
    }

    fn on_add_dynamic_child(&self, _realm: Arc<Realm>) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(async { Ok(()) })
    }

    fn on_remove_dynamic_child(&self, _realm: Arc<Realm>) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(async { Ok(()) })
    }

    fn on_route_framework_capability<'a>(
        &'a self,
        realm: Arc<Realm>,
        capability_decl: &'a FrameworkCapabilityDecl,
        capability: Option<Box<dyn FrameworkCapability>>,
    ) -> BoxFuture<Result<Option<Box<dyn FrameworkCapability>>, ModelError>> {
        Box::pin(self.on_route_framework_capability_async(realm, capability_decl, capability))
    }
}

struct HubInjectionCapability {
    abs_moniker: AbsoluteMoniker,
    relative_path: Vec<String>,
    intercepted_capability: Box<dyn FrameworkCapability>,
}

impl HubInjectionCapability {
    pub fn new(
        abs_moniker: AbsoluteMoniker,
        relative_path: Vec<String>,
        intercepted_capability: Box<dyn FrameworkCapability>,
    ) -> Self {
        HubInjectionCapability { abs_moniker, relative_path, intercepted_capability }
    }

    pub async fn open_async(
        &self,
        flags: u32,
        open_mode: u32,
        relative_path: String,
        server_end: zx::Channel,
    ) -> Result<(), ModelError> {
        let mut dir_path = self.relative_path.clone();
        dir_path.append(
            &mut relative_path
                .split("/")
                .map(|s| s.to_string())
                .filter(|s| !s.is_empty())
                .collect::<Vec<String>>(),
        );

        let (client_chan, server_chan) = zx::Channel::create().unwrap();
        self.intercepted_capability.open(flags, open_mode, String::new(), server_chan).await?;

        let hub_proxy = ClientEnd::<DirectoryMarker>::new(client_chan)
            .into_proxy()
            .expect("failed to create directory proxy");

        let mut dir = directory::simple::empty();
        dir.add_node(
            "old_hub",
            directory_broker::DirectoryBroker::from_directory_proxy(hub_proxy),
            &self.abs_moniker,
        )?;
        dir.open(
            flags,
            open_mode,
            &mut dir_path.iter().map(|s| s.as_str()),
            ServerEnd::new(server_end),
        );

        fasync::spawn(async move {
            let _ = dir.await;
        });

        Ok(())
    }
}

impl FrameworkCapability for HubInjectionCapability {
    fn open(
        &self,
        flags: u32,
        open_mode: u32,
        relative_path: String,
        server_chan: zx::Channel,
    ) -> BoxFuture<Result<(), ModelError>> {
        Box::pin(self.open_async(flags, open_mode, relative_path, server_chan))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_hook_test() {
        let a = AbsoluteMoniker::new(vec![ChildMoniker::new("a".to_string(), None)]);
        let ab = AbsoluteMoniker::new(vec![
            ChildMoniker::new("a".to_string(), None),
            ChildMoniker::new("b".to_string(), None),
        ]);
        let ac = AbsoluteMoniker::new(vec![
            ChildMoniker::new("a".to_string(), None),
            ChildMoniker::new("c".to_string(), None),
        ]);
        let abd = AbsoluteMoniker::new(vec![
            ChildMoniker::new("a".to_string(), None),
            ChildMoniker::new("b".to_string(), None),
            ChildMoniker::new("d".to_string(), None),
        ]);
        let abe = AbsoluteMoniker::new(vec![
            ChildMoniker::new("a".to_string(), None),
            ChildMoniker::new("b".to_string(), None),
            ChildMoniker::new("e".to_string(), None),
        ]);
        let acf = AbsoluteMoniker::new(vec![
            ChildMoniker::new("a".to_string(), None),
            ChildMoniker::new("c".to_string(), None),
            ChildMoniker::new("f".to_string(), None),
        ]);

        // Try adding parent followed by children then verify the topology string
        // is correct.
        {
            let test_hook = TestHook::new();
            assert!(test_hook.create_instance_if_necessary(a.clone()).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(ab.clone()).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(ac.clone()).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(abd.clone()).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(abe.clone()).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(acf.clone()).await.is_ok());
            assert_eq!("(a(b(d,e),c(f)))", test_hook.print());
        }

        // Changing the order of monikers should not affect the output string.
        {
            let test_hook = TestHook::new();
            assert!(test_hook.create_instance_if_necessary(a.clone()).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(ac.clone()).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(ab.clone()).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(abd.clone()).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(abe.clone()).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(acf.clone()).await.is_ok());
            assert_eq!("(a(b(d,e),c(f)))", test_hook.print());
        }

        // Submitting children before parents should still succeed.
        {
            let test_hook = TestHook::new();
            assert!(test_hook.create_instance_if_necessary(acf.clone()).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(abe.clone()).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(abd.clone()).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(ab.clone()).await.is_ok());
            // Model will call create_instance_if_necessary for ab's children again
            // after the call to bind_instance for ab.
            assert!(test_hook.create_instance_if_necessary(abe.clone()).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(abd.clone()).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(ac.clone()).await.is_ok());
            // Model will call create_instance_if_necessary for ac's children again
            // after the call to bind_instance for ac.
            assert!(test_hook.create_instance_if_necessary(acf.clone()).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(a.clone()).await.is_ok());
            // Model will call create_instance_if_necessary for a's children again
            // after the call to bind_instance for a.
            assert!(test_hook.create_instance_if_necessary(ab.clone()).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(ac.clone()).await.is_ok());
            assert_eq!("(a(b(d,e),c(f)))", test_hook.print());
        }
    }
}
