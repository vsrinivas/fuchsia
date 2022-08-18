// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fuchsia_ui_focus as focus, fidl_fuchsia_ui_keyboard_focus as kbd_focus,
    fidl_fuchsia_ui_shortcut as fidl_ui_shortcut,
    focus_chain_provider::FocusChainProviderPublisher,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_syslog::{fx_log_err, fx_log_warn},
    futures::StreamExt,
};

/// FocusListener listens to focus change and notify to related input modules.
pub struct FocusListener {
    /// The FIDL proxy to text_manager.
    text_manager: kbd_focus::ControllerProxy,

    /// The FIDL proxy to shortcut manager.
    shortcut_manager: fidl_ui_shortcut::ManagerProxy,

    /// A channel that receives focus chain updates.
    focus_chain_listener: focus::FocusChainListenerRequestStream,

    /// Forwards focus chain updates to downstream watchers.
    focus_chain_publisher: FocusChainProviderPublisher,
}

impl FocusListener {
    /// Creates a new focus listener that holds proxy to text manager and shortcut manager.
    /// The caller is expected to spawn a task to continually listen to focus change event.
    ///
    /// # Arguments
    /// - `focus_chain_publisher`: Allows focus chain updates to be sent to downstream listeners.
    ///   Note that this is not required for `FocusListener` to function, so it could be made an
    ///  `Option` in future changes.
    ///
    /// # Example
    ///
    /// ```ignore
    /// let mut listener = FocusListener::new(focus_chain_publisher);
    /// let task = fuchsia_async::Task::local(async move {
    ///     listener.dispatch_focus_changes().await
    /// });
    /// ```
    ///
    /// # FIDL
    ///
    /// Required:
    ///
    /// - `fuchsia.ui.views.FocusChainListener`
    /// - `fuchsia.ui.shortcut.Manager`
    /// - `fuchsia.ui.keyboard.focus.Controller`
    ///
    /// # Errors
    /// If unable to connect to text_manager, shortcut manager or protocols.
    pub fn new(focus_chain_publisher: FocusChainProviderPublisher) -> Result<Self, Error> {
        let text_manager = connect_to_protocol::<kbd_focus::ControllerMarker>()?;
        let shortcut_manager = connect_to_protocol::<fidl_ui_shortcut::ManagerMarker>()?;

        let (focus_chain_listener_client_end, focus_chain_listener) =
            fidl::endpoints::create_request_stream::<focus::FocusChainListenerMarker>()?;

        let focus_chain_listener_registry: focus::FocusChainListenerRegistryProxy =
            connect_to_protocol::<focus::FocusChainListenerRegistryMarker>()?;
        focus_chain_listener_registry
            .register(focus_chain_listener_client_end)
            .context("Failed to register focus chain listener.")?;

        Ok(Self::new_listener(
            text_manager,
            shortcut_manager,
            focus_chain_listener,
            focus_chain_publisher,
        ))
    }

    /// Creates a new focus listener that holds proxy to text and shortcut manager.
    /// The caller is expected to spawn a task to continually listen to focus change event.
    ///
    /// # Parameters
    /// - `text_manager`: A proxy to the text manager service.
    /// - `shortcut_manager`: A proxy to the shortcut manager service.
    /// - `focus_chain_listener`: A channel that receives focus chain updates.
    /// - `focus_chain_publisher`: Forwards focus chain updates to downstream watchers.
    ///
    /// # Errors
    /// If unable to connect to text_manager, shortcut manager or protocols.
    fn new_listener(
        text_manager: kbd_focus::ControllerProxy,
        shortcut_manager: fidl_ui_shortcut::ManagerProxy,
        focus_chain_listener: focus::FocusChainListenerRequestStream,
        focus_chain_publisher: FocusChainProviderPublisher,
    ) -> Self {
        Self { text_manager, shortcut_manager, focus_chain_listener, focus_chain_publisher }
    }

    /// Dispatches focus chain updates from `focus_chain_listener` to `text_manager`,
    /// `shortcut_manager`, and any subscribers of `focus_chain_publisher`.
    pub async fn dispatch_focus_changes(&mut self) -> Result<(), Error> {
        while let Some(focus_change) = self.focus_chain_listener.next().await {
            match focus_change {
                Ok(focus::FocusChainListenerRequest::OnFocusChange {
                    focus_chain,
                    responder,
                    ..
                }) => {
                    // Dispatch to downstream watchers.
                    self.focus_chain_publisher
                        .set_state_and_notify_if_changed(&focus_chain)
                        .context("while notifying FocusChainProviderPublisher")?;

                    // Dispatch to text manager.
                    if let Some(ref focus_chain) = focus_chain.focus_chain {
                        if let Some(ref view_ref) = focus_chain.last() {
                            let mut view_ref_dup = fuchsia_scenic::duplicate_view_ref(&view_ref)?;
                            self.text_manager
                                .notify(&mut view_ref_dup)
                                .await
                                .context("while notifying text_manager")?;
                        }
                    };

                    // Dispatch to shortcut manager.
                    self.shortcut_manager
                        .handle_focus_change(focus_chain)
                        .await
                        .context("while notifying shortcut_manager")?;

                    responder.send().context("while sending focus chain listener response")?;
                }
                Err(e) => fx_log_err!("FocusChainListenerRequest has error: {}.", e),
            }
        }
        fx_log_warn!("Stopped dispatching focus changes.");
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, fidl_fuchsia_ui_focus_ext::FocusChainExt, fidl_fuchsia_ui_views as fidl_ui_views,
        fidl_fuchsia_ui_views_ext::ViewRefExt, fuchsia_scenic as scenic, futures::join,
        pretty_assertions::assert_eq,
    };

    /// Listens for a ViewRef from a view focus change request on `request_stream`.
    ///
    /// # Parameters
    /// `request_stream`: A channel where ViewFocusChanged requests are received.
    ///
    /// # Returns
    /// The ViewRef of the focused view.
    async fn expect_focus_ctl_focus_change(
        mut request_stream: kbd_focus::ControllerRequestStream,
    ) -> fidl_ui_views::ViewRef {
        match request_stream.next().await {
            Some(Ok(kbd_focus::ControllerRequest::Notify { view_ref, responder, .. })) => {
                let _ = responder.send();
                view_ref
            }
            _ => panic!("Error expecting text_manager focus change."),
        }
    }

    /// Listens for a ViewRef from a view focus change request on `manager_request_stream`.
    ///
    /// # Parameters
    /// `shortcut_manager_request_stream`: A stream of Manager requests that contains
    /// HandleFocusChange requests.
    ///
    /// # Returns
    /// The updated FocusChain.
    async fn expect_shortcut_focus_change(
        mut shortcut_manager_request_stream: fidl_ui_shortcut::ManagerRequestStream,
    ) -> focus::FocusChain {
        match shortcut_manager_request_stream.next().await {
            Some(Ok(fidl_ui_shortcut::ManagerRequest::HandleFocusChange {
                focus_chain,
                responder,
                ..
            })) => {
                let _ = responder.send();
                focus_chain
            }
            _ => panic!("Error expecting shortcut focus change."),
        }
    }

    async fn expect_focus_koid_chain(
        focus_chain_provider_proxy: &focus::FocusChainProviderProxy,
    ) -> focus::FocusKoidChain {
        focus_chain_provider_proxy
            .watch_focus_koid_chain(focus::FocusChainProviderWatchFocusKoidChainRequest::EMPTY)
            .await
            .expect("watch_focus_koid_chain")
    }

    /// Tests focused view routing from FocusChainListener to text_manager service and shortcut manager.
    #[fuchsia_async::run_until_stalled(test)]
    async fn dispatch_focus() -> Result<(), Error> {
        let (focus_proxy, focus_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<kbd_focus::ControllerMarker>()?;
        let (shortcut_manager_proxy, shortcut_manager_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_shortcut::ManagerMarker>()?;

        let (focus_chain_listener_client_end, focus_chain_listener) =
            fidl::endpoints::create_proxy_and_stream::<focus::FocusChainListenerMarker>()?;

        let (focus_chain_watcher, focus_chain_provider_stream) =
            fidl::endpoints::create_proxy_and_stream::<focus::FocusChainProviderMarker>()?;
        let (focus_chain_provider_publisher, focus_chain_provider_stream_handler) =
            focus_chain_provider::make_publisher_and_stream_handler();
        focus_chain_provider_stream_handler
            .handle_request_stream(focus_chain_provider_stream)
            .detach();

        let mut listener = FocusListener::new_listener(
            focus_proxy,
            shortcut_manager_proxy,
            focus_chain_listener,
            focus_chain_provider_publisher,
        );

        fuchsia_async::Task::local(async move {
            let _ = listener.dispatch_focus_changes().await;
        })
        .detach();

        // Flush the initial value from the hanging get server.
        // Note that if the focus chain watcher tried to retrieve the koid chain for the first time
        // inside the `join!` statement below, concurrently with the update operation, it would end
        // up receiving the old value.
        let got_focus_koid_chain = expect_focus_koid_chain(&focus_chain_watcher).await;
        assert_eq!(got_focus_koid_chain, focus::FocusKoidChain::EMPTY);

        let view_ref = scenic::ViewRefPair::new()?.view_ref;
        let view_ref_dup = fuchsia_scenic::duplicate_view_ref(&view_ref)?;
        let focus_chain =
            focus::FocusChain { focus_chain: Some(vec![view_ref]), ..focus::FocusChain::EMPTY };

        let (_, view_ref, got_focus_chain, got_focus_koid_chain) = join!(
            focus_chain_listener_client_end.on_focus_change(focus_chain.duplicate().unwrap()),
            expect_focus_ctl_focus_change(focus_request_stream),
            expect_shortcut_focus_change(shortcut_manager_request_stream),
            expect_focus_koid_chain(&focus_chain_watcher),
        );

        assert_eq!(view_ref.get_koid().unwrap(), view_ref_dup.get_koid().unwrap(),);

        assert_eq!(1, got_focus_chain.len());
        assert_eq!(
            view_ref_dup.get_koid().unwrap(),
            got_focus_chain
                .koids()
                .next()
                .expect("focus chain is non-empty")
                .expect("ViewRef's koid was retrieved"),
        );

        assert!(focus_chain.equivalent(&got_focus_koid_chain).unwrap());

        Ok(())
    }
}
