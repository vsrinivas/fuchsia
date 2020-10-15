// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_ui_shortcut as ui_shortcut,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    futures::{StreamExt, TryStreamExt},
};

use crate::{
    registry::{Shortcut, Subscriber},
    service::KeyEvent,
    Environment,
};

/// FIDL services handled by `Router`.
enum Services {
    RegistryServer(ui_shortcut::RegistryRequestStream),
    ManagerServer(ui_shortcut::ManagerRequestStream),
}

/// Router handled incoming FIDL requests and invokes appropriate services.
pub struct Router {
    environment: Environment,
}

impl Router {
    pub fn new(environment: Environment) -> Self {
        Router { environment }
    }

    /// Sets up and starts serving `ServiceFs`.
    pub async fn setup_and_serve_fs(&self) -> Result<(), Error> {
        let mut fs = ServiceFs::new();
        fs.dir("svc")
            .add_fidl_service(Services::RegistryServer)
            .add_fidl_service(Services::ManagerServer);
        fs.take_and_serve_directory_handle()?;

        fs.for_each_concurrent(None, |incoming_service| async {
            match incoming_service {
                Services::RegistryServer(stream) => {
                    self.registry_server(stream).await.context("registry server error")
                }
                Services::ManagerServer(stream) => {
                    self.manager_server(stream).await.context("manager server error")
                }
            }
            .unwrap_or_else(|e| fx_log_err!("request failed: {:?}", e))
        })
        .await;

        Ok(())
    }

    /// Normalize shortcuts for input2 and input3 APIs.
    /// Those are safe to compose since they operate of different set of
    /// shortcut parameters.
    async fn normalize_shortcut(&self, mut shortcut: &mut ui_shortcut::Shortcut) {
        self.environment.registry_service.normalize_shortcut(&mut shortcut);
        self.environment.input2_service.lock().await.normalize_shortcut(&mut shortcut);
    }

    /// Handles requests to `fuchsia.ui.shortcut.Registry` interface.
    async fn registry_server(
        &self,
        mut stream: ui_shortcut::RegistryRequestStream,
    ) -> Result<(), Error> {
        // The lifetime of the shortcuts is determined by the lifetime of the connection,
        // so once this registry goes out of scope, it's removed from RegistryStore.
        let client_registry = self.environment.store.add_new_registry().await;

        // TODO: clean up empty Weak refs for registries from the store

        while let Some(req) = stream.try_next().await.context("error running registry server")? {
            match req {
                ui_shortcut::RegistryRequest::SetView { view_ref, listener, .. } => {
                    // New subscriber overrides the old one.
                    client_registry.lock().await.subscriber = Some(Subscriber {
                        view_ref,
                        listener: listener.into_proxy().context("listener error")?,
                    });
                }

                ui_shortcut::RegistryRequest::RegisterShortcut {
                    mut shortcut, responder, ..
                } => {
                    self.normalize_shortcut(&mut shortcut).await;
                    {
                        // Get and release a client registry in a closure to lock
                        // the client registry for a minimal amount of time.
                        client_registry.lock().await.shortcuts.push(Shortcut::new(shortcut));
                    }
                    responder.send().unwrap_or_else(|e| {
                        // Handle the error to prevent service shutdown in case of
                        // misbehaving client.
                        fx_log_info!("responding to a shortcut registration: {:?}", e)
                    });
                }
            }
        }
        Ok(())
    }

    /// Handles requests to `fuchsia.ui.shortcut.Manager` interface.
    async fn manager_server(
        &self,
        mut stream: ui_shortcut::ManagerRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = stream.try_next().await.context("error running manager server")? {
            match req {
                // Handle input2 key events for input2 shortcuts.
                ui_shortcut::ManagerRequest::HandleKeyEvent { event, responder } => {
                    let was_handled = {
                        let mut input2_service = self.environment.input2_service.lock().await;
                        input2_service
                            .handle_key_event(event)
                            .await
                            .context("handle input2 event")?
                    };
                    responder.send(was_handled).context("error sending response")?;
                }
                // Handle input3 key events for input3 shortcuts.
                ui_shortcut::ManagerRequest::HandleKey3Event { event, responder } => {
                    let was_handled = {
                        let mut service = self.environment.manager_service.lock().await;
                        match KeyEvent::new(event) {
                            Ok(key_event) => {
                                service.handle_key(key_event).await.context("handle key event")?
                            }
                            Err(e) => {
                                fx_log_err!("Error handling: {}", e);
                                false
                            }
                        }
                    };
                    responder.send(was_handled).context("error sending response")?;
                }
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    // There's not much value in unit tests, because of the complexity of the setup and amount of
    // logic in those. Instead, see the integration tests at shortcut/tests/.
}
