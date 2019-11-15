// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{AbsoluteMoniker, Event, EventType, Hook, HooksRegistration, ModelError},
    futures::channel::*,
    futures::future::BoxFuture,
    futures::lock::Mutex,
    std::sync::{Arc, Weak},
};

/// Notifies when the root instance has been destroyed by ComponentManager.
/// This is used to terminate ComponentManager when the root component has been destroyed.
/// TODO(xbhatnag): Consider replacing this with breakpoints.
pub struct RootRealmPostDestroyNotifier {
    pub rx: oneshot::Receiver<()>,
    inner: Arc<RootRealmPostDestroyNotifierInner>,
}

impl RootRealmPostDestroyNotifier {
    pub fn new() -> Self {
        let (tx, rx) = oneshot::channel();
        let inner = Arc::new(RootRealmPostDestroyNotifierInner { tx: Mutex::new(Some(tx)) });
        return Self { rx, inner };
    }

    pub fn hooks(&self) -> Vec<HooksRegistration> {
        vec![HooksRegistration {
            events: vec![EventType::PostDestroyInstance],
            callback: Arc::downgrade(&self.inner) as Weak<dyn Hook>,
        }]
    }

    pub async fn wait_for_root_realm_destroy(self) {
        self.rx.await.expect("Failed to wait for root instance to be destroyed");
    }
}

struct RootRealmPostDestroyNotifierInner {
    tx: Mutex<Option<oneshot::Sender<()>>>,
}

impl Hook for RootRealmPostDestroyNotifierInner {
    fn on<'a>(self: Arc<Self>, event: &'a Event) -> BoxFuture<'a, Result<(), ModelError>> {
        let inner = self.clone();
        Box::pin(async move {
            if event.target_realm().abs_moniker == AbsoluteMoniker::root() {
                let tx = inner.tx.lock().await.take();
                tx.expect("Root instance can only be destroyed once.")
                    .send(())
                    .expect("Could not notify on PostDestroyInstance of root realm");
            }
            Ok(())
        })
    }
}
