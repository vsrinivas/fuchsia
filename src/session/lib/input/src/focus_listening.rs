// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_ui_focus as focus, fidl_fuchsia_ui_input as fidl_ui_input,
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog::fx_log_err,
    futures::StreamExt,
};

/// Registers as a focus chain listener and dispatches focus chain updates to IME.
pub async fn handle_focus_changes() -> Result<(), Error> {
    let ime = connect_to_service::<fidl_ui_input::ImeServiceMarker>()?;

    let (focus_chain_listener_client_end, focus_chain_listener) =
        fidl::endpoints::create_request_stream::<focus::FocusChainListenerMarker>()?;

    let focus_chain_listener_registry: focus::FocusChainListenerRegistryProxy =
        connect_to_service::<focus::FocusChainListenerRegistryMarker>()?;
    focus_chain_listener_registry
        .register(focus_chain_listener_client_end)
        .expect("Failed to register focus chain listener.");

    dispatch_focus_change_to_ime(ime, focus_chain_listener).await
}

/// Dispatches focus chain updates from `focus_chain_listener` to `ime`.
///
/// # Parameters
/// `ime`: A proxy to the IME service.
/// `focus_chain_listener`: A channel that receives focus chain updates.
async fn dispatch_focus_change_to_ime(
    ime: fidl_ui_input::ImeServiceProxy,
    mut focus_chain_listener: focus::FocusChainListenerRequestStream,
) -> Result<(), Error> {
    while let Some(focus_change) = focus_chain_listener.next().await {
        match focus_change {
            Ok(focus::FocusChainListenerRequest::OnFocusChange {
                focus_chain, responder, ..
            }) => {
                if let Some(mut focus_chain) = focus_chain.focus_chain {
                    if let Some(mut view_ref) = focus_chain.pop() {
                        ime.view_focus_changed(&mut view_ref).await?;
                    }
                };
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
        super::*, fidl_fuchsia_ui_views as ui_views, fuchsia_scenic as scenic,
        fuchsia_zircon::AsHandleRef, futures::future::join,
    };

    /// Listens for a ViewRef from a view focus change request on `ime_request_stream`.
    ///
    /// # Parameters
    /// `ime_request_stream`: A channel where ViewFocusChanged requests are received.
    ///
    /// # Returns
    /// The ViewRef of the focused view.
    async fn expect_focus_change(
        mut ime_request_stream: fidl_ui_input::ImeServiceRequestStream,
    ) -> ui_views::ViewRef {
        match ime_request_stream.next().await {
            Some(Ok(fidl_ui_input::ImeServiceRequest::ViewFocusChanged {
                view_ref,
                responder,
                ..
            })) => {
                let _ = responder.send();
                view_ref
            }
            _ => panic!("Error expecting focus change."),
        }
    }

    /// Tests focused view routing from FocusChainListener to IME service.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn dispatch_focus() -> Result<(), Error> {
        let (ime_proxy, ime_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_ui_input::ImeServiceMarker>()?;

        let (focus_chain_listener_client_end, focus_chain_listener) =
            fidl::endpoints::create_proxy_and_stream::<focus::FocusChainListenerMarker>()?;

        fuchsia_async::Task::spawn(async move {
            let _ = dispatch_focus_change_to_ime(ime_proxy, focus_chain_listener).await;
        })
        .detach();

        let view_ref = scenic::ViewRefPair::new()?.view_ref;
        let view_ref_dup = fuchsia_scenic::duplicate_view_ref(&view_ref)?;
        let focus_chain =
            focus::FocusChain { focus_chain: Some(vec![view_ref]), ..focus::FocusChain::empty() };

        let (_, view_ref) = join(
            focus_chain_listener_client_end.on_focus_change(focus_chain),
            expect_focus_change(ime_request_stream),
        )
        .await;

        assert_eq!(
            view_ref.reference.as_handle_ref().get_koid(),
            view_ref_dup.reference.as_handle_ref().get_koid()
        );

        Ok(())
    }
}
