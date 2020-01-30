// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async as fasync;
use futures::{future::join, task::noop_waker, Future, FutureExt, StreamExt};
use mpmc::*;
use std::{
    pin::Pin,
    task::{Context, Poll},
};

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

#[fasync::run_singlethreaded]
#[test]
async fn backpressure() {
    let s: Sender<usize> = Sender::with_buffer_size(1);
    let _r = s.new_receiver();

    s.send(1).await;
    s.send(1).await;

    let mut send_exceeding_buffer = s.send(1).boxed();
    let poll_result =
        Pin::new(&mut send_exceeding_buffer).poll(&mut Context::from_waker(&noop_waker()));
    assert_eq!(poll_result, Poll::Pending);
}

#[fasync::run_singlethreaded]
#[test]
async fn backpressure_across_senders() {
    let s1: Sender<usize> = Sender::with_buffer_size(1);
    let s2 = s1.clone();
    let mut r = s1.new_receiver();

    s1.send(1).await;
    s1.send(1).await;

    // Ensure a different sender is pressured.
    let mut send1_exceeding_buffer = s2.send(1).boxed();
    let poll1_result =
        Pin::new(&mut send1_exceeding_buffer).poll(&mut Context::from_waker(&noop_waker()));
    assert_eq!(poll1_result, Poll::Pending);

    // Ensure all senders are blocked.
    let mut send2_exceeding_buffer = s1.send(1).boxed();
    let poll2_result =
        Pin::new(&mut send2_exceeding_buffer).poll(&mut Context::from_waker(&noop_waker()));
    assert_eq!(poll2_result, Poll::Pending);

    // Unblock
    r.next().await;
    r.next().await;

    // Ensure all sends resolve.
    join(send1_exceeding_buffer, send2_exceeding_buffer).await;
}
