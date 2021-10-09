// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};
use fidl_fuchsia_power_button::{Action, MonitorMarker, MonitorProxy};
use fuchsia_async::Task;
use fuchsia_syslog::fx_log_err;
use std::sync::{Arc, Mutex};

struct PowerButtonInner {
    proxy: MonitorProxy,
    inhibit_count: usize,
    orig_action: Action,
    release_task: Option<Task<()>>,
}

pub struct PowerButton {
    inner: Mutex<PowerButtonInner>,
}

impl PowerButton {
    pub fn new_from_namespace() -> Result<Arc<Self>, Error> {
        let proxy = fuchsia_component::client::connect_to_protocol::<MonitorMarker>()
            .context("Connecting to power button monitor")?;
        Ok(PowerButton::new(proxy))
    }

    pub fn new(proxy: MonitorProxy) -> Arc<Self> {
        Arc::new(PowerButton {
            inner: Mutex::new(PowerButtonInner {
                proxy,
                inhibit_count: 0,
                orig_action: Action::Shutdown,
                release_task: None,
            }),
        })
    }

    /// Make sure that the power button does nothing.
    pub async fn inhibit(self: Arc<Self>) -> Result<PowerButtonInhibitor, Error> {
        {
            // TODO: fxbug.dev/86245
            #[allow(must_not_suspend)]
            let mut inner = self.inner.lock().unwrap();
            if inner.inhibit_count == 0 {
                // Make sure the previous cancellation ran.
                if let Some(task) = inner.release_task.take() {
                    task.await;
                }
                let state = inner.proxy.get_action().await.context("Sending get_action")?;
                inner.orig_action = state;
                inner.proxy.set_action(Action::Ignore).await.context("Sending set_action")?;
            }
            inner.inhibit_count += 1;
        }
        Ok(PowerButtonInhibitor { button: self })
    }

    fn release(&self) {
        let mut inner = self.inner.lock().unwrap();
        inner.inhibit_count -= 1;
        if inner.inhibit_count == 0 {
            let proxy = inner.proxy.clone();
            assert_eq!(inner.release_task.is_none(), true);
            let action = inner.orig_action;
            inner.release_task = Some(fuchsia_async::Task::spawn(async move {
                proxy.set_action(action).await.unwrap_or_else(|e| {
                    fx_log_err!("Failed to restore power button action: {:?}", e);
                });
            }));
        }
    }
}

/// RAII struct returned by PowerButton.inhibit().
/// When it is dropped, the power button's state is restored.
#[must_use]
pub struct PowerButtonInhibitor {
    button: Arc<PowerButton>,
}

impl Drop for PowerButtonInhibitor {
    fn drop(&mut self) {
        self.button.release();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_power_button::{Action, MonitorRequest};
    use futures::TryStreamExt;

    struct FakePowerButtonManager {
        action: Mutex<Action>,
    }

    impl FakePowerButtonManager {
        pub fn new() -> Arc<Self> {
            Arc::new(FakePowerButtonManager { action: Mutex::new(Action::Shutdown) })
        }

        pub fn serve(self: Arc<Self>) -> MonitorProxy {
            let (proxy, mut stream) =
                fidl::endpoints::create_proxy_and_stream::<MonitorMarker>().unwrap();

            Task::spawn(async move {
                while let Some(req) = stream.try_next().await.unwrap() {
                    match req {
                        MonitorRequest::GetAction { responder } => responder
                            .send(*self.action.lock().unwrap())
                            .expect("Replying to GetAction"),
                        MonitorRequest::SetAction { action, responder } => {
                            *self.action.lock().unwrap() = action;
                            responder.send().expect("Replying to SetAction");
                        }
                    }
                }
            })
            .detach();

            proxy
        }

        pub fn get_action(&self) -> Action {
            *self.action.lock().unwrap()
        }
    }

    #[fuchsia::test]
    async fn test_inhibit() {
        let manager = FakePowerButtonManager::new();
        let button = PowerButton::new(manager.clone().serve());

        {
            let _inhibitor = button.clone().inhibit().await.expect("Inhibit succeeds");
            assert_eq!(manager.get_action(), Action::Ignore);
        }

        // Make sure the async task is executed.
        match button.inner.lock().unwrap().release_task.take() {
            Some(task) => task.await,
            None => {}
        }
        assert_eq!(manager.get_action(), Action::Shutdown);
    }

    #[fuchsia::test]
    async fn test_inhibit_twice() {
        let manager = FakePowerButtonManager::new();
        let button = PowerButton::new(manager.clone().serve());

        {
            let _inhibitor = button.clone().inhibit().await.expect("Inhibit succeeds");
            assert_eq!(manager.get_action(), Action::Ignore);
            {
                let _inhibitor2 = button.clone().inhibit().await.expect("Second inhibit succeeds");
                assert_eq!(manager.get_action(), Action::Ignore);
            }
            // Shouldn't yet be trying to release.
            assert!(button.inner.lock().unwrap().release_task.is_none());
            assert_eq!(manager.get_action(), Action::Ignore);
        }

        // Make sure the async task is executed.
        match button.inner.lock().unwrap().release_task.take() {
            Some(task) => task.await,
            None => {}
        }
        assert_eq!(manager.get_action(), Action::Shutdown);
    }
}
