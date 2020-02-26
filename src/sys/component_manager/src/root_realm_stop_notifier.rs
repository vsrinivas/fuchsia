// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        error::ModelError,
        hooks::{Event, EventType, Hook, HooksRegistration},
    },
    async_trait::async_trait,
    futures::{channel::*, lock::Mutex},
    std::sync::{Arc, Weak},
};

/// Notifies when the root instance has been destroyed by ComponentManager.
/// This is used to terminate ComponentManager when the root component has been destroyed.
/// TODO(xbhatnag): Consider replacing this with breakpoints.
pub struct RootRealmStopNotifier {
    rx: Mutex<Option<oneshot::Receiver<()>>>,
    tx: Mutex<Option<oneshot::Sender<()>>>,
}

impl RootRealmStopNotifier {
    pub fn new() -> Self {
        let (tx, rx) = oneshot::channel();
        Self { rx: Mutex::new(Some(rx)), tx: Mutex::new(Some(tx)) }
    }

    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![HooksRegistration::new(
            "RootRealmStopNotifier",
            vec![EventType::StopInstance],
            Arc::downgrade(self) as Weak<dyn Hook>,
        )]
    }

    pub async fn wait_for_root_realm_stop(&self) {
        let rx = { self.rx.lock().await.take() };
        if let Some(rx) = rx {
            rx.await.expect("Failed to wait for root instance to be stopped");
        }
    }
}

#[async_trait]
impl Hook for RootRealmStopNotifier {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        if event.target_moniker.is_root() {
            let tx = { self.tx.lock().await.take() };
            if let Some(tx) = tx {
                tx.send(()).expect("Could not notify on StopInstance of root realm");
            }
        }
        Ok(())
    }
}
