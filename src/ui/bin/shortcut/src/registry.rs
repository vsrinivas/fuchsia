// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_input as input, fidl_fuchsia_ui_shortcut as ui_shortcut,
    fidl_fuchsia_ui_views as ui_views,
    futures::lock::{Mutex, MutexGuard},
    std::collections::HashSet,
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
    /// Returned reference contains `MutexGuard`, and as a result it prevents
    /// other uses of client registry store (e.g. adding new, removing) until goes
    /// out of scope.
    pub async fn get_registries<'a>(&'a self) -> LockedRegistries<'a> {
        LockedRegistries(self.inner.lock().await)
    }
}

impl RegistryStoreInner {
    /// Create and add new client registry of shortcuts to the store.
    fn add_new_registry(&mut self) -> Arc<Mutex<ClientRegistry>> {
        let registry = Arc::new(Mutex::new(ClientRegistry::default()));
        self.registries.push(Arc::downgrade(&registry));
        registry
    }
}

/// Holds `MutexGuard` in order to provide exclusive access to vector
/// of client registries.
/// Implements `Deref` and can be used as `Vec<Weak<Mutex<ClientRegistry>>>`.
pub struct LockedRegistries<'a>(MutexGuard<'a, RegistryStoreInner>);

impl<'a> Deref for LockedRegistries<'a> {
    type Target = Vec<Weak<Mutex<ClientRegistry>>>;
    fn deref(&self) -> &Self::Target {
        &self.0.registries
    }
}

/// Abstraction wrapper for FIDL `fuchsia.ui.shortcut.Shortcut`.
/// Implements `Deref` and can be used as `fidl_fuchsia_ui_shortcut::Shortcut`.
#[derive(Debug)]
pub struct Shortcut {
    inner: ui_shortcut::Shortcut,

    /// Set of required keys to be pressed to activate this shortcut.
    pub keys_required_hash: Option<HashSet<input::Key>>,
}

impl Shortcut {
    pub fn new(shortcut: ui_shortcut::Shortcut) -> Self {
        let keys_required_hash: Option<HashSet<_>> = shortcut
            .keys_required
            .as_ref()
            .cloned()
            .map(|keys_required| keys_required.into_iter().collect::<HashSet<_>>());
        Self { inner: shortcut, keys_required_hash }
    }
}

impl Deref for Shortcut {
    type Target = ui_shortcut::Shortcut;
    fn deref(&self) -> &Self::Target {
        &self.inner
    }
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
            id: None,
            modifiers: None,
            key: None,
            use_priority: None,
            trigger: None,
            key3: None,
        };
        let shortcut = Shortcut::new(fidl_shortcut);
        let keys_required_hash = Some([input::Key::A, input::Key::B].iter().cloned().collect());
        assert_eq!(shortcut.keys_required_hash, keys_required_hash);
    }
}
