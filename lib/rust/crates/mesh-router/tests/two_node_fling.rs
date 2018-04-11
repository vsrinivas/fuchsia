#![feature(fnbox)]

extern crate bytes;
extern crate failure;
extern crate futures;
extern crate mesh_protocol;
extern crate mesh_router;
extern crate timebomb;

mod mocking;

use bytes::Bytes;
use failure::Error;
use futures::{future, Async, FutureExt, Sink, SinkExt};
use std::{cell::RefCell,
          rc::{Rc, Weak}};

////////////////////////////////////////////////////////////////////////////////////////////////////
// common decls

type BoxedChunkSink = Box<Sink<SinkItem = mesh_router::Chunk, SinkError = Error>>;

////////////////////////////////////////////////////////////////////////////////////////////////////
// Link implementation

struct InProcessLink {
    other_node: Weak<RefCell<TestNode>>,
}

impl mesh_router::OutgoingLink for InProcessLink {
    type Msg = BoxedChunkSink;

    fn begin_msg(&mut self, rh: mesh_protocol::RoutingHeader) -> Self::Msg {
        if let Some(n) = self.other_node.upgrade() {
            Box::new(n.borrow_mut().forward(rh))
        } else {
            panic!();
        }
    }
}

impl InProcessLink {
    fn link(node_a: Rc<RefCell<TestNode>>, node_b: Rc<RefCell<TestNode>>) {
        let a = Rc::new(RefCell::new(InProcessLink {
            other_node: Rc::downgrade(&node_b),
        }));
        let b = Rc::new(RefCell::new(InProcessLink {
            other_node: Rc::downgrade(&node_a),
        }));
        let a_id = node_a.borrow().id().clone();
        let b_id = node_b.borrow().id().clone();
        node_a.borrow_mut().add_outgoing(b_id, a);
        node_b.borrow_mut().add_outgoing(a_id, b);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// tests

type TestNode = mesh_router::Node<mocking::MockNodeHandler<InProcessLink>, InProcessLink>;

#[test]
fn two_node_fling() {
    timebomb::timeout_ms(
        || {
            let n1 = mocking::MockNodeHandler::new();
            let n2 = mocking::MockNodeHandler::new();

            let mut node1 = Rc::new(RefCell::new(TestNode::new(1.into(), n1.clone())));
            let mut node2 = Rc::new(RefCell::new(TestNode::new(2.into(), n2.clone())));
            InProcessLink::link(node1.clone(), node2.clone());

            n1.borrow_mut().expect_new_stream(
                "stream_from_introduction",
                Box::new(|s, arg| {
                    assert_eq!(arg, [1, 2, 3]);
                    Ok(())
                }),
            );

            let mut s1 = node1.borrow_mut().new_stream(
                2.into(),
                mesh_protocol::StreamType::ReliableOrdered,
                vec![1, 2, 3],
            );

            let (remote_stm, remote_stm_ctl) = mocking::MockChunkSink::new();
            remote_stm_ctl
                .borrow_mut()
                .expect_poll_ready("stream msg 1", Box::new(|| Ok(Async::Ready(()))))
                .expect_start_send("stream msg 1", Box::new(|chunk| Ok(())))
                .expect_poll_flush("stream msg 1", Box::new(|| Ok(Async::Ready(()))));
            let s2 = n2.borrow_mut().expect_intro(
                "single stream introduction",
                Box::new(move |s, intro| {
                    assert_eq!(intro, vec![1, 2, 3]);
                    Ok(())
                }),
            );
            n2.borrow_mut().expect_stream_begin_msg(
                "single stream recv",
                s2,
                Box::new(move || remote_stm),
            );

            mocking::complete(s1.and_then(move |s1| {
                let snk = node1.borrow_mut().stream_begin_send(s1, 3);
                snk.send(Bytes::from(vec![7, 8, 9]))
            }));
        },
        1000,
    );
}
