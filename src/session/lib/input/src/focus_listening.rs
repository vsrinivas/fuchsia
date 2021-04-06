// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    fidl_fuchsia_ui_focus as focus, fidl_fuchsia_ui_input as fidl_ui_input,
    fidl_fuchsia_ui_shortcut as fidl_ui_shortcut,
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog::fx_log_err,
    futures::StreamExt,
};

/// Registers as a focus chain listener and dispatches focus chain updates to IME
/// and shortcut manager.
pub async fn handle_focus_changes() -> Result<(), Error> {
    let ime = connect_to_service::<fidl_ui_input::ImeServiceMarker>()?;
    let shortcut_manager = connect_to_service::<fidl_ui_shortcut::ManagerMarker>()?;

    let (focus_chain_listener_client_end, focus_chain_listener) =
        fidl::endpoints::create_request_stream::<focus::FocusChainListenerMarker>()?;

    let focus_chain_listener_registry: focus::FocusChainListenerRegistryProxy =
        connect_to_service::<focus::FocusChainListenerRegistryMarker>()?;
    focus_chain_listener_registry
        .register(focus_chain_listener_client_end)
        .context("Failed to register focus chain listener.")?;

    dispatch_focus_changes(ime, shortcut_manager, focus_chain_listener).await
}

/// Dispatches focus chain updates from `focus_chain_listener` to
/// `ime` and `shortcut_manager`.
///
/// # Parameters
/// `ime`: A proxy to the IME service.
/// `shortcut_manager`: A proxy to the shortcut manager service.
/// `focus_chain_listener`: A channel that receives focus chain updates.
async fn dispatch_focus_changes(
    ime: fidl_ui_input::ImeServiceProxy,
    shortcut_manager: fidl_ui_shortcut::ManagerProxy,
    mut focus_chain_listener: focus::FocusChainListenerRequestStream,
) -> Result<(), Error> {
    while let Some(focus_change) = focus_chain_listener.next().await {
        match focus_change {
            Ok(focus::FocusChainListenerRequest::OnFocusChange {
                focus_chain, responder, ..
            }) => {
                // Dispatch to IME.
                if let Some(ref focus_chain) = focus_chain.focus_chain {
                    if let Some(ref view_ref) = focus_chain.last() {
                        let mut view_ref_dup = fuchsia_scenic::duplicate_view_ref(&view_ref)?;
                        ime.view_focus_changed(&mut view_ref_dup).await?;
                    }
                };

                // Dispatch to shortcut manager.
                shortcut_manager.handle_focus_change(focus_chain).await?;

                responder.send()?;
            }
            Err(e) => fx_log_err!("FocusChainListenerRequest has error: {}.", e),
        }
    }

    Err(format_err!("Stopped dispatching focus changes."))
}

#[cfg(test)]
mod tests {
    use {
        super::*, fidl_fuchsia_ui_focus as fidl_ui_focus,
        fidl_fuchsia_ui_shortcut as fidl_ui_shortcut, fidl_fuchsia_ui_views as fidl_ui_views,
        fuchsia_scenic as scenic, fuchsia_zircon::AsHandleRef, futures::join,
    };

    /// Listens for a ViewRef from a view focus change request on `ime_request_stream`.
    ///
    /// # Parameters
    /// `ime_request_stream`: A channel where ViewFocusChanged requests are received.
    ///
    /// # Returns
    /// The ViewRef of the focused view.
    async fn expect_ime_focus_change(
        mut ime_request_stream: fidl_ui_input::ImeServiceRequestStream,
    ) -> fidl_ui_views::ViewRef {
        match ime_request_stream.next().await {
            Some(Ok(fidl_ui_input::ImeServiceRequest::ViewFocusChanged {
                view_ref,
                responder,
                ..
            })) => {
                let _ = responder.send();
                view_ref
            }
            _ => panic!("Error expecting IME focus change."),
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

    /// Tests focused view routing from FocusChainListener to IME service and shortcut manager.
    #[fuchsia_async::run_until_stalled(test)]
    async fn dispatch_focus() -> Result<(), Error> {
        let (ime_proxy, ime_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_input::ImeServiceMarker>()?;
        let (shortcut_manager_proxy, shortcut_manager_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_shortcut::ManagerMarker>()?;

        let (focus_chain_listener_client_end, focus_chain_listener) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_focus::FocusChainListenerMarker>()?;

        fuchsia_async::Task::spawn(async move {
            let _ = dispatch_focus_changes(ime_proxy, shortcut_manager_proxy, focus_chain_listener)
                .await;
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
            expect_ime_focus_change(ime_request_stream),
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
