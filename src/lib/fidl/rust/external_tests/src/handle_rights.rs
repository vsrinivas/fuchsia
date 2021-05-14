// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file tests handle rights checking end-to-end for all variations that
// show up in the generated code. These checks are verified to be happening
// both on the send-side (before the handle is written to channel) and
// receive-side (after the handle is read from the channel).

// In order to accomplish this, several clones of the same protocol method
// were created in handle_rights.test.fidl, each differing only by its
// handle rights. An OrdinalTransformChannel intercepts the messages between
// two handles and replaces the method ordinal, so that the "wrong" FIDL method
// can be called, triggering specific rights checks.

use {
    fidl::{endpoints::ServerEnd, AsHandleRef, Channel, Event},
    fidl_fidl_rust_test_external::{
        EchoHandleProtocolMarker, EchoHandleProtocolProxy, EchoHandleProtocolRequest,
        EchoHandleProtocolSynchronousProxy, ErrorSyntaxProtocolMarker, ErrorSyntaxProtocolProxy,
        ErrorSyntaxProtocolRequest, ErrorSyntaxProtocolSynchronousProxy,
        PushEventProtocolControlHandle, PushEventProtocolEvent, PushEventProtocolMarker,
        PushEventProtocolProxy, SendHandleProtocolMarker, SendHandleProtocolProxy,
        SendHandleProtocolRequest, SendHandleProtocolSynchronousProxy,
    },
    fuchsia_async as fasync,
    fuchsia_async::futures::{future, stream::StreamExt},
    fuchsia_zircon::{Handle, ObjectType, Rights, Signals, Time},
    std::convert::TryInto,
    std::io::prelude::*,
};

const SEND_HANDLE_REDUCED_RIGHTS_ORDINAL: u64 = 0x512ec95c01c56f7b;
const SEND_HANDLE_SAME_RIGHTS_ORDINAL: u64 = 0x2dc4c33fd1a11b37;

const ECHO_HANDLE_REQUEST_REDUCED_RIGHTS_ORDINAL: u64 = 0x739e45ebdf6ad12b;
const ECHO_HANDLE_REQUEST_SAME_RIGHTS_ORDINAL: u64 = 0xa2095fc3413e815;

const ECHO_HANDLE_RESPONSE_REDUCED_RIGHTS_ORDINAL: u64 = 0x6d976f877db1bb8f;
const ECHO_HANDLE_RESPONSE_SAME_RIGHTS_ORDINAL: u64 = 0x2958e01fc423982;

const PUSH_EVENT_REDUCED_RIGHTS_ORDINAL: u64 = 0x3b8aa447b0d3514c;
const PUSH_EVENT_SAME_RIGHTS_ORDINAL: u64 = 0x33824de8c5cc3490;

// A channel where the data may be transformed during transit.
trait TransformableChannel {
    fn take_client_end(&mut self) -> Channel;
    fn take_server_end(&mut self) -> Channel;
    // Wait for a message sent from the client to the server to arrive and apply a transform on the
    // data.
    fn transform(&mut self);
    // Wait for a message sent from the server to the client to arrive and apply a transform on the
    // data.
    fn reversed_transform(&mut self);
}

// A no-op implementation of TransformableChannel, used for end-to-end tests
// where it is undesirable to have a transform.
struct NoTransformChannel {
    client_end: Channel,
    server_end: Channel,
}

impl NoTransformChannel {
    fn new() -> NoTransformChannel {
        let (client_end, server_end) = Channel::create().unwrap();
        NoTransformChannel { client_end, server_end }
    }
}

impl TransformableChannel for NoTransformChannel {
    fn take_client_end(&mut self) -> Channel {
        std::mem::replace(&mut self.client_end, Handle::invalid().into())
    }
    fn take_server_end(&mut self) -> Channel {
        std::mem::replace(&mut self.server_end, Handle::invalid().into())
    }
    fn transform(&mut self) {}
    fn reversed_transform(&mut self) {}
}

// A TransformableChannel that during transform:
// 1. Asserts that the received ordinal is the expected input ordinal
// 2. Replaces the received ordinal with the desired output ordinal
//
// The reverse transform applies these steps in the opposite direction.
struct OrdinalTransformChannel {
    client_end: Channel,
    server_end: Channel,

    proxy_client_end: Channel,
    proxy_server_end: Channel,
    client_end_ordinal: u64,
    server_end_ordinal: u64,
}

impl OrdinalTransformChannel {
    fn new(client_end_ordinal: u64, server_end_ordinal: u64) -> OrdinalTransformChannel {
        let (client_end, proxy_client_end) = Channel::create().unwrap();
        let (proxy_server_end, server_end) = Channel::create().unwrap();
        OrdinalTransformChannel {
            client_end,
            server_end,
            proxy_client_end,
            proxy_server_end,
            client_end_ordinal,
            server_end_ordinal,
        }
    }
    fn transform_impl(
        in_end: &mut Channel,
        in_ordinal: u64,
        out_end: &mut Channel,
        out_ordinal: u64,
    ) {
        let signals = in_end
            .as_handle_ref()
            .wait(Signals::CHANNEL_READABLE | Signals::CHANNEL_PEER_CLOSED, Time::INFINITE)
            .unwrap();
        assert!(signals.contains(Signals::CHANNEL_READABLE));

        let mut bytes = Vec::<u8>::new();
        let mut handles = Vec::<Handle>::new();
        in_end.read_split(&mut bytes, &mut handles).unwrap();

        let existing_ordinal = u64::from_le_bytes(bytes[8..16].try_into().unwrap());
        assert_eq!(in_ordinal, existing_ordinal);
        (&mut bytes[8..16]).write(&out_ordinal.to_le_bytes()).unwrap();

        out_end.write(&bytes, &mut handles).unwrap();
    }
}

impl TransformableChannel for OrdinalTransformChannel {
    fn take_client_end(&mut self) -> Channel {
        std::mem::replace(&mut self.client_end, Handle::invalid().into())
    }
    fn take_server_end(&mut self) -> Channel {
        std::mem::replace(&mut self.server_end, Handle::invalid().into())
    }
    fn transform(&mut self) {
        Self::transform_impl(
            &mut self.proxy_client_end,
            self.client_end_ordinal,
            &mut self.proxy_server_end,
            self.server_end_ordinal,
        );
    }
    fn reversed_transform(&mut self) {
        Self::transform_impl(
            &mut self.proxy_server_end,
            self.server_end_ordinal,
            &mut self.proxy_client_end,
            self.client_end_ordinal,
        );
    }
}

async fn send_handle_receiver_thread(
    server_end: Channel,
    sender_fifo: std::sync::mpsc::SyncSender<()>,
) {
    let stream = ServerEnd::<SendHandleProtocolMarker>::new(server_end).into_stream().unwrap();
    Box::new(stream)
        .for_each(|request| {
            match request {
                Ok(SendHandleProtocolRequest::SendHandleReducedRights { h, control_handle: _ })
                | Ok(SendHandleProtocolRequest::SendHandleSameRights { h, control_handle: _ }) => {
                    let info = h.as_handle_ref().basic_info().unwrap();
                    assert_eq!(ObjectType::EVENT, info.object_type);
                    assert_eq!(Rights::TRANSFER, info.rights);
                    sender_fifo.send(()).unwrap();
                }
                Err(err) => panic!("unexpected err: {}", err),
            }
            future::ready(())
        })
        .await;
}

fn send_handle_sync_helper<'a>(
    send_fn: fn(&SendHandleProtocolSynchronousProxy, fidl::Event) -> Result<(), fidl::Error>,
    transformable_channel: &'a mut (dyn TransformableChannel + Send),
) {
    let (sender_fifo, receiver_fifo) = std::sync::mpsc::sync_channel(1);
    let server_end = transformable_channel.take_server_end();
    std::thread::spawn(|| {
        fasync::LocalExecutor::new().unwrap().run_singlethreaded(async move {
            send_handle_receiver_thread(server_end, sender_fifo).await;
        });
    });

    let mut proxy =
        SendHandleProtocolSynchronousProxy::new(transformable_channel.take_client_end());
    let ev = Event::create().unwrap();
    send_fn(&mut proxy, ev).unwrap();
    transformable_channel.transform();
    receiver_fifo.recv().unwrap();
}

#[test]
fn send_handle_sync_end_to_end() {
    // Test end-to-end rights checking with no transformation of ordinals.
    send_handle_sync_helper(
        SendHandleProtocolSynchronousProxy::send_handle_reduced_rights,
        &mut NoTransformChannel::new(),
    )
}

#[test]
#[ignore] // TODO(fxbug.dev/74939) Remove the ignore
fn send_handle_sync_send() {
    // - The client creates an event with same rights.
    // - In zx_channel_write_etc, the rights are reduced to zx.rights.TRANSFER (specified by FIDL
    //   in the handle disposition).
    // - The ordinal is changed in the message bytes to fool the server into expecting
    //   SendHandleSameRights. Therefore we know the server had no part in reducing rights.
    send_handle_sync_helper(
        SendHandleProtocolSynchronousProxy::send_handle_reduced_rights,
        &mut OrdinalTransformChannel::new(
            SEND_HANDLE_REDUCED_RIGHTS_ORDINAL,
            SEND_HANDLE_SAME_RIGHTS_ORDINAL,
        ),
    )
}

#[test]
fn send_handle_sync_receive() {
    // - The client creates an event with same rights.
    // - zx_channel_write_etc doesn't change the rights (FIDL tells it to use same rights).
    // - The ordinal is changed in the message bytes to fool the server into expecting
    //   SendHandleReducedRights. This triggers a rights check on the receiving side.
    send_handle_sync_helper(
        SendHandleProtocolSynchronousProxy::send_handle_same_rights,
        &mut OrdinalTransformChannel::new(
            SEND_HANDLE_SAME_RIGHTS_ORDINAL,
            SEND_HANDLE_REDUCED_RIGHTS_ORDINAL,
        ),
    )
}

async fn send_handle_async_helper<'a>(
    send_fn: fn(&SendHandleProtocolProxy, fidl::Event) -> Result<(), fidl::Error>,
    transformable_channel: &'a mut (dyn TransformableChannel + Send),
) {
    let (sender_fifo, receiver_fifo) = std::sync::mpsc::sync_channel(1);
    let server_end = transformable_channel.take_server_end();
    std::thread::spawn(|| {
        fasync::LocalExecutor::new().unwrap().run_singlethreaded(async move {
            send_handle_receiver_thread(server_end, sender_fifo).await;
        });
    });

    let async_client_end =
        fasync::Channel::from_channel(transformable_channel.take_client_end()).unwrap();
    let proxy = SendHandleProtocolProxy::new(async_client_end);
    let ev = Event::create().unwrap();
    send_fn(&proxy, ev).unwrap();
    transformable_channel.transform();
    receiver_fifo.recv().unwrap();
}

#[fasync::run_singlethreaded(test)]
async fn send_handle_async_end_to_end() {
    // Test end-to-end rights checking with no transformation of ordinals.
    send_handle_async_helper(
        SendHandleProtocolProxy::send_handle_reduced_rights,
        &mut NoTransformChannel::new(),
    )
    .await
}

#[fasync::run_singlethreaded(test)]
#[ignore] // TODO(fxbug.dev/74939) Remove the ignore
async fn send_handle_async_send() {
    // See comment on sync version for details.
    send_handle_async_helper(
        SendHandleProtocolProxy::send_handle_reduced_rights,
        &mut OrdinalTransformChannel::new(
            SEND_HANDLE_REDUCED_RIGHTS_ORDINAL,
            SEND_HANDLE_SAME_RIGHTS_ORDINAL,
        ),
    )
    .await
}

#[fasync::run_singlethreaded(test)]
async fn send_handle_async_receive() {
    // See comment on sync version for details.
    send_handle_async_helper(
        SendHandleProtocolProxy::send_handle_same_rights,
        &mut OrdinalTransformChannel::new(
            SEND_HANDLE_SAME_RIGHTS_ORDINAL,
            SEND_HANDLE_REDUCED_RIGHTS_ORDINAL,
        ),
    )
    .await
}

async fn echo_handle_receiver_thread(server_end: Channel) {
    let stream = ServerEnd::<EchoHandleProtocolMarker>::new(server_end).into_stream().unwrap();
    stream
        .for_each(|request| {
            match request {
                Ok(EchoHandleProtocolRequest::EchoHandleRequestResponseReducedRights {
                    h,
                    responder,
                }) => {
                    let info = h.as_handle_ref().basic_info().unwrap();
                    assert_eq!(ObjectType::EVENT, info.object_type);
                    assert_eq!(Rights::TRANSFER | Rights::DUPLICATE, info.rights);
                    responder.send(h).unwrap();
                }
                Ok(EchoHandleProtocolRequest::EchoHandleRequestReducedRights { h, responder }) => {
                    let info = h.as_handle_ref().basic_info().unwrap();
                    assert_eq!(ObjectType::EVENT, info.object_type);
                    assert_eq!(Rights::TRANSFER, info.rights);
                    responder.send(h).unwrap();
                }
                Ok(EchoHandleProtocolRequest::EchoHandleRequestSameRights { h, responder }) => {
                    let info = h.as_handle_ref().basic_info().unwrap();
                    assert_eq!(ObjectType::EVENT, info.object_type);
                    assert_eq!(Rights::TRANSFER, info.rights);
                    responder.send(h).unwrap();
                }
                Ok(EchoHandleProtocolRequest::EchoHandleResponseReducedRights { h, responder }) => {
                    responder.send(h).unwrap();
                }
                Ok(EchoHandleProtocolRequest::EchoHandleResponseSameRights { h, responder }) => {
                    responder.send(h).unwrap();
                }
                Err(_) => panic!("unexpected err"),
            }
            future::ready(())
        })
        .await;
}

fn echo_handle_sync_helper<'a>(
    send_fn: fn(
        &EchoHandleProtocolSynchronousProxy,
        fidl::Event,
        Time,
    ) -> Result<fidl::Event, fidl::Error>,
    mut transformable_channel: Box<dyn TransformableChannel + Send>,
) {
    let server_end = transformable_channel.take_server_end();
    std::thread::spawn(|| {
        fasync::LocalExecutor::new().unwrap().run_singlethreaded(async move {
            echo_handle_receiver_thread(server_end).await;
        });
    });

    let mut proxy =
        EchoHandleProtocolSynchronousProxy::new(transformable_channel.take_client_end());
    let th = std::thread::spawn(move || {
        transformable_channel.transform();
        transformable_channel.reversed_transform();
    });
    let ev = Event::create().unwrap();
    let h_response = send_fn(&mut proxy, ev, Time::INFINITE).unwrap();

    let info = h_response.as_handle_ref().basic_info().unwrap();
    assert_eq!(ObjectType::EVENT, info.object_type);
    assert_eq!(Rights::TRANSFER, info.rights);

    th.join().unwrap();
}

#[test]
fn echo_handle_sync_end_to_end() {
    echo_handle_sync_helper(
        EchoHandleProtocolSynchronousProxy::echo_handle_request_response_reduced_rights,
        Box::new(NoTransformChannel::new()),
    );
}

#[test]
#[ignore] // TODO(fxbug.dev/74939) Remove the ignore
fn echo_handle_sync_request_send() {
    // - The client creates an event with same rights.
    // - In zx_channel_write_etc, the rights are reduced to zx.rights.TRANSFER (specified by FIDL
    //   in the handle disposition).
    // - The ordinal is changed in the message bytes to fool the server into expecting
    //   SendHandleSameRights. Therefore we know the server had no part in reducing rights.
    // - The rights are checked on the server.
    // - In the response direction, the ordinal flip happens in reverse and the final rights
    //   should be the same as those received by the server.
    echo_handle_sync_helper(
        EchoHandleProtocolSynchronousProxy::echo_handle_request_reduced_rights,
        Box::new(OrdinalTransformChannel::new(
            ECHO_HANDLE_REQUEST_REDUCED_RIGHTS_ORDINAL,
            ECHO_HANDLE_REQUEST_SAME_RIGHTS_ORDINAL,
        )),
    );
}

#[test]
fn echo_handle_sync_request_receive() {
    // - The client creates an event with same rights.
    // - zx_channel_write_etc doesn't change the rights (FIDL tells it to use same rights).
    // - The ordinal is changed in the message bytes to fool the server into expecting
    //   SendHandleReducedRights. This triggers a rights check on the receiving side.
    // - In the response direction, the ordinal flip happens in reverse and the final rights
    //   should be the same as those received by the server.
    echo_handle_sync_helper(
        EchoHandleProtocolSynchronousProxy::echo_handle_request_same_rights,
        Box::new(OrdinalTransformChannel::new(
            ECHO_HANDLE_REQUEST_SAME_RIGHTS_ORDINAL,
            ECHO_HANDLE_REQUEST_REDUCED_RIGHTS_ORDINAL,
        )),
    );
}

#[test]
#[ignore] // TODO(fxbug.dev/74939) Remove the ignore
fn echo_handle_sync_response_send() {
    // - The client creates an event with same rights and sends it to the server.
    // - The message ordinal is changed in transit from EchoHandleResponseSameRights to
    //   EchoHandleResponseReducedRights, but this doesn't change the rights.
    // - While responding, zx_channel_write_etc will reduce the handle rights based on the
    //   rights that FIDL provides in the handle disposition.
    // - The ordinal will be transformed so the response is received as a
    //   EchoHandleResponseSameRights, skipping any rights reduction on the receiving
    //   side.
    // - The output rights will be checked.
    echo_handle_sync_helper(
        EchoHandleProtocolSynchronousProxy::echo_handle_response_same_rights,
        Box::new(OrdinalTransformChannel::new(
            ECHO_HANDLE_RESPONSE_SAME_RIGHTS_ORDINAL,
            ECHO_HANDLE_RESPONSE_REDUCED_RIGHTS_ORDINAL,
        )),
    );
}

#[test]
fn echo_handle_sync_response_receive() {
    // - The client creates an event with same rights and sends it to the server.
    // - The message ordinal is changed in transit from EchoHandleResponseReduceRights to
    //   EchoHandleResponseSameRights, but this doesn't change the rights.
    // - While responding, zx_channel_write_etc will not change the handle rights.
    // - The ordinal will be transformed so the response is received as a
    //   EchoHandleResponseReduceRights, triggering the rights to be reduced when the
    //   message is received.
    // - The output rights will be checked.
    echo_handle_sync_helper(
        EchoHandleProtocolSynchronousProxy::echo_handle_response_reduced_rights,
        Box::new(OrdinalTransformChannel::new(
            ECHO_HANDLE_RESPONSE_REDUCED_RIGHTS_ORDINAL,
            ECHO_HANDLE_RESPONSE_SAME_RIGHTS_ORDINAL,
        )),
    );
}

async fn echo_handle_async_helper(
    send_fn: fn(
        &EchoHandleProtocolProxy,
        fidl::Event,
    ) -> fidl::client::QueryResponseFut<fidl::Event>,
    mut transformable_channel: Box<dyn TransformableChannel + Send>,
) {
    let server_end = transformable_channel.take_server_end();
    std::thread::spawn(|| {
        fasync::LocalExecutor::new().unwrap().run_singlethreaded(async move {
            echo_handle_receiver_thread(server_end).await;
        });
    });

    let async_client_end =
        fasync::Channel::from_channel(transformable_channel.take_client_end()).unwrap();
    let th = std::thread::spawn(move || {
        transformable_channel.transform();
        transformable_channel.reversed_transform();
    });
    let proxy = EchoHandleProtocolProxy::new(async_client_end);
    let ev = Event::create().unwrap();
    let h_response = send_fn(&proxy, ev).await.unwrap();

    let info = h_response.as_handle_ref().basic_info().unwrap();
    assert_eq!(ObjectType::EVENT, info.object_type);
    assert_eq!(Rights::TRANSFER, info.rights);

    th.join().unwrap();
}

#[fasync::run_singlethreaded(test)]
async fn echo_handle_async_end_to_end() {
    echo_handle_async_helper(
        EchoHandleProtocolProxy::echo_handle_request_response_reduced_rights,
        Box::new(NoTransformChannel::new()),
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
#[ignore] // TODO(fxbug.dev/74939) Remove the ignore
async fn echo_handle_async_request_send() {
    // See comment on sync version for details.
    echo_handle_async_helper(
        EchoHandleProtocolProxy::echo_handle_request_reduced_rights,
        Box::new(OrdinalTransformChannel::new(
            ECHO_HANDLE_REQUEST_REDUCED_RIGHTS_ORDINAL,
            ECHO_HANDLE_REQUEST_SAME_RIGHTS_ORDINAL,
        )),
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn echo_handle_async_request_receive() {
    // See comment on sync version for details.
    echo_handle_async_helper(
        EchoHandleProtocolProxy::echo_handle_request_same_rights,
        Box::new(OrdinalTransformChannel::new(
            ECHO_HANDLE_REQUEST_SAME_RIGHTS_ORDINAL,
            ECHO_HANDLE_REQUEST_REDUCED_RIGHTS_ORDINAL,
        )),
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
#[ignore] // TODO(fxbug.dev/74939) Remove the ignore
async fn echo_handle_async_response_send() {
    // See comment on sync version for details.
    echo_handle_async_helper(
        EchoHandleProtocolProxy::echo_handle_response_same_rights,
        Box::new(OrdinalTransformChannel::new(
            ECHO_HANDLE_RESPONSE_SAME_RIGHTS_ORDINAL,
            ECHO_HANDLE_RESPONSE_REDUCED_RIGHTS_ORDINAL,
        )),
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn echo_handle_async_response_receive() {
    // See comment on sync version for details.
    echo_handle_async_helper(
        EchoHandleProtocolProxy::echo_handle_response_reduced_rights,
        Box::new(OrdinalTransformChannel::new(
            ECHO_HANDLE_RESPONSE_REDUCED_RIGHTS_ORDINAL,
            ECHO_HANDLE_RESPONSE_SAME_RIGHTS_ORDINAL,
        )),
    )
    .await;
}

async fn push_event_receiver_thread(
    receiver_end: Channel,
    sender_fifo: std::sync::mpsc::SyncSender<()>,
) {
    let async_receiver_end = fasync::Channel::from_channel(receiver_end).unwrap();
    let proxy = PushEventProtocolProxy::new(async_receiver_end);
    while let Some(msg) = proxy.take_event_stream().next().await {
        match msg {
            Ok(PushEventProtocolEvent::PushEventReducedRights { h })
            | Ok(PushEventProtocolEvent::PushEventSameRights { h }) => {
                let info = h.as_handle_ref().basic_info().unwrap();
                assert_eq!(ObjectType::EVENT, info.object_type);
                assert_eq!(Rights::TRANSFER, info.rights);
                sender_fifo.send(()).unwrap();
            }
            Err(err) => panic!("unexpected err: {}", err),
        }
    }
}

async fn push_event_helper<'a>(
    send_fn: fn(&PushEventProtocolControlHandle, fidl::Event) -> Result<(), fidl::Error>,
    transformable_channel: &'a mut (dyn TransformableChannel + Send),
) {
    let (sender_fifo, receiver_fifo) = std::sync::mpsc::sync_channel(1);
    let receiver_chan_end = transformable_channel.take_server_end();
    std::thread::spawn(|| {
        fuchsia_async::LocalExecutor::new().unwrap().run_singlethreaded(async move {
            push_event_receiver_thread(receiver_chan_end, sender_fifo).await;
        });
    });
    let stream = ServerEnd::<PushEventProtocolMarker>::new(transformable_channel.take_client_end())
        .into_stream()
        .unwrap();
    let ev = Event::create().unwrap();
    send_fn(&fidl::endpoints::RequestStream::control_handle(&stream), ev).unwrap();
    transformable_channel.transform();
    receiver_fifo.recv().unwrap();
}

#[fasync::run_singlethreaded(test)]
async fn push_event_send_and_receive() {
    push_event_helper(
        PushEventProtocolControlHandle::send_push_event_reduced_rights,
        &mut NoTransformChannel::new(),
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
#[ignore] // TODO(fxbug.dev/74939) Remove the ignore
async fn push_event_send() {
    // Sends an event
    // - On the sending side, the PushEventReducedRights event is used in which
    //   FIDL generates handle dispositions with only the TRANSFER right that are
    //   then provided to zx_channel_write_etc which will reduce the rights of the
    //   event.
    // - The event ordinal is changed to PushEventSameRights.
    // - The message is received with no further handle rights changes.
    push_event_helper(
        PushEventProtocolControlHandle::send_push_event_reduced_rights,
        &mut OrdinalTransformChannel::new(
            PUSH_EVENT_REDUCED_RIGHTS_ORDINAL,
            PUSH_EVENT_SAME_RIGHTS_ORDINAL,
        ),
    )
    .await;
}

#[fasync::run_singlethreaded(test)]
async fn push_event_receive() {
    // Sends an event
    // - On the sending side, the PushEventSameRights event is used which does
    //   not change the handle rights.
    // - The event ordinal is changed to PushEventReducedRights.
    // - During decode on the receiving end, the rights are reduced to just
    //   TRANSFER.
    push_event_helper(
        PushEventProtocolControlHandle::send_push_event_same_rights,
        &mut OrdinalTransformChannel::new(
            PUSH_EVENT_SAME_RIGHTS_ORDINAL,
            PUSH_EVENT_REDUCED_RIGHTS_ORDINAL,
        ),
    )
    .await;
}

async fn error_syntax_receiver_thread(server_end: Channel) {
    let stream = ServerEnd::<ErrorSyntaxProtocolMarker>::new(server_end).into_stream().unwrap();
    stream
        .for_each(|request| {
            match request {
                Ok(ErrorSyntaxProtocolRequest::TestErrorSyntax { responder }) => {
                    let h = Event::create().unwrap();
                    responder.send(&mut Ok(h)).unwrap();
                }
                Err(_) => panic!("unexpected err"),
            }
            future::ready(())
        })
        .await;
}

#[test]
fn error_syntax_sync_end_to_end() {
    let (client_end, server_end) = Channel::create().unwrap();
    std::thread::spawn(|| {
        fasync::LocalExecutor::new().unwrap().run_singlethreaded(async move {
            error_syntax_receiver_thread(server_end).await;
        });
    });

    let proxy = ErrorSyntaxProtocolSynchronousProxy::new(client_end);
    let h_response = proxy.test_error_syntax(Time::INFINITE).unwrap();

    let info = h_response.unwrap().as_handle_ref().basic_info().unwrap();
    assert_eq!(ObjectType::EVENT, info.object_type);
    assert_eq!(Rights::TRANSFER, info.rights);
}

#[fasync::run_singlethreaded(test)]
async fn error_syntax_async_end_to_end() {
    let (client_end, server_end) = Channel::create().unwrap();
    std::thread::spawn(|| {
        fasync::LocalExecutor::new().unwrap().run_singlethreaded(async move {
            error_syntax_receiver_thread(server_end).await;
        });
    });

    let async_client_end = fasync::Channel::from_channel(client_end).unwrap();
    let proxy = ErrorSyntaxProtocolProxy::new(async_client_end);
    let h_response = proxy.test_error_syntax().await.unwrap();

    let info = h_response.unwrap().as_handle_ref().basic_info().unwrap();
    assert_eq!(ObjectType::EVENT, info.object_type);
    assert_eq!(Rights::TRANSFER, info.rights);
}
