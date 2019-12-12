// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        error::ModelError,
        hooks::{Event, EventType, Hook, HooksRegistration},
        moniker::AbsoluteMoniker,
    },
    futures::channel::*,
    futures::future::BoxFuture,
    futures::lock::Mutex,
    std::sync::{Arc, Weak},
};

/// Notifies when the root instance has been destroyed by ComponentManager.
/// This is used to terminate ComponentManager when the root component has been destroyed.
/// TODO(xbhatnag): Consider replacing this with breakpoints.
pub struct RootRealmStopNotifier {
    pub rx: oneshot::Receiver<()>,
    inner: Arc<RootRealmStopNotifierInner>,
}

impl RootRealmStopNotifier {
    pub fn new() -> Self {
        let (tx, rx) = oneshot::channel();
        let inner = Arc::new(RootRealmStopNotifierInner { tx: Mutex::new(Some(tx)) });
        return Self { rx, inner };
    }

    pub fn hooks(&self) -> Vec<HooksRegistration> {
        vec![HooksRegistration {
            events: vec![EventType::StopInstance],
            callback: Arc::downgrade(&self.inner) as Weak<dyn Hook>,
        }]
    }

    pub async fn wait_for_root_realm_stop(self) {
        self.rx.await.expect("Failed to wait for root instance to be stopped");
    }
}

struct RootRealmStopNotifierInner {
    tx: Mutex<Option<oneshot::Sender<()>>>,
}

impl Hook for RootRealmStopNotifierInner {
    fn on<'a>(self: Arc<Self>, event: &'a Event) -> BoxFuture<'a, Result<(), ModelError>> {
        let inner = self.clone();
        Box::pin(async move {
            if event.target_realm.abs_moniker == AbsoluteMoniker::root() {
                let tx = inner.tx.lock().await.take();
                tx.expect("Root instance can only be stopped once.")
                    .send(())
                    .expect("Could not notify on StopInstance of root realm");
            }
            Ok(())
        })
    }
}
