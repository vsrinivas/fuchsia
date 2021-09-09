// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Extensions for fuchsia.net.interfaces.admin.

use fidl_fuchsia_net_interfaces_admin as fnet_interfaces_admin;
use futures::{FutureExt as _, Stream, StreamExt as _, TryStreamExt as _};
use thiserror::Error;

/// Error type when using a [`fnet_interfaces_admin::AddressStateProviderProxy`].
#[derive(Error, Debug)]
pub enum AddressStateProviderError {
    /// Address removed error.
    #[error("address removed: {0:?}")]
    AddressRemoved(fnet_interfaces_admin::AddressRemovalReason),
    /// FIDL error.
    #[error("fidl error")]
    Fidl(#[from] fidl::Error),
    /// Channel closed.
    #[error("AddressStateProvider channel closed")]
    ChannelClosed,
}

// TODO(https://fxbug.dev/81964): Introduce type with better concurrency safety
// for hanging gets.
/// Returns a stream of assignment states obtained by watching on `address_state_provider`.
///
/// Note that this function calls the hanging get FIDL method
/// [`AddressStateProviderProxy::watch_address_assignment_state`] internally,
/// which means that this stream should not be polled concurrently with any
/// logic which calls the same hanging get. This also means that callers should
/// be careful not to drop the returned stream when it has been polled but yet
/// to yield an item, e.g. due to a timeout or if using select with another
/// stream, as doing so causes a pending hanging get to get lost, and may cause
/// future hanging get calls to fail or the channel to be closed.
pub fn assignment_state_stream(
    address_state_provider: fnet_interfaces_admin::AddressStateProviderProxy,
) -> impl Stream<Item = Result<fnet_interfaces_admin::AddressAssignmentState, AddressStateProviderError>>
{
    let event_stream = address_state_provider
        .take_event_stream()
        .map_err(AddressStateProviderError::Fidl)
        .and_then(
            |fnet_interfaces_admin::AddressStateProviderEvent::OnAddressRemoved { error }| {
                futures::future::err(AddressStateProviderError::AddressRemoved(error))
            },
        );
    let state_stream =
        futures::stream::try_unfold(address_state_provider, |address_state_provider| {
            address_state_provider.watch_address_assignment_state().map(|r| match r {
                Ok(state) => Ok(Some((state, address_state_provider))),
                Err(e) => {
                    if e.is_closed() {
                        Ok(None)
                    } else {
                        Err(AddressStateProviderError::Fidl(e))
                    }
                }
            })
        });
    // TODO(https://github.com/rust-lang/futures-rs/issues/2476): Terminate the
    // stream upon the first error with something more terse than the
    // `take_while` block.
    //
    // FIDL errors other than PEER_CLOSED will be observed on both streams
    // (note that the `try_unfold` stream will swallow PEER_CLOSED so that the
    // address removal reason can be read from the event stream). Though this
    // is not a problem technically, we terminate the stream upon yielding
    // the first error to prevent the same error from being yielded twice.
    futures::stream::select(event_stream, state_stream).take_while({
        let mut error_observed = false;
        move |r| {
            futures::future::ready(match r {
                Ok(_) => true,
                Err(_) => !std::mem::replace(&mut error_observed, true),
            })
        }
    })
}

// TODO(https://fxbug.dev/81964): Introduce type with better concurrency safety
// for hanging gets.
/// Wait until the Assigned state is observed on `stream`.
///
/// After this async function resolves successfully, the underlying
/// `AddressStateProvider` may be used as usual. If an error is returned, a
/// terminal error has occurred on the underlying channel.
pub async fn wait_assignment_state<S>(
    stream: &mut S,
    want: fidl_fuchsia_net_interfaces_admin::AddressAssignmentState,
) -> Result<(), AddressStateProviderError>
where
    S: Stream<
            Item = Result<fnet_interfaces_admin::AddressAssignmentState, AddressStateProviderError>,
        > + Unpin,
{
    stream
        .try_filter_map(|state| futures::future::ok((state == want).then(|| ())))
        .try_next()
        .await
        .and_then(|opt| opt.ok_or_else(|| AddressStateProviderError::ChannelClosed))
}

#[cfg(test)]
mod test {
    use super::{assignment_state_stream, AddressStateProviderError};
    use fidl_fuchsia_net_interfaces_admin as fnet_interfaces_admin;
    use futures::{FutureExt as _, StreamExt as _, TryStreamExt as _};

    // Test that the terminal event is observed when the server closes its end.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_assignment_state_stream() {
        let (address_state_provider, server_end) =
            fidl::endpoints::create_proxy::<fnet_interfaces_admin::AddressStateProviderMarker>()
                .expect("failed to create proxy");
        let state_stream = assignment_state_stream(address_state_provider);
        futures::pin_mut!(state_stream);

        const REMOVAL_REASON_INVALID: fnet_interfaces_admin::AddressRemovalReason =
            fnet_interfaces_admin::AddressRemovalReason::Invalid;
        {
            let (mut request_stream, control_handle) = server_end
                .into_stream_and_control_handle()
                .expect("failed to create stream and control handle");

            const ASSIGNMENT_STATE_ASSIGNED: fnet_interfaces_admin::AddressAssignmentState =
                fnet_interfaces_admin::AddressAssignmentState::Assigned;
            let state_fut = state_stream.try_next().map(|r| {
                assert_eq!(
                    r.expect("state stream error").expect("state stream ended"),
                    ASSIGNMENT_STATE_ASSIGNED
                )
            });
            let handle_fut = request_stream.try_next().map(|r| match r.expect("request stream error").expect("request stream ended") {
                fnet_interfaces_admin::AddressStateProviderRequest::WatchAddressAssignmentState { responder } => {
                    let () = responder.send(ASSIGNMENT_STATE_ASSIGNED).expect("failed to send stubbed assignment state");
                }
                req => panic!("unexpected method called: {:?}", req),
            });
            let ((), ()) = futures::join!(state_fut, handle_fut);

            let () = control_handle
                .send_on_address_removed(REMOVAL_REASON_INVALID)
                .expect("failed to send fake INVALID address removal reason event");
        }

        matches::assert_matches!(
            state_stream.try_collect::<Vec<_>>().await,
            Err(AddressStateProviderError::AddressRemoved(got)) if got == REMOVAL_REASON_INVALID
        );
    }

    // Test that only one error is returned on the assignment state stream when
    // an error observable on both the client proxy and the event stream occurs.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_assignment_state_stream_single_error() {
        let (address_state_provider, server_end) =
            fidl::endpoints::create_proxy::<fnet_interfaces_admin::AddressStateProviderMarker>()
                .expect("failed to create proxy");
        let state_stream = assignment_state_stream(address_state_provider);

        let () = server_end
            .close_with_epitaph(fidl::Status::INTERNAL)
            .expect("failed to send INTERNAL epitaph");

        // Use collect rather than try_collect to ensure that we don't observe
        // multiple errors on this stream.
        matches::assert_matches!(
            state_stream
                .collect::<Vec<_>>()
                .now_or_never()
                .expect("state stream not immediately ready")
                .as_slice(),
            [Err(AddressStateProviderError::Fidl(fidl::Error::ClientChannelClosed {
                status: fidl::Status::INTERNAL,
                protocol_name: _,
            }))]
        );
    }
}
