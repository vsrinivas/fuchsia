// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon_status as zx_status;

fn reverse<T>(value: (T, T)) -> (T, T) {
    (value.1, value.0)
}

pub trait Fixture {
    fn create_handles(&self, opt: fidl::SocketOpts) -> (fidl::Socket, fidl::Socket);
}

#[derive(Clone, Copy, PartialEq)]
enum AfterSend {
    RemainOpen,
    CloseSender,
}

fn send_bytes(
    sockets: (fidl::Socket, fidl::Socket),
    mut out: &'static [u8],
    after_send: AfterSend,
) {
    let tx = sockets.0;
    let rx = sockets.1;
    println!("#    send bytes from {:?} to {:?}: {:?}", tx, rx, out);
    let expect = out.to_vec();
    let tx_thread = std::thread::spawn(move || {
        loop {
            let r = tx.write(out);
            println!("#      write gives {:?}", r);
            match r {
                Ok(n) if n == out.len() => break,
                Ok(n) => out = &out[n..],
                Err(zx_status::Status::SHOULD_WAIT) => {
                    std::thread::sleep(std::time::Duration::from_millis(10))
                }
                Err(x) => panic!("Unhandled write error: {:?}", x),
            }
        }
        match after_send {
            AfterSend::RemainOpen => std::thread::park(),
            AfterSend::CloseSender => drop(tx),
        }
    });
    let mut in_bytes = Vec::new();
    let mut buf = [0u8; 1];
    let wait_some = || std::thread::sleep(std::time::Duration::from_millis(10));
    loop {
        // other channel should eventually receive the message, but we allow that propagation
        // need not be instant
        let r = rx.read(&mut buf);
        println!("#      read gives {:?}", r);
        match r {
            Ok(0) => (),
            Ok(1) => {
                in_bytes.push(buf[0]);
                if in_bytes.len() == out.len() {
                    assert_eq!(in_bytes, expect);
                    break;
                }
            }
            Ok(n) => panic!("Unexpected read length {}", n),
            Err(zx_status::Status::SHOULD_WAIT) => wait_some(),
            Err(x) => panic!("Unexpected error {:?}", x),
        }
    }
    let mut num_waits = 0;
    loop {
        match rx.read(&mut buf) {
            Ok(0) => (),
            Ok(n) => panic!("Unexpected bytes received on channel: {:?}", &buf[..n]),
            Err(zx_status::Status::SHOULD_WAIT) => {
                if num_waits >= 10 && after_send == AfterSend::RemainOpen {
                    tx_thread.thread().unpark();
                    break;
                }
                num_waits += 1;
                wait_some()
            }
            Err(zx_status::Status::PEER_CLOSED) if after_send == AfterSend::CloseSender => {
                break;
            }
            Err(x) => panic!("Unexpected error {:?}", x),
        }
    }
}

pub fn run(fixture: impl Fixture) {
    println!("# send bytes a->b remaining open");
    send_bytes(fixture.create_handles(fidl::SocketOpts::STREAM), &[1, 2, 3], AfterSend::RemainOpen);
    println!("# send bytes b->a remaining open");
    send_bytes(
        reverse(fixture.create_handles(fidl::SocketOpts::STREAM)),
        &[7, 8, 9],
        AfterSend::RemainOpen,
    );
    println!("# send bytes a->b then close");
    send_bytes(
        fixture.create_handles(fidl::SocketOpts::STREAM),
        &[1, 2, 3],
        AfterSend::CloseSender,
    );
    println!("# send bytes b->a then close");
    send_bytes(
        reverse(fixture.create_handles(fidl::SocketOpts::STREAM)),
        &[7, 8, 9],
        AfterSend::CloseSender,
    );
}

struct FidlFixture;

impl Fixture for FidlFixture {
    fn create_handles(&self, opts: fidl::SocketOpts) -> (fidl::Socket, fidl::Socket) {
        fidl::Socket::create(opts).unwrap()
    }
}

#[cfg(test)]
#[test]
fn tests() {
    run(FidlFixture)
}
