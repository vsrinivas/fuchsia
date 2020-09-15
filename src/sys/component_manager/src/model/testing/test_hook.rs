// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::*,
        channel,
        model::addable_directory::AddableDirectoryWithResult,
        model::{
            error::ModelError,
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
            moniker::AbsoluteMoniker,
        },
        path::PathBufExt,
    },
    async_trait::async_trait,
    cm_rust::CapabilityNameOrPath,
    directory_broker,
    fidl::endpoints::{ClientEnd, ServerEnd},
    fidl_fuchsia_io::DirectoryMarker,
    fuchsia_zircon as zx,
    futures::{executor::block_on, lock::Mutex, prelude::*},
    std::{
        cmp::Eq,
        collections::HashMap,
        fmt,
        ops::Deref,
        path::PathBuf,
        pin::Pin,
        sync::{Arc, Weak},
    },
    vfs::{
        directory::entry::DirectoryEntry, directory::immutable::simple as pfs,
        execution_scope::ExecutionScope, path::Path as pfsPath,
    },
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
        let mut s: String =
            self.abs_moniker.leaf().map_or(String::new(), |m| format!("{}", m.to_partial()));
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

#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord)]
pub enum Lifecycle {
    Bind(AbsoluteMoniker),
    Stop(AbsoluteMoniker),
    PreDestroy(AbsoluteMoniker),
    Destroy(AbsoluteMoniker),
}

impl fmt::Display for Lifecycle {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Lifecycle::Bind(m) => write!(f, "bind({})", m),
            Lifecycle::Stop(m) => write!(f, "stop({})", m),
            Lifecycle::PreDestroy(m) => write!(f, "predestroy({})", m),
            Lifecycle::Destroy(m) => write!(f, "destroy({})", m),
        }
    }
}

/// TestHook is a Hook that generates a strings representing the component
/// topology.
pub struct TestHook {
    instances: Mutex<HashMap<AbsoluteMoniker, Arc<ComponentInstance>>>,
    lifecycle_events: Mutex<Vec<Lifecycle>>,
}

impl fmt::Display for TestHook {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        writeln!(f, "{}", self.print())?;
        Ok(())
    }
}

impl TestHook {
    pub fn new() -> TestHook {
        Self { instances: Mutex::new(HashMap::new()), lifecycle_events: Mutex::new(vec![]) }
    }

    /// Returns the set of hooks into the component manager that TestHook is interested in.
    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![HooksRegistration::new(
            "TestHook",
            vec![
                EventType::Discovered,
                EventType::Destroyed,
                EventType::MarkedForDestruction,
                EventType::Started,
                EventType::Stopped,
            ],
            Arc::downgrade(self) as Weak<dyn Hook>,
        )]
    }

    /// Recursively traverse the Instance tree to generate a string representing the component
    /// topology.
    pub fn print(&self) -> String {
        let instances = block_on(self.instances.lock());
        let abs_moniker = AbsoluteMoniker::root();
        let root_instance =
            instances.get(&abs_moniker).map(|x| x.clone()).expect("Unable to find root instance.");
        block_on(root_instance.print())
    }

    /// Return the sequence of lifecycle events.
    pub fn lifecycle(&self) -> Vec<Lifecycle> {
        block_on(self.lifecycle_events.lock()).clone()
    }

    pub async fn on_started_async<'a>(
        &'a self,
        target_moniker: &AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        self.create_instance_if_necessary(target_moniker).await?;
        let mut events = self.lifecycle_events.lock().await;
        events.push(Lifecycle::Bind(target_moniker.clone()));
        Ok(())
    }

    pub async fn on_stopped_async<'a>(
        &'a self,
        target_moniker: &AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        let mut events = self.lifecycle_events.lock().await;
        events.push(Lifecycle::Stop(target_moniker.clone()));
        Ok(())
    }

    pub async fn on_marked_for_destruction_async<'a>(
        &'a self,
        target_moniker: &AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        // TODO: Can this be changed not restrict to dynamic instances? Static instances can be
        // deleted too.
        if let Some(child_moniker) = target_moniker.leaf() {
            if child_moniker.collection().is_some() {
                self.remove_instance(target_moniker).await?;
            }
        }

        let mut events = self.lifecycle_events.lock().await;
        events.push(Lifecycle::PreDestroy(target_moniker.clone()));
        Ok(())
    }

    pub async fn on_destroyed_async<'a>(
        &'a self,
        target_moniker: &AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        let mut events = self.lifecycle_events.lock().await;
        events.push(Lifecycle::Destroy(target_moniker.clone()));
        Ok(())
    }

    pub async fn create_instance_if_necessary(
        &self,
        abs_moniker: &AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        let mut instances = self.instances.lock().await;
        let new_instance = match instances.get(abs_moniker) {
            Some(old_instance) => Arc::new((old_instance.deref()).clone()),
            None => Arc::new(ComponentInstance {
                abs_moniker: abs_moniker.clone(),
                children: Mutex::new(vec![]),
            }),
        };
        instances.insert(abs_moniker.clone(), new_instance.clone());
        if let Some(parent_moniker) = abs_moniker.parent() {
            // If the parent isn't available yet then opt_parent_instance will have a value
            // of None.
            let opt_parent_instance = instances.get(&parent_moniker).map(|x| x.clone());
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

    pub async fn remove_instance(&self, abs_moniker: &AbsoluteMoniker) -> Result<(), ModelError> {
        let mut instances = self.instances.lock().await;
        if let Some(parent_moniker) = abs_moniker.parent() {
            instances.remove(&abs_moniker);
            let parent_instance = instances
                .get(&parent_moniker)
                .expect(&format!("parent instance {} not found", parent_moniker));
            let mut children = parent_instance.children.lock().await;
            let opt_index = children.iter().position(|c| c.abs_moniker == *abs_moniker);
            if let Some(index) = opt_index {
                children.remove(index);
            }
        }
        Ok(())
    }
}

#[async_trait]
impl Hook for TestHook {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        match &event.result {
            Ok(EventPayload::Destroyed) => {
                self.on_destroyed_async(&event.target_moniker).await?;
            }
            Ok(EventPayload::Discovered { .. }) => {
                self.create_instance_if_necessary(&event.target_moniker).await?;
            }
            Ok(EventPayload::MarkedForDestruction) => {
                self.on_marked_for_destruction_async(&event.target_moniker).await?;
            }
            Ok(EventPayload::Started { .. }) => {
                self.on_started_async(&event.target_moniker).await?;
            }
            Ok(EventPayload::Stopped { .. }) => {
                self.on_stopped_async(&event.target_moniker).await?;
            }
            _ => (),
        };
        Ok(())
    }
}

pub struct HubInjectionTestHook;

impl HubInjectionTestHook {
    pub fn new() -> Self {
        Self
    }

    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![HooksRegistration::new(
            "TestHook",
            vec![EventType::CapabilityRouted],
            Arc::downgrade(self) as Weak<dyn Hook>,
        )]
    }

    pub async fn on_scoped_framework_capability_routed_async<'a>(
        &'a self,
        scope_moniker: AbsoluteMoniker,
        capability: &'a InternalCapability,
        mut capability_provider: Option<Box<dyn CapabilityProvider>>,
    ) -> Result<Option<Box<dyn CapabilityProvider>>, ModelError> {
        // This Hook is about injecting itself between the Hub and the Model.
        // If the Hub hasn't been installed, then there's nothing to do here.
        let relative_path = match (&capability_provider, capability) {
            (Some(_), InternalCapability::Directory(name_or_path)) => {
                match name_or_path {
                    CapabilityNameOrPath::Path(source_path) => {
                        let mut relative_path = source_path.split();
                        // The source path must begin with "hub"
                        if relative_path.is_empty() || relative_path.remove(0) != "hub" {
                            return Ok(capability_provider);
                        }
                        relative_path
                    }
                    CapabilityNameOrPath::Name(source_name) => {
                        if source_name.str() != "hub" {
                            return Ok(capability_provider);
                        }
                        vec![]
                    }
                }
            }
            _ => return Ok(capability_provider),
        };

        Ok(Some(Box::new(HubInjectionCapabilityProvider::new(
            scope_moniker,
            relative_path,
            capability_provider.take().expect("Unable to take original capability."),
        ))))
    }
}

#[async_trait]
impl Hook for HubInjectionTestHook {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        if let Ok(EventPayload::CapabilityRouted {
            source: CapabilitySource::Framework { capability, scope_moniker },
            capability_provider,
        }) = &event.result
        {
            let mut capability_provider = capability_provider.lock().await;
            *capability_provider = self
                .on_scoped_framework_capability_routed_async(
                    scope_moniker.clone(),
                    capability,
                    capability_provider.take(),
                )
                .await?;
        }
        Ok(())
    }
}

struct HubInjectionCapabilityProvider {
    abs_moniker: AbsoluteMoniker,
    relative_path: Vec<String>,
    intercepted_capability: Box<dyn CapabilityProvider>,
}

impl HubInjectionCapabilityProvider {
    pub fn new(
        abs_moniker: AbsoluteMoniker,
        relative_path: Vec<String>,
        intercepted_capability: Box<dyn CapabilityProvider>,
    ) -> Self {
        HubInjectionCapabilityProvider { abs_moniker, relative_path, intercepted_capability }
    }
}

#[async_trait]
impl CapabilityProvider for HubInjectionCapabilityProvider {
    async fn open(
        self: Box<Self>,
        flags: u32,
        open_mode: u32,
        in_relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        let (client_chan, mut server_chan) = zx::Channel::create().unwrap();
        self.intercepted_capability
            .open(flags, open_mode, PathBuf::new(), &mut server_chan)
            .await?;

        let hub_proxy = ClientEnd::<DirectoryMarker>::new(client_chan)
            .into_proxy()
            .expect("failed to create directory proxy");

        let dir = pfs::simple();
        dir.add_node(
            "old_hub",
            directory_broker::DirectoryBroker::from_directory_proxy(hub_proxy),
            &self.abs_moniker,
        )?;
        let relative_path: PathBuf = self.relative_path.iter().collect();
        let mut relative_path =
            relative_path.attach(in_relative_path).to_str().expect("path is not utf8").to_string();
        relative_path.push('/');
        let path =
            pfsPath::validate_and_split(relative_path).expect("failed to split and validate path");
        let server_end = channel::take_channel(server_end);
        dir.open(ExecutionScope::new(), flags, open_mode, path, ServerEnd::new(server_end));

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_hook_test() {
        let root = AbsoluteMoniker::root();
        let a: AbsoluteMoniker = vec!["a:0"].into();
        let ab: AbsoluteMoniker = vec!["a:0", "b:0"].into();
        let ac: AbsoluteMoniker = vec!["a:0", "c:0"].into();
        let abd: AbsoluteMoniker = vec!["a:0", "b:0", "d:0"].into();
        let abe: AbsoluteMoniker = vec!["a:0", "b:0", "e:0"].into();
        let acf: AbsoluteMoniker = vec!["a:0", "c:0", "f:0"].into();

        // Try adding parent followed by children then verify the topology string
        // is correct.
        {
            let test_hook = TestHook::new();
            assert!(test_hook.create_instance_if_necessary(&root).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(&a).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(&ab).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(&ac).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(&abd).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(&abe).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(&acf).await.is_ok());
            assert_eq!("(a(b(d,e),c(f)))", test_hook.print());
        }

        // Changing the order of monikers should not affect the output string.
        {
            let test_hook = TestHook::new();
            assert!(test_hook.create_instance_if_necessary(&root).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(&a).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(&ac).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(&ab).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(&abd).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(&abe).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(&acf).await.is_ok());
            assert_eq!("(a(b(d,e),c(f)))", test_hook.print());
        }

        // Submitting children before parents should still succeed.
        {
            let test_hook = TestHook::new();
            assert!(test_hook.create_instance_if_necessary(&root).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(&acf).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(&abe).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(&abd).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(&ab).await.is_ok());
            // Model will call create_instance_if_necessary for ab's children again
            // after the call to bind_instance for ab.
            assert!(test_hook.create_instance_if_necessary(&abe).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(&abd).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(&ac).await.is_ok());
            // Model will call create_instance_if_necessary for ac's children again
            // after the call to bind_instance for ac.
            assert!(test_hook.create_instance_if_necessary(&acf).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(&a).await.is_ok());
            // Model will call create_instance_if_necessary for a's children again
            // after the call to bind_instance for a.
            assert!(test_hook.create_instance_if_necessary(&ab).await.is_ok());
            assert!(test_hook.create_instance_if_necessary(&ac).await.is_ok());
            assert_eq!("(a(b(d,e),c(f)))", test_hook.print());
        }
    }
}
