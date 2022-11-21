// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource},
        model::{
            component::{ComponentInstance, Runtime, StartReason, WeakComponentInstance},
            error::ModelError,
        },
    },
    anyhow::format_err,
    async_trait::async_trait,
    cm_moniker::IncarnationId,
    cm_rust::{CapabilityName, ComponentDecl},
    cm_util::io::clone_dir,
    config_encoder::ConfigFields,
    fidl_fuchsia_diagnostics_types as fdiagnostics, fidl_fuchsia_io as fio,
    fidl_fuchsia_sys2 as fsys, fuchsia_zircon as zx,
    futures::{channel::oneshot, lock::Mutex},
    moniker::{AbsoluteMoniker, ExtendedMoniker},
    std::{
        collections::HashMap,
        convert::TryFrom,
        fmt,
        sync::{Arc, Weak},
    },
};

/// Defines the `EventType` enum as well as its implementation.
/// |description| is the description of the event that will be a doc comment on that event type.
/// |name| is the name of the event on CamelCase format, capitalized.
/// |string_name| is the name of the event on snake_case format, not capitalized.
macro_rules! events {
    ([$($(#[$description:meta])* ($name:ident, $string_name:ident),)*]) => {
        pub trait HasEventType {
            fn event_type(&self) -> EventType;
        }

        #[derive(Clone, Debug, Eq, PartialEq, Hash)]
        pub enum EventType {
            $(
                $(#[$description])*
                $name,
            )*
        }

        /// Transfers any move-only state out of self into a new event that is otherwise
        /// a clone.
        #[async_trait]
        pub trait TransferEvent {
            async fn transfer(&self) -> Self;
        }

        impl fmt::Display for EventType {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                write!(f, "{}", match self {
                    $(
                        EventType::$name => stringify!($string_name),
                    )*
                }
                .to_string())
            }
        }

        impl TryFrom<String> for EventType {
            type Error = anyhow::Error;

            fn try_from(string: String) -> Result<EventType, Self::Error> {
                match string.as_str() {
                    $(
                        stringify!($string_name) => Ok(EventType::$name),
                    )*
                    other => Err(format_err!("invalid string for event type: {:?}", other))
                }
            }
        }

        impl EventType {
            /// Returns all available event types.
            pub fn values() -> Vec<EventType> {
                vec![
                    $(EventType::$name,)*
                ]
            }
        }

        impl HasEventType for EventPayload {
            fn event_type(&self) -> EventType {
                match self {
                    $(
                        EventPayload::$name { .. } => EventType::$name,
                    )*
                }
            }
        }
    };
}

macro_rules! external_events {
    ($($name:ident),*) => {

        impl From<fsys::EventType> for EventType {
            fn from(fidl_event_type: fsys::EventType) -> Self {
                match fidl_event_type {
                    $(
                        fsys::EventType::$name => EventType::$name,
                    )*
                }
            }
        }

        impl TryInto<fsys::EventType> for EventType {
            type Error = anyhow::Error;
            fn try_into(self) -> Result<fsys::EventType, anyhow::Error> {
                match self {
                    $(
                        EventType::$name => Ok(fsys::EventType::$name),
                    )*
                    EventType::CapabilityRouted =>
                        Err(format_err!("can't serve capability routed")),
                }
            }
        }
    }
}

// Keep the event types listed below in alphabetical order!
events!([
    /// After a CapabilityProvider has been selected through the CapabilityRouted event,
    /// the CapabilityRequested event is dispatched with the ServerEnd of the channel
    /// for the capability.
    (CapabilityRequested, capability_requested),
    /// A capability is being requested by a component and requires routing.
    /// The event propagation system is used to supply the capability being requested.
    (CapabilityRouted, capability_routed),
    /// A directory exposed to the framework by a component is available.
    (DirectoryReady, directory_ready),
    /// A component instance was discovered.
    (Discovered, discovered),
    /// Destruction of an instance has begun. The instance may/may not be stopped by this point.
    /// The instance still exists in the parent's realm but will soon be removed.
    (Destroyed, destroyed),
    /// An instance's declaration was resolved successfully for the first time.
    (Resolved, resolved),
    /// An instance is about to be started.
    (Started, started),
    /// An instance was stopped successfully.
    /// This event must occur before Destroyed.
    (Stopped, stopped),
    /// Similar to the Started event, except the payload will carry an eventpair
    /// that the subscriber could use to defer the launch of the component.
    (DebugStarted, debug_started),
    /// A component instance was unresolved.
    (Unresolved, unresolved),
]);

external_events!(
    CapabilityRequested,
    DirectoryReady,
    Discovered,
    Destroyed,
    Resolved,
    Started,
    Stopped,
    DebugStarted,
    Unresolved
);

impl Into<CapabilityName> for EventType {
    fn into(self) -> CapabilityName {
        self.to_string().into()
    }
}

/// The component manager calls out to objects that implement the `Hook` trait on registered
/// component manager events. Hooks block the flow of a task, and can mutate, decorate and replace
/// capabilities. This permits `Hook` to serve as a point of extensibility for the component
/// manager.
/// IMPORTANT: Hooks must not block on completion of an Action since Hooks are often called while
/// executing an Action. Waiting on an Action in a Hook could cause a deadlock.
/// IMPORTANT: Hooks should avoid causing event dispatch because we do not guarantee serialization
/// between Hooks. Therefore the order a receiver see events in may be unexpected.
#[async_trait]
pub trait Hook: Send + Sync {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError>;
}

/// An object registers a hook into a component manager event via a `HooksRegistration` object.
/// A single object may register for multiple events through a vector of `EventType`. `Hooks`
/// does not retain the callback. The hook is lazily removed when the callback object loses
/// strong references.
#[derive(Clone)]
pub struct HooksRegistration {
    events: Vec<EventType>,
    callback: Weak<dyn Hook>,
}

impl HooksRegistration {
    pub fn new(
        _name: &'static str,
        events: Vec<EventType>,
        callback: Weak<dyn Hook>,
    ) -> HooksRegistration {
        Self { events, callback }
    }
}

#[derive(Clone)]
pub enum EventPayload {
    // Keep the events listed below in alphabetical order!
    CapabilityRequested {
        source_moniker: AbsoluteMoniker,
        name: String,
        capability: Arc<Mutex<Option<zx::Channel>>>,
    },
    CapabilityRouted {
        source: CapabilitySource,
        // Events are passed to hooks as immutable borrows. In order to mutate,
        // a field within an Event, interior mutability is employed here with
        // a Mutex.
        capability_provider: Arc<Mutex<Option<Box<dyn CapabilityProvider>>>>,
    },
    DirectoryReady {
        name: String,
        node: fio::NodeProxy,
    },
    Discovered {
        instance_id: IncarnationId,
    },
    Destroyed,
    Resolved {
        component: WeakComponentInstance,
        resolved_url: String,
        decl: ComponentDecl,
        config: Option<ConfigFields>,
        package_dir: Option<fio::DirectoryProxy>,
    },
    Unresolved,
    Started {
        component: WeakComponentInstance,
        runtime: RuntimeInfo,
        component_decl: ComponentDecl,
        start_reason: StartReason,
    },
    Stopped {
        status: zx::Status,
    },
    DebugStarted {
        runtime_dir: Option<fio::DirectoryProxy>,
        break_on_start: Arc<zx::EventPair>,
    },
}

/// Information about a component's runtime provided to `Started`.
#[derive(Clone)]
pub struct RuntimeInfo {
    pub resolved_url: String,
    pub package_dir: Option<fio::DirectoryProxy>,
    pub outgoing_dir: Option<fio::DirectoryProxy>,
    pub runtime_dir: Option<fio::DirectoryProxy>,
    pub diagnostics_receiver:
        Arc<Mutex<Option<oneshot::Receiver<fdiagnostics::ComponentDiagnostics>>>>,
    pub start_time: zx::Time,
}

impl RuntimeInfo {
    pub fn from_runtime(runtime: &mut Runtime, resolved_url: String) -> Self {
        let diagnostics_receiver = Arc::new(Mutex::new(
            runtime
                .controller
                .as_mut()
                .and_then(|controller| controller.take_diagnostics_receiver()),
        ));

        Self {
            resolved_url,
            package_dir: runtime.namespace.as_ref().and_then(|n| clone_dir(n.package_dir.as_ref())),
            outgoing_dir: clone_dir(runtime.outgoing_dir.as_ref()),
            runtime_dir: clone_dir(runtime.runtime_dir.as_ref()),
            diagnostics_receiver,
            start_time: runtime.timestamp,
        }
    }
}

impl fmt::Debug for EventPayload {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        let mut formatter = fmt.debug_struct("EventPayload");
        formatter.field("type", &self.event_type());
        match self {
            EventPayload::DirectoryReady { name, .. } => {
                formatter.field("name", &name.as_str()).finish()
            }
            EventPayload::CapabilityRequested { name, .. } => {
                formatter.field("name", &name).finish()
            }
            EventPayload::CapabilityRouted { source: capability, .. } => {
                formatter.field("capability", &capability).finish()
            }
            EventPayload::Started { component_decl, .. } => {
                formatter.field("component_decl", &component_decl).finish()
            }
            EventPayload::Resolved { component: _, resolved_url, decl, config, .. } => {
                formatter.field("resolved_url", resolved_url);
                formatter.field("decl", decl);
                formatter.field("config", config).finish()
            }
            EventPayload::Stopped { status } => formatter.field("status", status).finish(),
            EventPayload::Discovered { instance_id } => {
                formatter.field("instance_id", instance_id).finish()
            }
            EventPayload::Unresolved
            | EventPayload::Destroyed
            | EventPayload::DebugStarted { .. } => formatter.finish(),
        }
    }
}

#[derive(Clone, Debug)]
pub struct Event {
    /// Moniker of component that this event applies to
    pub target_moniker: ExtendedMoniker,

    /// Component url of the component that this event applies to
    pub component_url: String,

    /// Payload of the event
    pub payload: EventPayload,

    /// Time when this event was created
    pub timestamp: zx::Time,
}

impl Event {
    pub fn new(component: &Arc<ComponentInstance>, payload: EventPayload) -> Self {
        let timestamp = zx::Time::get_monotonic();
        Self::new_internal(
            component.abs_moniker.clone().into(),
            component.component_url.clone(),
            timestamp,
            payload,
        )
    }

    pub fn new_builtin(payload: EventPayload) -> Self {
        let timestamp = zx::Time::get_monotonic();
        Self::new_internal(
            ExtendedMoniker::ComponentManager,
            "bin/component_manager".to_string(),
            timestamp,
            payload,
        )
    }

    pub fn new_with_timestamp(
        component: &Arc<ComponentInstance>,
        payload: EventPayload,
        timestamp: zx::Time,
    ) -> Self {
        Self::new_internal(
            component.abs_moniker.clone().into(),
            component.component_url.clone(),
            timestamp,
            payload,
        )
    }

    #[cfg(test)]
    pub fn new_for_test(
        target_moniker: AbsoluteMoniker,
        component_url: impl Into<String>,
        payload: EventPayload,
    ) -> Self {
        let timestamp = zx::Time::get_monotonic();
        Self::new_internal(
            ExtendedMoniker::ComponentInstance(target_moniker),
            component_url.into(),
            timestamp,
            payload,
        )
    }

    fn new_internal(
        target_moniker: ExtendedMoniker,
        component_url: String,
        timestamp: zx::Time,
        payload: EventPayload,
    ) -> Self {
        Self { target_moniker, component_url, timestamp, payload }
    }
}

#[async_trait]
impl TransferEvent for EventPayload {
    async fn transfer(&self) -> Self {
        match self {
            EventPayload::CapabilityRequested { source_moniker, name, capability } => {
                let capability = capability.lock().await.take();
                EventPayload::CapabilityRequested {
                    source_moniker: source_moniker.clone(),
                    name: name.to_string(),
                    capability: Arc::new(Mutex::new(capability)),
                }
            }
            result => result.clone(),
        }
    }
}

impl HasEventType for Event {
    fn event_type(&self) -> EventType {
        self.payload.event_type()
    }
}

#[async_trait]
impl TransferEvent for Event {
    async fn transfer(&self) -> Self {
        Self {
            target_moniker: self.target_moniker.clone(),
            component_url: self.component_url.clone(),
            payload: self.payload.transfer().await,
            timestamp: self.timestamp,
        }
    }
}

impl fmt::Display for Event {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let payload = match &self.payload {
            EventPayload::DirectoryReady { name, .. } => format!("serving {}", name),
            EventPayload::CapabilityRequested { source_moniker, name, .. } => {
                format!("requested '{}' from '{}'", name.to_string(), source_moniker)
            }
            EventPayload::CapabilityRouted { source, .. } => {
                format!("routed {}", source.to_string())
            }
            EventPayload::Started { start_reason, .. } => {
                format!("because {}", start_reason.to_string())
            }
            EventPayload::Stopped { status } => {
                format!("with status: {}", status.to_string())
            }
            EventPayload::Discovered { .. }
            | EventPayload::Destroyed { .. }
            | EventPayload::Resolved { .. }
            | EventPayload::DebugStarted { .. }
            | EventPayload::Unresolved => "".to_string(),
        };
        write!(f, "[{}] '{}' {}", self.event_type().to_string(), self.target_moniker, payload)
    }
}

/// This is a collection of hooks to component manager events.
pub struct Hooks {
    hooks_map: Mutex<HashMap<EventType, Vec<Weak<dyn Hook>>>>,
}

impl Hooks {
    pub fn new() -> Self {
        Self { hooks_map: Mutex::new(HashMap::new()) }
    }

    /// For every hook in `hooks`, add it to the list of hooks that are executed when `dispatch`
    /// is called for `hook.event`.
    pub async fn install(&self, hooks: Vec<HooksRegistration>) {
        let mut hooks_map = self.hooks_map.lock().await;
        for hook in hooks {
            for event in hook.events {
                let existing_hooks = hooks_map.entry(event).or_insert(vec![]);
                existing_hooks.push(hook.callback.clone());
            }
        }
    }

    /// Same as `install`, but adds the hook to the front of the queue.
    ///
    /// This is test-only because in general it shouldn't matter what order hooks are executed
    /// in. This is useful for tests that need guarantees about hook execution order.
    #[cfg(test)]
    pub async fn install_front(&self, hooks: Vec<HooksRegistration>) {
        let mut hooks_map = self.hooks_map.lock().await;
        for hook in hooks {
            for event in hook.events {
                let existing_hooks = hooks_map.entry(event).or_insert(vec![]);
                existing_hooks.insert(0, hook.callback.clone());
            }
        }
    }

    pub async fn dispatch(&self, event: &Event) -> Result<(), ModelError> {
        let strong_hooks = {
            let mut hooks_map = self.hooks_map.lock().await;
            if let Some(hooks) = hooks_map.get_mut(&event.event_type()) {
                // We must upgrade our weak references to hooks to strong ones before we can
                // call out to them.
                let mut strong_hooks = vec![];
                hooks.retain(|hook| {
                    if let Some(hook) = hook.upgrade() {
                        strong_hooks.push(hook);
                        true
                    } else {
                        false
                    }
                });
                strong_hooks
            } else {
                vec![]
            }
        };
        for hook in strong_hooks {
            hook.on(event).await?;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {super::*, moniker::AbsoluteMonikerBase, std::sync::Arc};

    // This test verifies that the payload of the CapabilityRequested event will be transferred.
    #[fuchsia::test]
    async fn capability_requested_transfer() {
        let (_, capability_server_end) = zx::Channel::create().unwrap();
        let capability_server_end = Arc::new(Mutex::new(Some(capability_server_end)));
        let event = Event::new_for_test(
            AbsoluteMoniker::root(),
            "fuchsia-pkg://root",
            EventPayload::CapabilityRequested {
                source_moniker: AbsoluteMoniker::root(),
                name: "foo".to_string(),
                capability: capability_server_end,
            },
        );

        // Verify the transferred event carries the capability.
        let transferred_event = event.transfer().await;
        match transferred_event.payload {
            EventPayload::CapabilityRequested { capability, .. } => {
                let capability = capability.lock().await;
                assert!(capability.is_some());
            }
            _ => panic!("Event type unexpected"),
        }

        // Verify that the original event no longer carries the capability.
        match &event.payload {
            EventPayload::CapabilityRequested { capability, .. } => {
                let capability = capability.lock().await;
                assert!(capability.is_none());
            }
            _ => panic!("Event type unexpected"),
        }

        // Transferring the original event again should give an empty capability provider.
        let second_transferred_event = event.transfer().await;
        match &second_transferred_event.payload {
            EventPayload::CapabilityRequested { capability, .. } => {
                let capability = capability.lock().await;
                assert!(capability.is_none());
            }
            _ => panic!("Event type unexpected"),
        }
    }
}
