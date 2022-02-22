// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_ui_keyboard_focus as fidl_focus;
use {
    anyhow::{format_err, Context, Error},
    fidl_fuchsia_ui_focus as focus, fidl_fuchsia_ui_shortcut as fidl_ui_shortcut,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_syslog::fx_log_err,
    futures::StreamExt,
};

/// FocusListener listens to focus change and notify to related input modules.
pub struct FocusListener {
    /// The FIDL proxy to text_manager.
    text_manager: fidl_focus::ControllerProxy,

    /// The FIDL proxy to shortcut manager.
    shortcut_manager: fidl_ui_shortcut::ManagerProxy,

    /// A channel that receives focus chain updates.
    focus_chain_listener: focus::FocusChainListenerRequestStream,
}

impl FocusListener {
    /// Creates a new focus listener that holds proxy to text manager and shortcut manager.
    /// The caller is expected to spawn a task to continually listen to focus change event.
    /// Example:
    /// let mut listener = FocusListener::new();
    /// fuchsia_async::Task::local(async move {
    ///     let _ = listener.dispatch_focus_changes().await;
    /// }).detach();
    ///
    /// # Errors
    /// If unable to connect to text_manager, shortcut manager or protocols.
    pub fn new() -> Result<Self, Error> {
        let text_manager = connect_to_protocol::<fidl_focus::ControllerMarker>()?;
        let shortcut_manager = connect_to_protocol::<fidl_ui_shortcut::ManagerMarker>()?;

        let (focus_chain_listener_client_end, focus_chain_listener) =
            fidl::endpoints::create_request_stream::<focus::FocusChainListenerMarker>()?;

        let focus_chain_listener_registry: focus::FocusChainListenerRegistryProxy =
            connect_to_protocol::<focus::FocusChainListenerRegistryMarker>()?;
        focus_chain_listener_registry
            .register(focus_chain_listener_client_end)
            .context("Failed to register focus chain listener.")?;

        Ok(Self::new_listener(text_manager, shortcut_manager, focus_chain_listener))
    }

    /// Creates a new focus listener that holds proxy to text and shortcut manager.
    /// The caller is expected to spawn a task to continually listen to focus change event.
    ///
    /// # Parameters
    /// - `text_manager`: A proxy to the text manager service.
    /// - `shortcut_manager`: A proxy to the shortcut manager service.
    /// - `focus_chain_listener`: A channel that receives focus chain updates.
    ///
    /// # Errors
    /// If unable to connect to text_manager, shortcut manager or protocols.
    fn new_listener(
        text_manager: fidl_focus::ControllerProxy,
        shortcut_manager: fidl_ui_shortcut::ManagerProxy,
        focus_chain_listener: focus::FocusChainListenerRequestStream,
    ) -> Self {
        Self { text_manager, shortcut_manager, focus_chain_listener }
    }

    /// Dispatches focus chain updates from `focus_chain_listener` to `text_manager` and `shortcut_manager`.
    pub async fn dispatch_focus_changes(&mut self) -> Result<(), Error> {
        while let Some(focus_change) = self.focus_chain_listener.next().await {
            match focus_change {
                Ok(focus::FocusChainListenerRequest::OnFocusChange {
                    focus_chain,
                    responder,
                    ..
                }) => {
                    // Dispatch to text manager.
                    if let Some(ref focus_chain) = focus_chain.focus_chain {
                        if let Some(ref view_ref) = focus_chain.last() {
                            let mut view_ref_dup = fuchsia_scenic::duplicate_view_ref(&view_ref)?;
                            self.text_manager.notify(&mut view_ref_dup).await?;
                        }
                    };

                    // Dispatch to shortcut manager.
                    self.shortcut_manager.handle_focus_change(focus_chain).await?;

                    responder.send()?;
                }
                Err(e) => fx_log_err!("FocusChainListenerRequest has error: {}.", e),
            }
        }

        Err(format_err!("Stopped dispatching focus changes."))
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, fidl_fuchsia_ui_focus as fidl_ui_focus,
        fidl_fuchsia_ui_shortcut as fidl_ui_shortcut, fidl_fuchsia_ui_views as fidl_ui_views,
        fuchsia_scenic as scenic, fuchsia_zircon::AsHandleRef, futures::join,
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
        mut request_stream: fidl_focus::ControllerRequestStream,
    ) -> fidl_ui_views::ViewRef {
        match request_stream.next().await {
            Some(Ok(fidl_focus::ControllerRequest::Notify { view_ref, responder, .. })) => {
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
    ) -> fidl_ui_focus::FocusChain {
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

    /// Tests focused view routing from FocusChainListener to text_manager service and shortcut manager.
    #[fuchsia_async::run_until_stalled(test)]
    async fn dispatch_focus() -> Result<(), Error> {
        let (focus_proxy, focus_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_focus::ControllerMarker>()?;
        let (shortcut_manager_proxy, shortcut_manager_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_shortcut::ManagerMarker>()?;

        let (focus_chain_listener_client_end, focus_chain_listener) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_focus::FocusChainListenerMarker>()?;

        let mut listener =
            FocusListener::new_listener(focus_proxy, shortcut_manager_proxy, focus_chain_listener);

        fuchsia_async::Task::local(async move {
            let _ = listener.dispatch_focus_changes().await;
        })
        .detach();

        let view_ref = scenic::ViewRefPair::new()?.view_ref;
        let view_ref_dup = fuchsia_scenic::duplicate_view_ref(&view_ref)?;
        let focus_chain = fidl_ui_focus::FocusChain {
            focus_chain: Some(vec![view_ref]),
            ..fidl_ui_focus::FocusChain::EMPTY
        };

        let (_, view_ref, got_focus_chain) = join!(
            focus_chain_listener_client_end.on_focus_change(focus_chain),
            expect_focus_ctl_focus_change(focus_request_stream),
            expect_shortcut_focus_change(shortcut_manager_request_stream),
        );

        assert_eq!(
            view_ref.reference.as_handle_ref().get_koid(),
            view_ref_dup.reference.as_handle_ref().get_koid()
        );

        let got_focus_chain_vec = got_focus_chain.focus_chain.unwrap();
        assert_eq!(1, got_focus_chain_vec.len());
        assert_eq!(
            view_ref_dup.reference.as_handle_ref().get_koid(),
            got_focus_chain_vec.first().unwrap().reference.as_handle_ref().get_koid()
        );

        Ok(())
    }
}
