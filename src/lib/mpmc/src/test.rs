// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use fuchsia_async as fasync;
use futures::StreamExt;
use mpmc::*;

#[fasync::run_singlethreaded]
#[test]
async fn it_works() {
    let s = Sender::default();
    let mut r1 = s.new_receiver();
    let mut r2 = r1.clone();

    s.send(20).await;
    assert_eq!(r1.next().await, Some(20));
    assert_eq!(r2.next().await, Some(20));
}

#[fasync::run_singlethreaded]
#[test]
async fn dropping_sender_terminates_stream() {
    let s = Sender::default();
    let mut r1 = s.new_receiver();
    let mut r2 = r1.clone();

    s.send(20).await;
    drop(s);
    assert_eq!(r1.next().await, Some(20));
    assert_eq!(r2.next().await, Some(20));
    assert_eq!(r1.next().await, None);
    assert_eq!(r2.next().await, None);
}

#[fasync::run_singlethreaded]
#[test]
async fn receivers_cloned_after_termination_yield_none() {
    let s = Sender::default();
    let mut r1 = s.new_receiver();

    s.send(20).await;
    drop(s);
    let mut r2 = r1.clone();
    assert_eq!(r1.next().await, Some(20));
    assert_eq!(r1.next().await, None);
    assert_eq!(r2.next().await, None);
}

#[fasync::run_singlethreaded]
#[test]
async fn sender_side_initialization() {
    let s = Sender::default();
    let mut r1 = s.new_receiver();
    let mut r2 = s.new_receiver();

    s.send(20).await;
    assert_eq!(r1.next().await, Some(20));
    assert_eq!(r2.next().await, Some(20));
}
