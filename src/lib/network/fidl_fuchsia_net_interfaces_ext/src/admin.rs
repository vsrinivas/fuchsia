// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Extensions for fuchsia.net.interfaces.admin.

use fidl_fuchsia_net_interfaces_admin as fnet_interfaces_admin;
use fuchsia_zircon_status as zx;
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

impl From<TerminalError<fnet_interfaces_admin::AddressRemovalReason>>
    for AddressStateProviderError
{
    fn from(e: TerminalError<fnet_interfaces_admin::AddressRemovalReason>) -> Self {
        match e {
            TerminalError::Fidl(e) => AddressStateProviderError::Fidl(e),
            TerminalError::Terminal(r) => AddressStateProviderError::AddressRemoved(r),
        }
    }
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
    let event_fut = address_state_provider.take_event_stream().into_future().map(
        |(item, _stream)| match item {
            Some(Ok(
                fidl_fuchsia_net_interfaces_admin::AddressStateProviderEvent::OnAddressRemoved {
                    error,
                },
            )) => AddressStateProviderError::AddressRemoved(error),
            Some(Err(e)) => AddressStateProviderError::Fidl(e),
            None => AddressStateProviderError::ChannelClosed,
        },
    );
    futures::stream::try_unfold(
        (address_state_provider, event_fut),
        |(address_state_provider, event_fut)| {
            // NB: Rely on the fact that select always polls the left future
            // first to guarantee that if a terminal event was yielded by the
            // right future, then we don't have an assignment state to emit to
            // clients.
            futures::future::select(
                address_state_provider.watch_address_assignment_state(),
                event_fut,
            )
            .then(|s| match s {
                futures::future::Either::Left((state_result, event_fut)) => match state_result {
                    Ok(state) => {
                        futures::future::ok(Some((state, (address_state_provider, event_fut))))
                            .left_future()
                    }
                    Err(e) if e.is_closed() => event_fut.map(Result::Err).right_future(),
                    Err(e) => {
                        futures::future::err(AddressStateProviderError::Fidl(e)).left_future()
                    }
                },
                futures::future::Either::Right((error, _state_fut)) => {
                    futures::future::err(error).left_future()
                }
            })
        },
    )
}

// TODO(https://fxbug.dev/81964): Introduce type with better concurrency safety
// for hanging gets.
/// Wait until the Assigned state is observed on `stream`.
///
/// After this async function resolves successfully, the underlying
/// `AddressStateProvider` may be used as usual. If an error is returned, a
/// terminal error has occurred on the underlying channel.
pub async fn wait_assignment_state<S>(
    stream: S,
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

type ControlEventStreamFutureToReason =
    fn(
        (
            Option<Result<fnet_interfaces_admin::ControlEvent, fidl::Error>>,
            fnet_interfaces_admin::ControlEventStream,
        ),
    ) -> Result<Option<fnet_interfaces_admin::InterfaceRemovedReason>, fidl::Error>;

/// A wrapper for fuchsia.net.interfaces.admin/Control that observes terminal
/// events.
#[derive(Clone)]
pub struct Control {
    proxy: fnet_interfaces_admin::ControlProxy,
    // Keeps a shared future that will resolve when the first event is seen on a
    // ControlEventStream. The shared future makes the observed terminal event
    // "sticky" for as long as we clone the future before polling it. Note that
    // we don't drive the event stream to completion, the future is resolved
    // when the first event is seen. That means this relies on the terminal
    // event contract but does *not* enforce that the channel is closed
    // immediately after or that no other events are issued.
    terminal_event_fut: futures::future::Shared<
        futures::future::Map<
            futures::stream::StreamFuture<fnet_interfaces_admin::ControlEventStream>,
            ControlEventStreamFutureToReason,
        >,
    >,
}

impl Control {
    /// Calls AddAddress on the proxy.
    pub fn add_address(
        &self,
        address: &mut fidl_fuchsia_net::Subnet,
        parameters: fnet_interfaces_admin::AddressParameters,
        address_state_provider: fidl::endpoints::ServerEnd<
            fnet_interfaces_admin::AddressStateProviderMarker,
        >,
    ) -> Result<(), TerminalError<fnet_interfaces_admin::InterfaceRemovedReason>> {
        self.or_terminal_event_no_return(self.proxy.add_address(
            address,
            parameters,
            address_state_provider,
        ))
    }

    /// Calls GetId on the proxy.
    pub async fn get_id(
        &self,
    ) -> Result<u64, TerminalError<fnet_interfaces_admin::InterfaceRemovedReason>> {
        self.or_terminal_event(self.proxy.get_id()).await
    }

    /// Calls RemoveAddress on the proxy.
    pub async fn remove_address(
        &self,
        address: &mut fidl_fuchsia_net::Subnet,
    ) -> Result<
        fnet_interfaces_admin::ControlRemoveAddressResult,
        TerminalError<fnet_interfaces_admin::InterfaceRemovedReason>,
    > {
        self.or_terminal_event(self.proxy.remove_address(address)).await
    }

    /// Calls SetConfiguration on the proxy.
    pub async fn set_configuration(
        &self,
        config: fnet_interfaces_admin::Configuration,
    ) -> Result<
        fnet_interfaces_admin::ControlSetConfigurationResult,
        TerminalError<fnet_interfaces_admin::InterfaceRemovedReason>,
    > {
        self.or_terminal_event(self.proxy.set_configuration(config)).await
    }

    /// Calls GetConfiguration on the proxy.
    pub async fn get_configuration(
        &self,
    ) -> Result<
        fnet_interfaces_admin::ControlGetConfigurationResult,
        TerminalError<fnet_interfaces_admin::InterfaceRemovedReason>,
    > {
        self.or_terminal_event(self.proxy.get_configuration()).await
    }

    /// Calls Enable on the proxy.
    pub async fn enable(
        &self,
    ) -> Result<
        fnet_interfaces_admin::ControlEnableResult,
        TerminalError<fnet_interfaces_admin::InterfaceRemovedReason>,
    > {
        self.or_terminal_event(self.proxy.enable()).await
    }

    /// Calls Disable on the proxy.
    pub async fn disable(
        &self,
    ) -> Result<
        fnet_interfaces_admin::ControlDisableResult,
        TerminalError<fnet_interfaces_admin::InterfaceRemovedReason>,
    > {
        self.or_terminal_event(self.proxy.disable()).await
    }

    /// Calls Detach on the proxy.
    pub fn detach(
        &self,
    ) -> Result<(), TerminalError<fnet_interfaces_admin::InterfaceRemovedReason>> {
        self.or_terminal_event_no_return(self.proxy.detach())
    }

    /// Creates a new `Control` wrapper from `proxy`.
    pub fn new(proxy: fnet_interfaces_admin::ControlProxy) -> Self {
        let terminal_event_fut = proxy
            .take_event_stream()
            .into_future()
            .map::<_, ControlEventStreamFutureToReason>(|(event, _stream)| {
                event
                    .map(|r| {
                        r.map(|event| {
                            let fidl_fuchsia_net_interfaces_admin::ControlEvent::OnInterfaceRemoved {
                                reason,
                            } = event;
                            reason
                        })
                    })
                    .transpose()
            })
            .shared();
        Self { proxy, terminal_event_fut }
    }

    /// Waits for interface removal.
    pub async fn wait_termination(
        self,
    ) -> TerminalError<fnet_interfaces_admin::InterfaceRemovedReason> {
        let Self { proxy: _, terminal_event_fut } = self;
        match terminal_event_fut.await {
            Ok(Some(event)) => TerminalError::Terminal(event),
            Ok(None) => TerminalError::Fidl(fidl::Error::ClientRead(zx::Status::PEER_CLOSED)),
            Err(e) => TerminalError::Fidl(e),
        }
    }

    /// Creates a new `Control` and its `ServerEnd`.
    pub fn create_endpoints(
    ) -> Result<(Self, fidl::endpoints::ServerEnd<fnet_interfaces_admin::ControlMarker>), fidl::Error>
    {
        let (proxy, server_end) = fidl::endpoints::create_proxy()?;
        Ok((Self::new(proxy), server_end))
    }

    async fn or_terminal_event<R: Unpin>(
        &self,
        fut: fidl::client::QueryResponseFut<R>,
    ) -> Result<R, TerminalError<fnet_interfaces_admin::InterfaceRemovedReason>> {
        match futures::future::select(fut, self.terminal_event_fut.clone()).await {
            futures::future::Either::Left((query_result, fut)) => match query_result {
                Ok(ok) => Ok(ok),
                Err(e) if e.is_closed() => match fut.await {
                    Ok(Some(reason)) => Err(TerminalError::Terminal(reason)),
                    Ok(None) | Err(_) => Err(TerminalError::Fidl(e)),
                },
                Err(e) => Err(TerminalError::Fidl(e)),
            },
            futures::future::Either::Right((event, fut)) => {
                match event.map_err(|e| TerminalError::Fidl(e))? {
                    Some(removal_reason) => Err(TerminalError::Terminal(removal_reason)),
                    None => {
                        if let Some(query_result) = fut.now_or_never() {
                            query_result.map_err(TerminalError::Fidl)
                        } else {
                            Err(TerminalError::Fidl(fidl::Error::ClientRead(
                                zx::Status::PEER_CLOSED,
                            )))
                        }
                    }
                }
            }
        }
    }

    fn or_terminal_event_no_return(
        &self,
        r: Result<(), fidl::Error>,
    ) -> Result<(), TerminalError<fnet_interfaces_admin::InterfaceRemovedReason>> {
        r.map_err(|err| {
            if !err.is_closed() {
                return TerminalError::Fidl(err);
            }
            // TODO(https://fxbug.dev/96755): The terminal event may have been
            // sent by the server but the future may not resolve immediately,
            // resulting in the terminal event being missed and a FIDL error
            // being returned to the user.
            //
            // Poll event stream to see if we have a terminal event to return
            // instead of a FIDL closed error.
            match self.terminal_event_fut.clone().now_or_never() {
                Some(Ok(Some(terminal_event))) => TerminalError::Terminal(terminal_event),
                Some(Err(e)) => {
                    // Prefer the error observed by the proxy.
                    let _: fidl::Error = e;
                    TerminalError::Fidl(err)
                }
                None | Some(Ok(None)) => TerminalError::Fidl(err),
            }
        })
    }
}

impl std::fmt::Debug for Control {
    fn fmt(&self, fmt: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let Self { proxy, terminal_event_fut: _ } = self;
        fmt.debug_struct("Control").field("proxy", proxy).finish()
    }
}

/// Errors observed from wrapped terminal events.
#[derive(Debug)]
pub enum TerminalError<E> {
    /// Terminal event was observed.
    Terminal(E),
    /// A FIDL error occurred.
    Fidl(fidl::Error),
}

impl<E> std::fmt::Display for TerminalError<E>
where
    E: std::fmt::Debug,
{
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            TerminalError::Terminal(e) => write!(f, "terminal event: {:?}", e),
            TerminalError::Fidl(e) => write!(f, "fidl error: {}", e),
        }
    }
}

impl<E: std::fmt::Debug> std::error::Error for TerminalError<E> {}

#[cfg(test)]
mod test {
    use super::{assignment_state_stream, AddressStateProviderError};
    use fidl::prelude::*;
    use fidl_fuchsia_net_interfaces_admin as fnet_interfaces_admin;
    use fuchsia_zircon_status as zx;
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

        assert_matches::assert_matches!(
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
        assert_matches::assert_matches!(
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

    // Test that if an assignment state and a terminal event is available at
    // the same time, the state is yielded first.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn assignment_state_stream_state_before_event() {
        let (address_state_provider, mut request_stream) =
            fidl::endpoints::create_proxy_and_stream::<
                fnet_interfaces_admin::AddressStateProviderMarker,
            >()
            .expect("failed to create proxy");

        const ASSIGNMENT_STATE_ASSIGNED: fnet_interfaces_admin::AddressAssignmentState =
            fnet_interfaces_admin::AddressAssignmentState::Assigned;
        const REMOVAL_REASON_INVALID: fnet_interfaces_admin::AddressRemovalReason =
            fnet_interfaces_admin::AddressRemovalReason::Invalid;

        let ((), ()) = futures::future::join(
            async move {
                let () = request_stream
                    .try_next()
                    .await
                    .expect("request stream error")
                    .expect("request stream ended")
                    .into_watch_address_assignment_state()
                    .expect("unexpected request")
                    .send(ASSIGNMENT_STATE_ASSIGNED)
                    .expect("failed to send stubbed assignment state");
                let () = request_stream
                    .control_handle()
                    .send_on_address_removed(REMOVAL_REASON_INVALID)
                    .expect("failed to send fake INVALID address removal reason event");
            },
            async move {
                let got = assignment_state_stream(address_state_provider).collect::<Vec<_>>().await;
                assert_matches::assert_matches!(
                    got.as_slice(),
                    &[
                        Ok(got_state),
                        Err(AddressStateProviderError::AddressRemoved(got_reason)),
                    ] => {
                        assert_eq!(got_state, ASSIGNMENT_STATE_ASSIGNED);
                        assert_eq!(got_reason, REMOVAL_REASON_INVALID);
                    }
                );
            },
        )
        .await;
    }

    // Tests that terminal event is observed when using ControlWrapper.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn control_terminal_event() {
        let (control, mut request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fnet_interfaces_admin::ControlMarker>()
                .expect("create proxy");
        let control = super::Control::new(control);
        const EXPECTED_EVENT: fnet_interfaces_admin::InterfaceRemovedReason =
            fnet_interfaces_admin::InterfaceRemovedReason::BadPort;
        const ID: u64 = 15;
        let ((), ()) = futures::future::join(
            async move {
                assert_matches::assert_matches!(control.get_id().await, Ok(ID));
                assert_matches::assert_matches!(
                    control.get_id().await,
                    Err(super::TerminalError::Terminal(got)) if got == EXPECTED_EVENT
                );
            },
            async move {
                let responder = request_stream
                    .try_next()
                    .await
                    .expect("operating request stream")
                    .expect("stream ended unexpectedly")
                    .into_get_id()
                    .expect("unexpected request");
                let () = responder.send(ID).expect("failed to send response");
                let () = request_stream
                    .control_handle()
                    .send_on_interface_removed(EXPECTED_EVENT)
                    .expect("sending terminal event");
            },
        )
        .await;
    }

    // Tests that terminal error is observed when using ControlWrapper if no
    // event is issued.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn control_missing_terminal_event() {
        let (control, mut request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fnet_interfaces_admin::ControlMarker>()
                .expect("create proxy");
        let control = super::Control::new(control);
        let ((), ()) = futures::future::join(
            async move {
                assert_matches::assert_matches!(
                    control.get_id().await,
                    Err(super::TerminalError::Fidl(fidl::Error::ClientChannelClosed {
                        status: zx::Status::PEER_CLOSED,
                        protocol_name: fidl_fuchsia_net_interfaces_admin::ControlMarker::DEBUG_NAME
                    }))
                );
            },
            async move {
                match request_stream
                    .try_next()
                    .await
                    .expect("operating request stream")
                    .expect("stream ended unexpectedly")
                {
                    fnet_interfaces_admin::ControlRequest::GetId { responder } => {
                        // Just close the channel without issuing a response.
                        std::mem::drop(responder);
                    }
                    request => panic!("unexpected request {:?}", request),
                }
            },
        )
        .await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn control_pipelined_error() {
        let (control, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fnet_interfaces_admin::ControlMarker>()
                .expect("create proxy");
        let control = super::Control::new(control);
        const CLOSE_REASON: fnet_interfaces_admin::InterfaceRemovedReason =
            fnet_interfaces_admin::InterfaceRemovedReason::BadPort;
        let () = request_stream
            .control_handle()
            .send_on_interface_removed(CLOSE_REASON)
            .expect("send terminal event");
        std::mem::drop(request_stream);
        assert_matches::assert_matches!(control.or_terminal_event_no_return(Ok(())), Ok(()));
        assert_matches::assert_matches!(
            control
                .or_terminal_event_no_return(Err(fidl::Error::ClientWrite(zx::Status::INTERNAL))),
            Err(super::TerminalError::Fidl(fidl::Error::ClientWrite(zx::Status::INTERNAL)))
        );
        #[cfg(target_os = "fuchsia")]
        assert_matches::assert_matches!(
            control
                .or_terminal_event_no_return(Err(fidl::Error::ClientRead(zx::Status::PEER_CLOSED))),
            Err(super::TerminalError::Terminal(CLOSE_REASON))
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn control_wait_termination() {
        let (control, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fnet_interfaces_admin::ControlMarker>()
                .expect("create proxy");
        let control = super::Control::new(control);
        const CLOSE_REASON: fnet_interfaces_admin::InterfaceRemovedReason =
            fnet_interfaces_admin::InterfaceRemovedReason::BadPort;
        let () = request_stream
            .control_handle()
            .send_on_interface_removed(CLOSE_REASON)
            .expect("send terminal event");
        std::mem::drop(request_stream);
        assert_matches::assert_matches!(
            control.wait_termination().await,
            super::TerminalError::Terminal(CLOSE_REASON)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn control_respond_and_drop() {
        const ID: u64 = 15;
        let (control, mut request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fnet_interfaces_admin::ControlMarker>()
                .expect("create proxy");
        let control = super::Control::new(control);
        let ((), ()) = futures::future::join(
            async move {
                assert_matches::assert_matches!(control.get_id().await, Ok(ID));
            },
            async move {
                let responder = request_stream
                    .try_next()
                    .await
                    .expect("operating request stream")
                    .expect("stream ended unexpectedly")
                    .into_get_id()
                    .expect("unexpected request");
                let () = responder.send(ID).expect("failed to send response");
            },
        )
        .await;
    }
}
