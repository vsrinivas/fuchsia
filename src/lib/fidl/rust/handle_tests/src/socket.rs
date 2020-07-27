// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::LoggingFixture;
use async_trait::async_trait;
use futures::channel::oneshot;
use futures::future::join;
use futures::lock::Mutex;
use futures::prelude::*;

fn reverse<T>(value: (T, T)) -> (T, T) {
    (value.1, value.0)
}

#[async_trait]
pub trait Fixture: LoggingFixture {
    async fn create_handles(&self, opt: fidl::SocketOpts) -> (fidl::Socket, fidl::Socket);
}

#[derive(Clone, Copy, PartialEq)]
enum AfterSend {
    RemainOpen,
    CloseSender,
}

async fn send_bytes(
    fixture: &Mutex<impl Fixture + 'static>,
    sockets: (fidl::Socket, fidl::Socket),
    out: &'static [u8],
    after_send: AfterSend,
) {
    let mut tx = fidl::AsyncSocket::from_socket(sockets.0).unwrap();
    let mut rx = fidl::AsyncSocket::from_socket(sockets.1).unwrap();
    fixture.lock().await.log(&format!("#    send bytes from {:?} to {:?}: {:?}", tx, rx, out));
    let expect = out.to_vec();
    let (tx_done, rx_done) = oneshot::channel();
    join(
        async move {
            tx.write_all(out).await.unwrap();
            match after_send {
                AfterSend::RemainOpen => {
                    fixture.lock().await.log(&format!("#    waiting for done"));
                    rx_done.await.unwrap()
                }
                AfterSend::CloseSender => drop(tx),
            }
        },
        async move {
            let mut in_bytes = Vec::new();
            let mut buf = [0u8; 1];
            while in_bytes.len() != out.len() {
                rx.read_exact(&mut buf).await.unwrap();
                in_bytes.push(buf[0]);
            }
            assert_eq!(in_bytes, expect);
            let _ = tx_done.send(());
        },
    )
    .await;
}

pub async fn run(fixture: impl Fixture + 'static) {
    let fixture = &Mutex::new(fixture);
    fixture.lock().await.log("# send bytes a->b remaining open");
    let sockets = fixture.lock().await.create_handles(fidl::SocketOpts::STREAM).await;
    send_bytes(&fixture, sockets, &[1, 2, 3], AfterSend::RemainOpen).await;
    fixture.lock().await.log("# send bytes b->a remaining open");
    let sockets = reverse(fixture.lock().await.create_handles(fidl::SocketOpts::STREAM).await);
    send_bytes(&fixture, sockets, &[7, 8, 9], AfterSend::RemainOpen).await;
    fixture.lock().await.log("# send bytes a->b then close");
    let sockets = fixture.lock().await.create_handles(fidl::SocketOpts::STREAM).await;
    send_bytes(&fixture, sockets, &[1, 2, 3], AfterSend::CloseSender).await;
    fixture.lock().await.log("# send bytes b->a then close");
    let sockets = reverse(fixture.lock().await.create_handles(fidl::SocketOpts::STREAM).await);
    send_bytes(&fixture, sockets, &[7, 8, 9], AfterSend::CloseSender).await;
}

struct FidlFixture;

#[async_trait]
impl Fixture for FidlFixture {
    async fn create_handles(&self, opts: fidl::SocketOpts) -> (fidl::Socket, fidl::Socket) {
        fidl::Socket::create(opts).unwrap()
    }
}

impl LoggingFixture for FidlFixture {
    fn log(&mut self, msg: &str) {
        println!("{}", msg);
    }
}

#[cfg(test)]
#[fuchsia_async::run_singlethreaded(test)]
async fn tests() {
    run(FidlFixture).await
}
