// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::{AsHandleRef, Channel, Event},
    fidl_fidl_rust_test_external::{
        EchoHandleProtocolProxy, EchoHandleProtocolRequest, EchoHandleProtocolRequestStream,
        EchoHandleProtocolSynchronousProxy, PushEventProtocolEvent, PushEventProtocolProxy,
        PushEventProtocolRequestStream, SendHandleProtocolProxy, SendHandleProtocolRequest,
        SendHandleProtocolRequestStream, SendHandleProtocolSynchronousProxy,
    },
    fuchsia_async as fasync,
    fuchsia_async::futures::{future, stream::StreamExt},
    fuchsia_zircon::{ObjectType, Rights, Time},
};

async fn send_handle_receiver_thread(
    server_end: Channel,
    sender_fifo: std::sync::mpsc::SyncSender<()>,
) {
    let async_server_end = fasync::Channel::from_channel(server_end).unwrap();
    let stream = <SendHandleProtocolRequestStream as fidl::endpoints::RequestStream>::from_channel(
        async_server_end,
    );
    Box::new(stream)
        .for_each(|request| {
            match request {
                Ok(SendHandleProtocolRequest::SendHandle { h, control_handle: _ }) => {
                    let info = h.as_handle_ref().basic_info().unwrap();
                    assert_eq!(ObjectType::EVENT, info.object_type);
                    assert_eq!(Rights::TRANSFER, info.rights);
                    sender_fifo.send(()).unwrap();
                }
                Err(_) => panic!("unexpected err"),
            }
            future::ready(())
        })
        .await;
}

#[test]
fn send_handle_sync() {
    let (sender_fifo, receiver_fifo) = std::sync::mpsc::sync_channel(1);
    let (client_end, server_end) = Channel::create().unwrap();
    std::thread::spawn(|| {
        fasync::Executor::new().unwrap().run_singlethreaded(async move {
            send_handle_receiver_thread(server_end, sender_fifo).await;
        });
    });

    let mut proxy = SendHandleProtocolSynchronousProxy::new(client_end);
    let ev = Event::create().unwrap();
    proxy.send_handle(ev).unwrap();
    receiver_fifo.recv().unwrap();
}

#[fasync::run_singlethreaded(test)]
async fn send_handle_async() {
    let (sender_fifo, receiver_fifo) = std::sync::mpsc::sync_channel(1);
    let (client_end, server_end) = Channel::create().unwrap();
    std::thread::spawn(|| {
        fasync::Executor::new().unwrap().run_singlethreaded(async move {
            send_handle_receiver_thread(server_end, sender_fifo).await;
        });
    });

    let async_client_end = fasync::Channel::from_channel(client_end).unwrap();
    let proxy = SendHandleProtocolProxy::new(async_client_end);
    let ev = Event::create().unwrap();
    proxy.send_handle(ev).unwrap();
    receiver_fifo.recv().unwrap();
}

async fn echo_handle_receiver_thread(server_end: Channel) {
    let async_server_end = fasync::Channel::from_channel(server_end).unwrap();
    let stream = <EchoHandleProtocolRequestStream as fidl::endpoints::RequestStream>::from_channel(
        async_server_end,
    );
    stream
        .for_each(|request| {
            match request {
                Ok(EchoHandleProtocolRequest::EchoHandle { h, responder }) => {
                    let info = h.as_handle_ref().basic_info().unwrap();
                    assert_eq!(ObjectType::EVENT, info.object_type);
                    assert_eq!(Rights::TRANSFER | Rights::DUPLICATE, info.rights);
                    responder.send(h).unwrap();
                }
                Err(_) => panic!("unexpected err"),
            }
            future::ready(())
        })
        .await;
}

#[test]
fn echo_handle_sync() {
    let (client_end, server_end) = Channel::create().unwrap();
    std::thread::spawn(|| {
        fasync::Executor::new().unwrap().run_singlethreaded(async move {
            echo_handle_receiver_thread(server_end).await;
        });
    });

    let mut proxy = EchoHandleProtocolSynchronousProxy::new(client_end);
    let ev = Event::create().unwrap();
    let h_response = proxy.echo_handle(ev, Time::INFINITE).unwrap();

    let info = h_response.as_handle_ref().basic_info().unwrap();
    assert_eq!(ObjectType::EVENT, info.object_type);
    assert_eq!(Rights::TRANSFER, info.rights);
}

#[fasync::run_singlethreaded(test)]
async fn echo_handle_async() {
    let (client_end, server_end) = Channel::create().unwrap();
    std::thread::spawn(|| {
        fasync::Executor::new().unwrap().run_singlethreaded(async move {
            echo_handle_receiver_thread(server_end).await;
        });
    });

    let async_client_end = fasync::Channel::from_channel(client_end).unwrap();
    let proxy = EchoHandleProtocolProxy::new(async_client_end);
    let ev = Event::create().unwrap();
    let h_response = proxy.echo_handle(ev).await.unwrap();

    let info = h_response.as_handle_ref().basic_info().unwrap();
    assert_eq!(ObjectType::EVENT, info.object_type);
    assert_eq!(Rights::TRANSFER, info.rights);
}

async fn push_event_receiver_thread(
    receiver_end: Channel,
    sender_fifo: std::sync::mpsc::SyncSender<()>,
) {
    let async_receiver_end = fasync::Channel::from_channel(receiver_end).unwrap();
    let proxy = PushEventProtocolProxy::new(async_receiver_end);
    while let Some(msg) = proxy.take_event_stream().next().await {
        match msg {
            Ok(PushEventProtocolEvent::PushEvent { h }) => {
                let info = h.as_handle_ref().basic_info().unwrap();
                assert_eq!(ObjectType::EVENT, info.object_type);
                assert_eq!(Rights::TRANSFER, info.rights);
                sender_fifo.send(()).unwrap();
            }
            Err(_) => panic!("unexpected err"),
        }
    }
}

#[fasync::run_singlethreaded(test)]
async fn push_event() {
    let (sender_fifo, receiver_fifo) = std::sync::mpsc::sync_channel(1);
    let (sender_chan_end, receiver_chan_end) = Channel::create().unwrap();
    std::thread::spawn(|| {
        fuchsia_async::Executor::new().unwrap().run_singlethreaded(async move {
            push_event_receiver_thread(receiver_chan_end, sender_fifo).await;
        });
    });
    let async_sender_chan_end = fasync::Channel::from_channel(sender_chan_end).unwrap();
    let sender = <PushEventProtocolRequestStream as fidl::endpoints::RequestStream>::from_channel(
        async_sender_chan_end,
    );
    let ev = Event::create().unwrap();
    fidl::endpoints::RequestStream::control_handle(&sender).send_push_event(ev).unwrap();
    receiver_fifo.recv().unwrap();
}
