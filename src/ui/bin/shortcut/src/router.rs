// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Result},
    fidl_fuchsia_ui_shortcut as ui_shortcut,
    futures::TryStreamExt,
    tracing::{error, info},
};

use crate::{
    registry::{Shortcut, Subscriber},
    service::KeyEvent,
    Environment,
};
use fidl_fuchsia_ui_shortcut2::{self as fs2, HandledUnknown};

/// Router handled incoming FIDL requests and invokes appropriate services.
pub struct Router {
    environment: Environment,
}

impl Router {
    pub fn new(environment: Environment) -> Self {
        Router { environment }
    }

    /// Normalize shortcuts for input3 APIs.
    /// Those are safe to compose since they operate of different set of
    /// shortcut parameters.
    async fn normalize_shortcut(&self, mut shortcut: &mut ui_shortcut::Shortcut) {
        self.environment.registry_service.normalize_shortcut(&mut shortcut);
    }

    /// Handles requests to `fuchsia.ui.shortcut.Registry` interface.
    pub async fn registry_server(
        &self,
        mut stream: ui_shortcut::RegistryRequestStream,
    ) -> Result<()> {
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
                    // A shortcut registration invalidates the previously computed
                    // focused registries, so let's update it now.
                    self.environment.store.recompute_focused_registries().await;
                    responder.send().unwrap_or_else(|e| {
                        // Handle the error to prevent service shutdown in case of
                        // misbehaving client.
                        info!("responding to a shortcut registration: {:?}", e)
                    });
                }
            }
        }
        Ok(())
    }

    /// Handles requests to `fuchsia.ui.shortcut2.Registry`.
    pub async fn handle_registry_stream(
        &self,
        stream: fidl_fuchsia_ui_shortcut2::RegistryRequestStream,
    ) -> Result<()> {
        // TODO(fxbug.dev/106089): This extra level of indirection will be
        // removed when the "shortcut v1" implementation is removed.
        self.environment.shortcut2.handle_registry_stream(stream).await
    }

    /// Handles requests to `fuchsia.ui.shortcut.Manager` interface.
    pub async fn manager_server(
        &self,
        mut stream: ui_shortcut::ManagerRequestStream,
    ) -> Result<()> {
        while let Some(req) = stream.try_next().await.context("error running manager server")? {
            match req {
                // Handle input3 key events for input3 shortcuts.
                ui_shortcut::ManagerRequest::HandleKey3Event { event, responder } => {
                    let mut was_handled = {
                        let mut service = self.environment.manager_service.lock().await;
                        match KeyEvent::new(event.clone()) {
                            Ok(key_event) => {
                                service.handle_key(key_event).await.context("handle key event")?
                            }
                            Err(e) => {
                                error!("Error handling: {}", e);
                                false
                            }
                        }
                    };
                    if !was_handled {
                        was_handled =
                            match self.environment.shortcut2.handle_key_event(event).await? {
                                fs2::Handled::Handled => true,
                                fs2::Handled::NotHandled | HandledUnknown!() => false,
                            };
                    }
                    responder
                        .send(was_handled)
                        .context("Error sending response for HandleKey3Event")?;
                }
                ui_shortcut::ManagerRequest::HandleFocusChange {
                    focus_chain, responder, ..
                } => {
                    self.environment.store.handle_focus_change(&focus_chain).await;
                    self.environment.shortcut2.handle_focus_change(&focus_chain).await;
                    responder.send()?;
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
