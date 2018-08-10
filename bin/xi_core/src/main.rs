// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Main process for Fuchsia builds - uses fidl rather than stdin / stdout

#![deny(warnings)]

extern crate xi_core_lib;
extern crate xi_rpc;

extern crate fuchsia_zircon as zx;
extern crate mxruntime;
extern crate fidl;
extern crate fidl_fuchsia_xi;
extern crate byteorder;
extern crate failure;
extern crate futures;
extern crate fuchsia_app as component;
extern crate fuchsia_async as async;

use fidl::endpoints2::ServiceMarker;
use std::io;
use std::sync::Arc;
use std::thread;

use failure::{Error, ResultExt};
use futures::{TryFutureExt, future};

use fidl_fuchsia_xi::{Json, JsonImpl, JsonMarker};

use component::server::ServicesServer;

use zx::{AsHandleRef, Signals, Socket, Status, Time};

use xi_rpc::RpcLoop;
use xi_core_lib::XiCore;

// TODO: this should be moved into fuchsia_zircon.
pub struct BlockingSocket(Arc<Socket>);

impl io::Read for BlockingSocket {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        let wait_sigs = Signals::SOCKET_READABLE | Signals::SOCKET_PEER_CLOSED;
        let signals = self.0.wait_handle(wait_sigs, Time::INFINITE)?;
        if signals.contains(Signals::SOCKET_PEER_CLOSED) {
            return Ok(0);
        }
        self.0.read(buf).or_else(|status|
            if status == Status::PEER_CLOSED {
                Ok(0)
            } else {
                Err(status.into())
            }
        )
    }
}

impl io::Write for BlockingSocket {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        self.0.write(buf).map_err(Into::into)
        // TODO: handle case where socket is full (wait and retry)
    }

    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

fn editor_main(sock: Socket) {
    eprintln!("editor_main");
    let mut state = XiCore::new();
    let arc_sock = Arc::new(sock);
    let my_in = io::BufReader::new(BlockingSocket(arc_sock.clone()));
    let my_out = BlockingSocket(arc_sock);
    let mut rpc_looper = RpcLoop::new(my_out);

    let _ = rpc_looper.mainloop(|| my_in, &mut state);
}

fn spawn_json_server(chan: async::Channel) {
    async::spawn(
    JsonImpl {
        state: (),
        on_open: |_, _| future::ready(()),
        connect_socket: |_state, socket, _controller| {
            eprintln!("connect_socket");
            let _ = thread::spawn(move || editor_main(socket));
            future::ready(())
        }
    }
    .serve(chan)
    .unwrap_or_else(|e| eprintln!("error running xi Json server {:?}", e)))
}

fn main() {
    if let Err(e) = main_xi() {
        eprintln!("xi-core: Error: {:?}", e);
    }
}

fn main_xi() -> Result<(), Error> {
    let mut executor = async::Executor::new().context("unable to create executor")?;

    let server = ServicesServer::new()
        .add_service((JsonMarker::NAME, spawn_json_server))
        .start()
        .map_err(|e| e.context("error starting service server"))?;

    let n_threads = 2;
    executor.run(server, n_threads)
}
