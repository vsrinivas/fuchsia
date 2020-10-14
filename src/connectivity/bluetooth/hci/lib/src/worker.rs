// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, AsHandleRef, Duration},
    futures::{
        channel::mpsc::Receiver,
        future::{Fuse, FutureExt},
        select,
        sink::SinkExt,
        stream::StreamExt,
    },
    parking_lot::Mutex,
};

use crate::{
    control_plane::{ControlPlane, Message, Responder},
    log::*,
    snoop::Snoop,
    transport::{HwTransport, IncomingPacket, OutgoingPacket},
};

/// Wrap the open fuchsia_zircon::Channel `in_chan` as an fuchsia_async::Channel in `channel`.
/// Send a response using `responder`.
/// If `channel` is already `Option::Some`, then an error is sent, and the channel is not replaced.
///
/// Returns `true` if `channel` is set by this future and `false` if it is not.
async fn try_open_channel(
    in_chan: zx::Channel,
    channel: &mut Option<fasync::Channel>,
    mut responder: Responder,
) -> bool {
    let status = if channel.is_some() {
        zx::Status::ALREADY_BOUND
    } else {
        match fasync::Channel::from_channel(in_chan) {
            Ok(in_chan) => {
                *channel = Some(in_chan);
                zx::Status::OK
            }
            Err(e) => e.into(),
        }
    };
    responder.send(status).await.unwrap_or_else(log_responder_error);
    status == zx::Status::OK
}

/// A handle that can be used to communicate with the worker task running on a
/// separate thread. Messages should be sent using the `control_plane`.
pub struct WorkerHandle {
    pub control_plane: Mutex<ControlPlane>,
    /// `thread` always starts out as `Some(zx::Thread)` when the thread is spawned and the thread
    /// object is taken out when it is shutdown due to a `WorkerHandle::shutdown` call.
    thread: Option<zx::Thread>,
}

impl WorkerHandle {
    /// Spawn a thread that runs the worker task. Returns a handle that can be
    /// used to communicate with that running task.
    pub fn new(name: impl Into<String>) -> Result<WorkerHandle, zx::Status> {
        let (control_plane, receiver) = ControlPlane::new();
        let (th_tx, th_rx) = std::sync::mpsc::channel();

        let thread_builder = std::thread::Builder::new().name(name.into());
        thread_builder.spawn(move || {
            bt_log_spew!("spawned worker thread");

            // Create and immediately run a closure returning a result for more convenient error
            // propogation.
            let result = move || -> Result<(), anyhow::Error> {
                let handle = fuchsia_runtime::thread_self().duplicate(zx::Rights::SAME_RIGHTS)?;
                th_tx.send(handle)?;
                let mut executor = fasync::Executor::new()?;
                executor.run_singlethreaded(run(receiver));
                Ok(())
            }();

            if let Err(e) = result {
                bt_log_err!("error running worker thread: {}", e);
            };
        })?;

        // Error receiving the worker thread handle indicates that the worker thread died before it
        // could send the handle or that it was never spawned.
        let handle = th_rx.recv().map_err(|_| zx::Status::INTERNAL)?;

        Ok(WorkerHandle { control_plane: Mutex::new(control_plane), thread: Some(handle) })
    }

    pub fn shutdown(self, timeout: Duration) {
        self.control_plane.lock().close();
        if let Some(t) = &self.thread {
            match t.wait_handle(zx::Signals::THREAD_TERMINATED, zx::Time::after(timeout)) {
                Ok(_) => bt_log_info!("child thread has exited"),
                Err(e) => bt_log_info!("wait on child thread termination failed: {}", e),
            }
        } else {
            bt_log_warn!("No handle for child thread available to wait on.");
        }
    }
}

/// Holds the state kept by the main worker task.
pub(crate) struct Worker {
    transport: Box<dyn HwTransport>,
    cmd: Option<fasync::Channel>,
    acl: Option<fasync::Channel>,
    snoop: Snoop,
}

impl Worker {
    fn new(
        transport: Box<dyn HwTransport>,
        cmd: Option<fasync::Channel>,
        acl: Option<fasync::Channel>,
        snoop: Snoop,
    ) -> Self {
        Self { transport, cmd, acl, snoop }
    }

    /// Wait until worker's transport object and cmd channel have been set. This prevents the driver
    /// from reading data from a source without having anywhere for the data to go.
    pub async fn build(control_plane: &mut Receiver<(Message, Responder)>) -> Option<Worker> {
        bt_log_trace!("building read task");
        let mut transport: Option<Box<dyn HwTransport>> = None;
        let mut host_cmd = None;
        let mut host_acl = None;
        let mut snoop = Snoop::default();
        while let Some(msg) = control_plane.next().await {
            match msg {
                (Message::OpenTransport(builder), mut responder) => {
                    if transport.is_some() {
                        bt_log_err!("transport already bound");
                        responder
                            .send(zx::Status::ALREADY_BOUND)
                            .await
                            .unwrap_or_else(log_responder_error);

                        // The OpenTransport message is controlled by the driver rather than as part
                        // of the bt-transport protocol. Therefore, setting it multiple times
                        // indicates an error in the driver's program logic rather than a client-
                        // side issue. Because of this, `build` gives up on trying to build a Worker
                        // object.
                        return None;
                    }
                    match builder.build() {
                        Ok(h) => {
                            responder
                                .send(zx::Status::OK)
                                .await
                                .unwrap_or_else(log_responder_error);
                            if host_cmd.is_some() {
                                return Some(Worker::new(h, host_cmd, host_acl, snoop));
                            } else {
                                transport = Some(h);
                            }
                        }
                        Err(e) => {
                            responder.send(e.into()).await.unwrap_or_else(log_responder_error);
                        }
                    }
                }
                (Message::OpenCmd(c), responder) => {
                    if !try_open_channel(c, &mut host_cmd, responder).await {
                        // Continue to the next loop iteration, if there is no newly opened
                        // command channel.
                        continue;
                    }
                    if let Some(transport) = transport {
                        return Some(Worker::new(transport, host_cmd, host_acl, snoop));
                    }
                }
                (Message::OpenAcl(c), responder) => {
                    try_open_channel(c, &mut host_acl, responder).await;
                }
                (Message::OpenSnoop(c), responder) => {
                    try_open_channel(c, &mut snoop.channel, responder).await;
                }
                (Message::Unbind, mut responder) => {
                    // Close all open resources before responding to Unbind message
                    drop(host_cmd);
                    drop(host_acl);
                    drop(snoop);
                    if let Some(mut t) = transport {
                        t.unbind();
                    }

                    responder.send(zx::Status::OK).await.unwrap_or_else(log_responder_error);

                    return None;
                }
            }
        }
        None
    }
}

/// Main async task that proxies data between the higher layers of the Bluetooth system and the
/// underlying hardware over the Bluetooth Host Controller Interface.
async fn run(mut control_plane: Receiver<(Message, Responder)>) {
    // Declare all local variables needed by this task
    let mut worker: Worker;
    let mut cmd_buf = zx::MessageBuf::new();
    let mut acl_buf = zx::MessageBuf::new();
    let transport_borrow;
    let mut cmd_read;
    let mut acl_read;
    let mut incoming_buffer: Vec<u8> = Vec::with_capacity(0);

    // Get all handles before reading any data.
    // Set up read futures from sockets
    if let Some(w) = Worker::build(&mut control_plane).await {
        worker = w;
        transport_borrow = worker.transport.as_mut();

        cmd_read = if let Some(cmd) = worker.cmd.as_ref() {
            cmd.recv_msg(&mut cmd_buf).fuse()
        } else {
            Fuse::terminated()
        };

        acl_read = if let Some(acl) = worker.acl.as_ref() {
            acl.recv_msg(&mut acl_buf).fuse()
        } else {
            Fuse::terminated()
        };
    } else {
        return;
    }

    let mut unbound = false;

    loop {
        select! {
            msg = control_plane.next() => {
                if let Some(m) = msg {
                    if unbound {
                        let (_, mut responder) = m;
                        responder.send(zx::Status::UNAVAILABLE)
                            .await
                            .unwrap_or_else(log_responder_error);
                        continue;
                    }

                    match m {
                        (Message::OpenTransport(_), mut responder) => {
                            bt_log_warn!("transport already bound");
                            responder.send(zx::Status::ALREADY_BOUND).await
                                .unwrap_or_else(log_responder_error);
                        }
                        (Message::OpenCmd(c), responder) => {
                            cmd_read = Fuse::terminated();
                            if try_open_channel(c, &mut worker.cmd, responder).await {
                                let cmd = worker.cmd.as_ref()
                                    .expect("try_open_channel returning true indicates cmd is Some");
                                cmd_read = cmd.recv_msg(&mut cmd_buf).fuse();
                            }
                        }
                        (Message::OpenAcl(c), responder) => {
                            acl_read = Fuse::terminated();
                            if try_open_channel(c, &mut worker.acl, responder).await {
                                let acl = worker.acl.as_ref()
                                    .expect("try_open_channel returning true indicates acl is Some");
                                acl_read = acl.recv_msg(&mut acl_buf).fuse();
                            }
                        }
                        (Message::OpenSnoop(c), mut responder) => {
                            // Because the snoop channel is not read from, it needs to be polled
                            // here to determine whether it is bound or not. If a read was
                            // performed on the channel, a closed notification would be surfaced at
                            // the point where the read is performed.
                            if worker.snoop.is_bound() {
                                responder.send(zx::Status::ALREADY_BOUND).await
                                    .unwrap_or_else(log_responder_error);
                            } else {
                                try_open_channel(c, &mut worker.snoop.channel, responder).await;
                            }
                        }
                        (Message::Unbind, mut responder) => {
                            // Signal unbind to transport and close all worker resources before
                            // responding.
                            unsafe { transport_borrow.unbind(); }
                            cmd_read = Fuse::terminated();
                            acl_read = Fuse::terminated();
                            worker.cmd.take();
                            worker.acl.take();
                            worker.snoop.channel.take();
                            unbound = true;

                            responder
                                .send(zx::Status::OK)
                                .await
                                .unwrap_or_else(log_responder_error);
                        }
                    }
                } else {
                    // driver has dropped the sender so we should close
                    bt_log_warn!("driver channel closed. read task ending");

                    return;
                }
            }
            res = cmd_read => {
                trace_duration!("Worker::CommandReadOutgoing");

                // End current borrow of cmd by cmd_read
                cmd_read = Fuse::terminated();

                if let Err(status) = res {
                    log_read_error(status, "Command");
                    worker.cmd = None;
                    continue;
                }

                // forward data to the transport
                transport_borrow.send(OutgoingPacket::Cmd(cmd_buf.bytes())).await
                    .expect("Underlying transport driver error");

                // write data to snoop channel
                worker.snoop.write(Snoop::OUTGOING_CMD, cmd_buf.bytes());

                // rearm read future
                cmd_read = worker.cmd.as_ref().expect("cmd must be some in this select branch")
                    .recv_msg(&mut cmd_buf).fuse();
            }
            res = acl_read => {
                trace_duration!("Worker::AclReadOutgoing");

                // End current borrow of acl by acl_read
                acl_read = Fuse::terminated();

                if let Err(status) = res {
                    log_read_error(status, "Acl");
                    worker.acl = None;
                    continue;
                }

                // forward data to the transport
                transport_borrow.send(OutgoingPacket::Acl(acl_buf.bytes())).await
                    .expect("Underlying transport driver error");

                // write data to snoop channel
                worker.snoop.write(Snoop::OUTGOING_ACL, acl_buf.bytes());

                // rearm future
                acl_read = worker.acl.as_ref().expect("acl must be some in this select branch")
                    .recv_msg(&mut acl_buf).fuse();
            }
            res = transport_borrow.next() => {
                trace_duration!("Worker::IncomingPacket");
                match res {
                    Some(token) => {
                        match transport_borrow.take_incoming(token, incoming_buffer) {
                            IncomingPacket::Event(mut data) => {
                                trace_duration!("Worker::EventSendIncoming");
                                incoming_buffer = data;
                                let mut success = true;
                                if let Some(cmd) = worker.cmd.as_ref() {
                                    success = cmd.write(&incoming_buffer, &mut vec![]).is_ok();
                                }
                                if !success {
                                    bt_log_warn!("Failed write to command channel");
                                    cmd_read = Fuse::terminated();
                                    worker.cmd = None;
                                }
                                worker.snoop.write(Snoop::INCOMING_EVT, &incoming_buffer);
                            }
                            IncomingPacket::Acl(mut data) => {
                                trace_duration!("Worker::AclSendIncoming");

                                incoming_buffer = data;
                                let mut success = true;
                                if let Some(acl) = worker.acl.as_ref() {
                                    success = acl.write(&incoming_buffer, &mut vec![]).is_ok();
                                }
                                if !success {
                                    bt_log_warn!("Failed write to acl channel");
                                    acl_read = Fuse::terminated();
                                    worker.acl = None;
                                }
                                worker.snoop.write(Snoop::INCOMING_ACL, &incoming_buffer);
                            }
                        }
                    }
                    None => {
                        // TODO (49096): unbind the driver or attempt to reopen the underlying
                        // driver.
                        bt_log_err!("Error fetching data from underlying transport driver");
                        return;
                    }
                }
            }
        }
    }
}

fn log_read_error(status: zx::Status, channel_name: &'static str) {
    if status == zx::Status::PEER_CLOSED {
        bt_log_info!("{} channel closed", channel_name);
    } else {
        bt_log_info!("Error reading from {} channel {:?} -- closing", channel_name, status);
    }
}

fn log_responder_error<E: std::fmt::Debug>(e: E) {
    bt_log_err!("could not notify main thread of message response: {:?}", e);
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{test_utils::*, transport::IncomingPacket};
    use async_utils::PollExt;
    use futures::future;

    #[fasync::run_until_stalled(test)]
    async fn build_worker_not_yet_complete() {
        let (mut control_plane, mut receiver) = ControlPlane::new();
        let worker = async move {
            Worker::build(&mut receiver).await;
            panic!("worker stopped unexpectedly");
        }
        .fuse();

        // Handle worker requests in the background, panicking if the `Worker::build` future
        // completes.
        fasync::Task::local(worker).detach();

        let (cmd, cmd_) = zx::Channel::create().unwrap();
        assert_eq!(control_plane.async_send(Message::OpenCmd(cmd)).await, zx::Status::OK);
        assert_eq!(
            control_plane.async_send(Message::OpenCmd(cmd_)).await,
            zx::Status::ALREADY_BOUND
        );

        let (snoop, snoop_) = zx::Channel::create().unwrap();
        assert_eq!(control_plane.async_send(Message::OpenSnoop(snoop)).await, zx::Status::OK);
        assert_eq!(
            control_plane.async_send(Message::OpenSnoop(snoop_)).await,
            zx::Status::ALREADY_BOUND
        );

        let (acl, acl_) = zx::Channel::create().unwrap();
        assert_eq!(control_plane.async_send(Message::OpenAcl(acl)).await, zx::Status::OK);
        assert_eq!(
            control_plane.async_send(Message::OpenAcl(acl_)).await,
            zx::Status::ALREADY_BOUND
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn build_worker_not_yet_complete_then_unbind_and_check_host_resources() {
        let (mut control_plane, mut receiver) = ControlPlane::new();
        let worker = async move {
            let res = Worker::build(&mut receiver).await;
            // Worker should never be built in this test since unbind is called before worker
            // finishes building
            assert!(res.is_none());
        }
        .fuse();

        // Handle worker requests in the background.
        fasync::Task::local(worker).detach();

        // Create channels from host to check that all host side resources are cleaned up on unbind
        // Do not create Transport yet so that the worker does not complete the "build" function.
        let (cmd, cmd_) = zx::Channel::create().unwrap();
        assert_eq!(control_plane.async_send(Message::OpenCmd(cmd)).await, zx::Status::OK);
        let (snoop, snoop_) = zx::Channel::create().unwrap();
        assert_eq!(control_plane.async_send(Message::OpenSnoop(snoop)).await, zx::Status::OK);
        let (acl, acl_) = zx::Channel::create().unwrap();
        assert_eq!(control_plane.async_send(Message::OpenAcl(acl)).await, zx::Status::OK);

        // Send an unbind message
        assert_eq!(control_plane.async_send(Message::Unbind).await, zx::Status::OK);

        // All peer channels must be closed here
        assert_eq!(cmd_.write(b"", &mut vec![]), Err(zx::Status::PEER_CLOSED));
        assert_eq!(snoop_.write(b"", &mut vec![]), Err(zx::Status::PEER_CLOSED));
        assert_eq!(acl_.write(b"", &mut vec![]), Err(zx::Status::PEER_CLOSED));
    }

    #[fasync::run_until_stalled(test)]
    async fn build_worker_not_yet_complete_then_unbind_and_check_transport_resources() {
        let (mut control_plane, mut receiver) = ControlPlane::new();
        let worker = async move {
            let res = Worker::build(&mut receiver).await;
            // Worker should never be built in this test since unbind is called before worker
            // finishes building
            assert!(res.is_none());
        }
        .fuse();

        // Handle worker requests in the background.
        fasync::Task::local(worker).detach();

        // Create transport resource to check that it is cleaned up on unbind message
        let (transport, _, _) = TestTransport::new();
        // Future that will complete once `HwTransport::unbind` is called.
        let unbound = transport.unbound.wait_or_dropped();

        assert_eq!(
            control_plane.async_send(Message::OpenTransport(transport)).await,
            zx::Status::OK
        );

        // Send an unbind message
        assert_eq!(control_plane.async_send(Message::Unbind).await, zx::Status::OK);

        // Check that the `HwTransport` received the unbind method call.
        let _ = futures::poll!(unbound)
            .expect("unbound event to be signaled after unbind message is handled");
    }

    #[fasync::run_until_stalled(test)]
    async fn build_worker_drop_control_plane_returns_none() {
        let (mut control_plane, mut receiver) = ControlPlane::new();
        let worker = Worker::build(&mut receiver);

        let tests = async {
            let (transport, _, _) = TestTransport::new();
            assert_eq!(
                control_plane.async_send(Message::OpenTransport(transport)).await,
                zx::Status::OK
            );
            drop(control_plane);
        };

        let (w, _) = future::join(worker, tests).await;
        assert!(w.is_none());
    }

    #[fasync::run_until_stalled(test)]
    async fn build_worker_returns_none() {
        let (mut control_plane, mut receiver) = ControlPlane::new();

        let worker = Worker::build(&mut receiver);

        let tests = async {
            let (transport, _, _) = TestTransport::new();
            assert_eq!(
                control_plane.async_send(Message::OpenTransport(transport)).await,
                zx::Status::OK
            );

            let (transport, _, _) = TestTransport::new();
            assert_eq!(
                control_plane.async_send(Message::OpenTransport(transport)).await,
                zx::Status::ALREADY_BOUND
            );
        };

        let worker = future::join(worker, tests).await;
        assert!(worker.0.is_none());
    }

    #[fasync::run_until_stalled(test)]
    async fn build_worker_success() {
        let (mut control_plane, mut receiver) = ControlPlane::new();
        let worker = Worker::build(&mut receiver);

        let tests = async {
            let (cmd, _cmd) = zx::Channel::create().unwrap();
            assert_eq!(control_plane.async_send(Message::OpenCmd(cmd)).await, zx::Status::OK);

            let (transport, _, _) = TestTransport::new();
            assert_eq!(
                control_plane.async_send(Message::OpenTransport(transport)).await,
                zx::Status::OK
            );
        };

        let (w, _) = future::join(worker, tests).await;
        assert!(w.is_some());
    }

    #[fasync::run_until_stalled(test)]
    async fn run_worker_receive_messages() {
        let (mut control_plane, receiver) = ControlPlane::new();

        fasync::Task::local(run(receiver)).detach();

        let (transport, _in, _out) = TestTransport::new();
        assert_eq!(
            control_plane.async_send(Message::OpenTransport(transport)).await,
            zx::Status::OK
        );

        let (cmd, _cmd) = zx::Channel::create().unwrap();
        assert_eq!(control_plane.async_send(Message::OpenCmd(cmd)).await, zx::Status::OK);

        // At this point we have a Worker object built in the background thread
        let (cmd, _cmd) = zx::Channel::create().unwrap();
        assert_eq!(
            control_plane.async_send(Message::OpenCmd(cmd)).await,
            zx::Status::ALREADY_BOUND
        );

        let (acl, _acl) = zx::Channel::create().unwrap();
        assert_eq!(control_plane.async_send(Message::OpenAcl(acl)).await, zx::Status::OK);

        let (acl, _acl) = zx::Channel::create().unwrap();
        assert_eq!(
            control_plane.async_send(Message::OpenAcl(acl)).await,
            zx::Status::ALREADY_BOUND
        );

        let (snoop, _snoop) = zx::Channel::create().unwrap();
        assert_eq!(control_plane.async_send(Message::OpenSnoop(snoop)).await, zx::Status::OK);

        let (snoop, _snoop) = zx::Channel::create().unwrap();
        assert_eq!(
            control_plane.async_send(Message::OpenSnoop(snoop)).await,
            zx::Status::ALREADY_BOUND
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn run_worker_send_recv_cmd_channel_data() {
        let (mut control_plane, receiver) = ControlPlane::new();
        fasync::Task::local(run(receiver)).detach();

        let (transport, transport_in, mut transport_out) = TestTransport::new();
        assert_eq!(
            control_plane.async_send(Message::OpenTransport(transport)).await,
            zx::Status::OK
        );

        let (cmd, c) = zx::Channel::create().unwrap();
        assert_eq!(control_plane.async_send(Message::OpenCmd(cmd)).await, zx::Status::OK);
        let cmd = fasync::Channel::from_channel(c).unwrap();

        let (snoop, s) = zx::Channel::create().unwrap();
        let snoop_sink = SnoopSink::spawn_from_channel(s);
        assert_eq!(control_plane.async_send(Message::OpenSnoop(snoop)).await, zx::Status::OK);

        let data = vec![1, 2, 3];
        cmd.write(&data, &mut vec![]).unwrap();
        assert_eq!(transport_out.next().await.unwrap(), OwnedOutgoingPacket::Cmd(data));

        let data = vec![4, 5, 6];
        let expected = data.clone();
        let mut buf = zx::MessageBuf::new();
        let cmd_read = cmd.recv_msg(&mut buf);
        transport_in.unbounded_send(IncomingPacket::Event(data)).unwrap();
        cmd_read.await.unwrap();
        assert_eq!(buf.bytes(), &expected[..]);

        // assert snoop reads
        let snoop_output = snoop_sink.data().await;
        assert_eq!(snoop_output.len(), 2);
        assert_eq!(snoop_output[0], vec![0, 1, 2, 3]);
        assert_eq!(snoop_output[1], vec![5, 4, 5, 6]);
    }

    #[fasync::run_until_stalled(test)]
    async fn run_worker_send_recv_acl_channel_data() {
        let (mut control_plane, receiver) = ControlPlane::new();
        fasync::Task::local(run(receiver)).detach();

        let (transport, transport_in, mut transport_out) = TestTransport::new();
        assert_eq!(
            control_plane.async_send(Message::OpenTransport(transport)).await,
            zx::Status::OK
        );

        let (cmd, _c) = zx::Channel::create().unwrap();
        assert_eq!(control_plane.async_send(Message::OpenCmd(cmd)).await, zx::Status::OK);

        let (acl, a) = zx::Channel::create().unwrap();
        assert_eq!(control_plane.async_send(Message::OpenAcl(acl)).await, zx::Status::OK);
        let acl = fasync::Channel::from_channel(a).unwrap();

        let (snoop, s) = zx::Channel::create().unwrap();
        let snoop_sink = SnoopSink::spawn_from_channel(s);
        assert_eq!(control_plane.async_send(Message::OpenSnoop(snoop)).await, zx::Status::OK);

        let data = vec![1, 2, 3];
        acl.write(&data, &mut vec![]).unwrap();
        assert_eq!(transport_out.next().await.unwrap(), OwnedOutgoingPacket::Acl(data));

        let data = vec![4, 5, 6];
        let expected = data.clone();
        let mut buf = zx::MessageBuf::new();
        let acl_read = acl.recv_msg(&mut buf);
        transport_in.unbounded_send(IncomingPacket::Acl(data)).unwrap();
        acl_read.await.unwrap();
        assert_eq!(buf.bytes(), &expected[..]);

        // assert snoop reads
        let snoop_output = snoop_sink.data().await;
        assert_eq!(snoop_output.len(), 2);
        assert_eq!(snoop_output[0], vec![2, 1, 2, 3]);
        assert_eq!(snoop_output[1], vec![6, 4, 5, 6]);
    }

    #[fasync::run_until_stalled(test)]
    async fn worker_unbind_then_resources_are_closed() {
        let (mut control_plane, receiver) = ControlPlane::new();
        fasync::Task::local(run(receiver)).detach();

        // Create transport resource to check that it is cleaned up on unbind message
        let (transport, _transport_in, _transport_out) = TestTransport::new();
        // Future that will complete once `HwTransport::unbind` is called.
        let unbound = transport.unbound.wait();

        assert_eq!(
            control_plane.async_send(Message::OpenTransport(transport)).await,
            zx::Status::OK
        );

        // Create channels from host to check that all host side resources are cleaned up on unbind
        let (cmd, cmd_) = zx::Channel::create().unwrap();
        assert_eq!(control_plane.async_send(Message::OpenCmd(cmd)).await, zx::Status::OK);
        let (snoop, snoop_) = zx::Channel::create().unwrap();
        assert_eq!(control_plane.async_send(Message::OpenSnoop(snoop)).await, zx::Status::OK);
        let (acl, acl_) = zx::Channel::create().unwrap();
        assert_eq!(control_plane.async_send(Message::OpenAcl(acl)).await, zx::Status::OK);

        // Send an unbind message
        assert_eq!(control_plane.async_send(Message::Unbind).await, zx::Status::OK);

        // All peer channels must be closed here
        assert_eq!(cmd_.write(b"", &mut vec![]), Err(zx::Status::PEER_CLOSED));
        assert_eq!(snoop_.write(b"", &mut vec![]), Err(zx::Status::PEER_CLOSED));
        assert_eq!(acl_.write(b"", &mut vec![]), Err(zx::Status::PEER_CLOSED));

        // Check that the `HwTransport` received the unbind method call.
        let _ = futures::poll!(unbound)
            .expect("unbound event to be signaled after unbind message is handled");
    }
}
