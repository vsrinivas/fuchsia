// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_input as input, fidl_fuchsia_ui_focus as ui_focus,
    fidl_fuchsia_ui_shortcut as ui_shortcut, fidl_fuchsia_ui_views as ui_views,
    fuchsia_scenic as scenic,
    fuchsia_syslog::{fx_log_debug, fx_log_info},
    fuchsia_zircon as zx,
    fuchsia_zircon::AsHandleRef,
    futures::{
        lock::{MappedMutexGuard, Mutex, MutexGuard},
        stream, StreamExt,
    },
    std::collections::{HashMap, HashSet},
    std::ops::Deref,
    std::sync::{Arc, Weak},
    std::vec::Vec,
};

/// Describes a shortcut activation listener.
pub struct Subscriber {
    pub view_ref: ui_views::ViewRef,
    pub listener: fidl_fuchsia_ui_shortcut::ListenerProxy,
}

/// Describes all data related to a single connected client:
/// - shortcut activation listener
/// - shortcuts installed
#[derive(Default)]
pub struct ClientRegistry {
    pub subscriber: Option<Subscriber>,
    pub shortcuts: Vec<Shortcut>,
}

/// Describes all shortcuts and listeners.
#[derive(Clone)]
pub struct RegistryStore {
    inner: Arc<Mutex<RegistryStoreInner>>,
}

#[derive(Default)]
struct RegistryStoreInner {
    // Weak ref for ClientRegistry to allow shortcuts to be dropped out
    // of collection once client connection is removed.
    registries: Vec<Weak<Mutex<ClientRegistry>>>,

    // The last received focus chain.  `None` if an update was never received
    // since startup (rare), or set by `handle_focus_change`.
    last_seen_focus_chain: Option<Vec<fidl_fuchsia_ui_views::ViewRef>>,

    // Currently focused clients in the FocusChain order, i.e. parents first.
    // It is computed eagerly on state changes, such that shortcut matching could
    // be done quickly on a trigger key.
    //
    // The focused_registries is invalidated on any changes to the focus or the
    // registry itself and needs to be recomputed when `registries` or
    // `last_seen_focus_chain` change.
    //
    // Use `recompute_focused_registries` to keep the registries up to date.
    //
    // # Invariant
    //
    // At any given time, `focused_registries` contains all registries
    // whose `ViewRef`s are matching the `last_seen_focus_chain`.
    focused_registries: Vec<Weak<Mutex<ClientRegistry>>>,
}

impl RegistryStore {
    pub fn new() -> Self {
        let inner = Arc::new(Mutex::new(RegistryStoreInner::default()));
        RegistryStore { inner }
    }

    /// Add a new client registry to the store.
    /// Newly added client registry is returned.
    pub async fn add_new_registry(&self) -> Arc<Mutex<ClientRegistry>> {
        self.inner.lock().await.add_new_registry()
    }

    /// Get all client registries.
    /// Returned reference can be used as a `Vec<Weak<Mutex<ClientRegistry>>>`.
    /// Returned reference contains `MutexGuard`, and as a result it prevents
    /// other uses of client registry store (e.g. adding new, removing) until goes
    /// out of scope.
    /// Is only used for testing.
    #[cfg(test)]
    async fn get_registries<'a>(&'a self) -> LockedRegistries<'a> {
        LockedRegistries(MutexGuard::map(self.inner.lock().await, |r| &mut r.registries))
    }

    /// Get client registries for currently focused clients.
    /// Returned reference can be used as a `Vec<Weak<Mutex<ClientRegistry>>>`.
    /// Returned reference contains `MutexGuard`, and as a result it prevents
    /// other uses of client registry store (e.g. adding new, removing) until goes
    /// out of scope.
    /// Returned reference is ordered by Scenic View hierarchy, parent first.
    pub async fn get_focused_registries<'a>(&'a self) -> LockedRegistries<'a> {
        LockedRegistries(MutexGuard::map(self.inner.lock().await, |r| &mut r.focused_registries))
    }

    /// Update client registries to account for a `FocusChain` event.
    pub async fn handle_focus_change(&self, focus_chain: &ui_focus::FocusChain) {
        let focus_chain = match focus_chain {
            ui_focus::FocusChain { focus_chain: Some(focus_chain), .. } => focus_chain,
            _ => return,
        };
        self.inner.lock().await.update_focused_registries(focus_chain).await;
    }

    /// Recomputes the focused registries based on the current state of the focus chain.
    /// Review: pub(crate)?
    pub async fn recompute_focused_registries(&self) {
        self.inner.lock().await.recompute_focused_registries().await;
    }
}

impl RegistryStoreInner {
    /// Create and add new client registry of shortcuts to the store.
    fn add_new_registry(&mut self) -> Arc<Mutex<ClientRegistry>> {
        let registry = Arc::new(Mutex::new(ClientRegistry::default()));
        self.registries.push(Arc::downgrade(&registry));
        registry
    }

    async fn recompute_focused_registries(&mut self) {
        // A better way would be not to clone the focus chain but the mutability
        // of self would need to be rethought for that. Use this approach until
        // performance becomes an issue.
        let focus_chain = self.last_seen_focus_chain.as_ref().map(|f| clone_focus_chain(f));
        if let Some(ref f) = focus_chain {
            fx_log_debug!("recompute_focused_registries: focus_chain: {:?}", koids_of(f));
            self.update_focused_registries(f).await;
        }
    }

    /// Updates `self.focused_registries` to reflect current focus chain.
    async fn update_focused_registries(
        &mut self,
        focus_chain: &Vec<fidl_fuchsia_ui_views::ViewRef>,
    ) {
        // Iterator over all active registries, i.e. all `Weak` refs upgraded.
        let registries_iter = stream::iter(
            self.registries
                .iter()
                .cloned()
                .filter_map(|registry_weak: Weak<Mutex<ClientRegistry>>| {
                    registry_weak.upgrade().map(|registry_arc: Arc<Mutex<ClientRegistry>>| {
                        (registry_arc, registry_weak.clone())
                    })
                })
                .into_iter(),
        );

        // Acquire Mutex locks and hash all client registries by `Koid`.
        let mut registries: HashMap<zx::Koid, Weak<Mutex<ClientRegistry>>> = registries_iter
            .filter_map(|(registry_arc, registry_weak)| async move {
                if let ClientRegistry { subscriber: Some(Subscriber { view_ref, .. }), .. } =
                    &*registry_arc.lock().await
                {
                    let koid = match view_ref.reference.as_handle_ref().get_koid() {
                        Ok(koid) => koid,
                        // If Koid can't be received, this means view ref is invalid
                        // and can't be notified.
                        _ => {
                            fx_log_info!("Client uses invalid ViewRef: {:?}", view_ref);
                            return None;
                        }
                    };
                    Some((koid, registry_weak))
                } else {
                    // Client registry has no subscriber set and can't be notified.
                    fx_log_info!("Client has no listener and view ref set up.");
                    None
                }
            })
            .collect()
            .await;

        // Keep the last focus chain so that early shortcut registrations would
        // be recomputed correctly.
        self.last_seen_focus_chain = Some(clone_focus_chain(focus_chain));

        // Only focused shortcut clients are retained, and other view_refs are dropped.
        self.focused_registries = focus_chain
            .into_iter()
            .filter_map(|view_ref| {
                view_ref
                    .reference
                    .as_handle_ref()
                    .get_koid()
                    .ok()
                    .and_then(|koid| registries.remove(&koid))
            })
            .collect();
        fx_log_debug!(
            "update_focused_registries: updated focus chain to: {:?}, num focused registries: {:?}",
            &self.last_seen_focus_chain.as_ref().map(|f| koids_of(f)),
            &self.focused_registries.len()
        );
    }
}

/// Holds `MutexGuard` in order to provide exclusive access to vector
/// of client registries.
/// Implements `Deref` and can be used as `Vec<Weak<Mutex<ClientRegistry>>>`.
pub struct LockedRegistries<'a>(
    MappedMutexGuard<'a, RegistryStoreInner, Vec<Weak<Mutex<ClientRegistry>>>>,
);

impl<'a> Deref for LockedRegistries<'a> {
    type Target = Vec<Weak<Mutex<ClientRegistry>>>;
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

/// Abstraction wrapper for FIDL `fuchsia.ui.shortcut.Shortcut`.
/// Implements `Deref` and can be used as `fidl_fuchsia_ui_shortcut::Shortcut`.
#[derive(Debug)]
pub struct Shortcut {
    inner: ui_shortcut::Shortcut,

    /// The keys that must be armed (already actuated) before this shortcut may
    /// be triggered.
    pub required_armed_keys: Option<HashSet<input::Key>>,
}

impl Shortcut {
    pub fn new(shortcut: ui_shortcut::Shortcut) -> Self {
        let required_armed_keys: Option<HashSet<_>> = shortcut
            .keys_required
            .as_ref()
            .cloned()
            .map(|keys_required| keys_required.into_iter().collect::<HashSet<_>>());
        Self { inner: shortcut, required_armed_keys }
    }
}

impl Deref for Shortcut {
    type Target = ui_shortcut::Shortcut;
    fn deref(&self) -> &Self::Target {
        &self.inner
    }
}

/// Produces a clone of the supplied `focus_chain`.  This is a separate free function since
/// the `ViewRef`s that `focus_chain` consists of are not trivially cloneable, and it is
/// unclear whether this function is of general interest or not.
///
/// # Performance
///
/// It takes about 1us to clone a ViewRef.
fn clone_focus_chain(
    focus_chain: &Vec<fidl_fuchsia_ui_views::ViewRef>,
) -> Vec<fidl_fuchsia_ui_views::ViewRef> {
    focus_chain
        .iter()
        .map(|v| scenic::duplicate_view_ref(v).expect("failed to clone a ViewRef"))
        .collect()
}

/// Turns `focus_chain` into a sequence of corresponding `zx::Koid`s.  Mainly useful
/// for debugging.
fn koids_of(focus_chain: &Vec<fidl_fuchsia_ui_views::ViewRef>) -> Vec<zx::Koid> {
    focus_chain
        .iter()
        .map(|v| v.reference.as_handle_ref().get_koid().expect("failed to get koid"))
        .collect()
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_async as fasync};

    #[fasync::run_singlethreaded(test)]
    async fn new_registry_adds_registry() {
        let store = RegistryStore::new();
        let client_registry = store.add_new_registry().await;
        let all_registries = store.get_registries().await;

        assert_eq!(1, all_registries.len());

        let first_registry = all_registries.first().unwrap().upgrade().unwrap();

        assert!(Arc::ptr_eq(&client_registry, &first_registry));
    }

    #[fasync::run_singlethreaded(test)]
    async fn shortcut_populates_keys_hash() {
        let fidl_shortcut = ui_shortcut::Shortcut {
            keys_required: Some(vec![input::Key::A, input::Key::B]),
            ..ui_shortcut::Shortcut::EMPTY
        };
        let shortcut = Shortcut::new(fidl_shortcut);
        assert_eq!(
            shortcut.required_armed_keys,
            Some([input::Key::A, input::Key::B].iter().cloned().collect())
        );
    }
}
