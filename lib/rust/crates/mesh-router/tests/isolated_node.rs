#![feature(fnbox)]

extern crate bytes;
extern crate failure;
extern crate futures;
extern crate mesh_protocol;
extern crate mesh_router;

mod mocking;

use bytes::Bytes;
use futures::{Async, FutureExt, SinkExt};
use mesh_router::{StreamDataAccessor, StreamDataHolder};
use mocking::{MockChunkSink, MockNodeHandler, MockOutgoingLink};

////////////////////////////////////////////////////////////////////////////////////////////////
// actual tests

type TestNode = mesh_router::Node<MockNodeHandler<MockOutgoingLink>, MockOutgoingLink>;

#[test]
fn no_op() {
    let nh = MockNodeHandler::new();
    let mut n = TestNode::new(1.into(), nh);
}

#[test]
fn add_link() {
    let nh = MockNodeHandler::new();
    let mut n = TestNode::new(1.into(), nh);
    let link = MockOutgoingLink::new();
    n.add_outgoing(2.into(), link);
}

#[test]
fn create_stream() {
    let nh = MockNodeHandler::new();
    let mut n = TestNode::new(1.into(), nh.clone());
    nh.borrow_mut().expect_new_stream(
        "new_stream",
        Box::new(|sd, i| {
            assert_eq!(i, [1, 2, 3]);
            Ok(())
        }),
    );
    mocking::complete(n.new_stream(
        2.into(),
        mesh_protocol::StreamType::ReliableOrdered,
        vec![1, 2, 3],
    ));
}

#[test]
fn send_msg() {
    let nh = MockNodeHandler::new();
    let mut n = TestNode::new(1.into(), nh.clone());
    let nh2 = nh.clone();
    nh.borrow_mut().expect_new_stream(
        "new_stream",
        Box::new(|sd, i| {
            assert_eq!(i, [1, 2, 3]);
            Ok(())
        }),
    );
    let s = n.new_stream(
        2.into(),
        mesh_protocol::StreamType::ReliableOrdered,
        vec![1, 2, 3],
    );
    let link = MockOutgoingLink::new();
    n.add_outgoing(2.into(), link.clone());

    let (snk, snk_ctl) = MockChunkSink::new();
    snk_ctl
        .borrow_mut()
        .expect_poll_ready("for send chunk", Box::new(|| Ok(Async::Ready(()))))
        .expect_start_send("for send chunk", Box::new(|itm| Ok(())))
        .expect_poll_flush("for send chunk", Box::new(|| Ok(Async::Ready(()))));

    mocking::complete(s.then(move |sr| {
        let msg_stream = sr.unwrap();
        let msg_s_id = msg_stream.borrow().stream_data().id().clone();
        link.borrow_mut().expect_forward(
            "message",
            Box::new(move |rh: mesh_protocol::RoutingHeader| -> MockChunkSink {
                assert_eq!(rh.src, 1.into());
                assert_eq!(rh.dst, 2.into());
                assert_eq!(rh.stream, msg_s_id);
                assert_eq!(
                    rh.seq,
                    mesh_protocol::SequenceNum::new_accept_intro(vec![1, 2, 3])
                );
                snk
            }),
        );
        n.stream_begin_send(msg_stream, 3)
            .send(Bytes::from(vec![7, 8, 9]))
    }));
}
