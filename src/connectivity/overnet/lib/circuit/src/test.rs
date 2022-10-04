// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::*;
use fuchsia_async as fasync;
use futures::channel::mpsc::unbounded;
use futures::channel::oneshot::channel as oneshot;
use futures::stream::StreamExt;

/// Given two nodes that both exist in this process, connect them directly to oneanother. This
/// function runs the connection and must be polled continuously.
///
/// This is written as a test function. In short: it panics at the slightest irregularity.
async fn connect_nodes(a: &Node, b: &Node) -> impl std::future::Future<Output = ()> + Send + Sync {
    let (a_control_reader, write_a_control_reader) = stream::stream();
    let (read_a_control_writer, a_control_writer) = stream::stream();
    let (b_control_reader, write_b_control_reader) = stream::stream();
    let (read_b_control_writer, b_control_writer) = stream::stream();
    let (a_new_stream_sender, mut a_new_streams) = unbounded();
    let (b_new_stream_sender, mut b_new_streams) = unbounded();
    let (a_new_stream_requests, a_new_stream_receiver) = unbounded();
    let (b_new_stream_requests, b_new_stream_receiver) = unbounded();
    let a_runner = a.link_node(
        Some((read_a_control_writer, write_a_control_reader)),
        a_new_stream_sender,
        a_new_stream_receiver,
        Quality::IN_PROCESS,
    );
    let b_runner = b.link_node(
        Some((read_b_control_writer, write_b_control_reader)),
        b_new_stream_sender,
        b_new_stream_receiver,
        Quality::IN_PROCESS,
    );

    async move {
        let a_to_b = async move {
            loop {
                let got = a_control_reader
                    .read(1, |buf| {
                        b_control_writer.write(buf.len(), |out_buf| {
                            out_buf[..buf.len()].copy_from_slice(buf);
                            Ok(buf.len())
                        })?;
                        Ok(((), buf.len()))
                    })
                    .await;

                if let Err(Error::ConnectionClosed) = got {
                    break;
                }
                got.unwrap();
            }
        };

        let b_to_a = async move {
            loop {
                let got = b_control_reader
                    .read(1, |buf| {
                        a_control_writer.write(buf.len(), |out_buf| {
                            out_buf[..buf.len()].copy_from_slice(buf);
                            Ok(buf.len())
                        })?;
                        Ok(((), buf.len()))
                    })
                    .await;

                if let Err(Error::ConnectionClosed) = got {
                    break;
                }
                got.unwrap();
            }
        };

        futures::pin_mut!(a_to_b);
        futures::pin_mut!(b_to_a);

        let control = futures::future::join(a_to_b, b_to_a);

        let a_runner = async move {
            let _ = a_runner.await;
        };
        let b_runner = async move {
            let _ = b_runner.await;
        };

        futures::pin_mut!(a_runner);
        futures::pin_mut!(b_runner);

        let runners = futures::future::join(a_runner, b_runner);

        let a_to_b = async move {
            while let Some((reader, writer)) = a_new_streams.next().await {
                let (err_sender, err) = oneshot();
                b_new_stream_requests.unbounded_send((reader, writer, err_sender)).unwrap();
                err.await.unwrap().unwrap();
            }
        };

        let b_to_a = async move {
            while let Some((reader, writer)) = b_new_streams.next().await {
                let (err_sender, err) = oneshot();
                a_new_stream_requests.unbounded_send((reader, writer, err_sender)).unwrap();
                err.await.unwrap().unwrap();
            }
        };

        futures::pin_mut!(a_to_b);
        futures::pin_mut!(b_to_a);

        let streams = futures::future::join(a_to_b, b_to_a);

        futures::future::join3(control, runners, streams).await;
    }
}

#[fuchsia::test]
async fn connection_test() {
    let (new_peer_sender_a, mut new_peers) = unbounded();
    let (new_peer_sender_b, _new_peers_b) = unbounded();
    let (incoming_streams_sender_a, _streams_a) = unbounded();
    let (incoming_streams_sender_b, mut streams) = unbounded();
    let a = Node::new("a", "test", new_peer_sender_a, incoming_streams_sender_a).unwrap();
    let b = Node::new("b", "test", new_peer_sender_b, incoming_streams_sender_b).unwrap();

    let _conn = fasync::Task::spawn(connect_nodes(&a, &b).await);

    let new_peer = new_peers.next().await.unwrap();
    assert_eq!("b", &new_peer);

    let (_reader, peer_writer) = stream::stream();
    let (peer_reader, writer) = stream::stream();
    a.connect_to_peer(peer_reader, peer_writer, "b").await.unwrap();

    writer
        .write(8, |buf| {
            buf[..8].copy_from_slice(&[1, 2, 3, 4, 5, 6, 7, 8]);
            Ok(8)
        })
        .unwrap();

    let (reader, _writer, from) = streams.next().await.unwrap();
    assert_eq!("a", &from);

    reader
        .read(8, |buf| {
            assert_eq!(&[1, 2, 3, 4, 5, 6, 7, 8], &buf);
            Ok(((), 8))
        })
        .await
        .unwrap();
}

#[fuchsia::test]
async fn connection_test_duplex() {
    let (new_peer_sender_a, new_peers_a) = unbounded();
    let (new_peer_sender_b, new_peers_b) = unbounded();
    let (incoming_streams_sender_a, streams_a) = unbounded();
    let (incoming_streams_sender_b, streams_b) = unbounded();
    let a = Node::new("a", "test", new_peer_sender_a, incoming_streams_sender_a).unwrap();
    let b = Node::new("b", "test", new_peer_sender_b, incoming_streams_sender_b).unwrap();

    let _conn = fasync::Task::spawn(connect_nodes(&a, &b).await);

    let a_task = async move {
        let mut new_peers = new_peers_a;
        let new_peer = new_peers.next().await.unwrap();
        assert_eq!("b", &new_peer);

        let (_reader, peer_writer) = stream::stream();
        let (peer_reader, writer) = stream::stream();
        a.connect_to_peer(peer_reader, peer_writer, "b").await.unwrap();

        writer
            .write(8, |buf| {
                buf[..8].copy_from_slice(&[1, 2, 3, 4, 5, 6, 7, 8]);
                Ok(8)
            })
            .unwrap();

        let mut streams = streams_a;
        let (reader, _writer, from) = streams.next().await.unwrap();
        assert_eq!("b", &from);

        reader
            .read(8, |buf| {
                assert_eq!(&[9, 10, 11, 12, 13, 14, 15, 16], &buf);
                Ok(((), 8))
            })
            .await
            .unwrap();
    };

    let b_task = async move {
        let mut new_peers = new_peers_b;
        let new_peer = new_peers.next().await.unwrap();
        assert_eq!("a", &new_peer);

        let (_reader, peer_writer) = stream::stream();
        let (peer_reader, writer) = stream::stream();
        b.connect_to_peer(peer_reader, peer_writer, "a").await.unwrap();

        writer
            .write(8, |buf| {
                buf[..8].copy_from_slice(&[9, 10, 11, 12, 13, 14, 15, 16]);
                Ok(8)
            })
            .unwrap();

        let mut streams = streams_b;
        let (reader, _writer, from) = streams.next().await.unwrap();
        assert_eq!("a", &from);

        reader
            .read(8, |buf| {
                assert_eq!(&[1, 2, 3, 4, 5, 6, 7, 8], &buf);
                Ok(((), 8))
            })
            .await
            .unwrap();
    };

    futures::pin_mut!(a_task);
    futures::pin_mut!(b_task);

    futures::future::join(a_task, b_task).await;
}

#[fuchsia::test]
async fn connection_test_with_router() {
    let (new_peer_sender_a, new_peers_a) = unbounded();
    let (new_peer_sender_b, new_peers_b) = unbounded();
    let (incoming_streams_sender_a, streams_a) = unbounded();
    let (incoming_streams_sender_b, streams_b) = unbounded();
    let a = Node::new("a", "test", new_peer_sender_a, incoming_streams_sender_a).unwrap();
    let b = Node::new("b", "test", new_peer_sender_b, incoming_streams_sender_b).unwrap();
    let (new_peer_sender_router, _new_peers_router) = unbounded();
    let (incoming_streams_sender_router, _streams_router) = unbounded();
    let (router, router_task) = Node::new_with_router(
        "router",
        "test",
        std::time::Duration::from_millis(100),
        new_peer_sender_router,
        incoming_streams_sender_router,
    )
    .unwrap();

    let _conn_a = fasync::Task::spawn(connect_nodes(&a, &router).await);
    let _conn_b = fasync::Task::spawn(connect_nodes(&b, &router).await);
    let _router_task = fasync::Task::spawn(router_task);

    let a_task = async move {
        let mut new_peers = new_peers_a;
        let new_peers = [new_peers.next().await.unwrap(), new_peers.next().await.unwrap()];
        assert!(new_peers.iter().any(|x| x == "b"));
        assert!(new_peers.iter().any(|x| x == "router"));

        let (_reader, peer_writer) = stream::stream();
        let (peer_reader, writer) = stream::stream();
        a.connect_to_peer(peer_reader, peer_writer, "b").await.unwrap();

        writer
            .write(8, |buf| {
                buf[..8].copy_from_slice(&[1, 2, 3, 4, 5, 6, 7, 8]);
                Ok(8)
            })
            .unwrap();

        let mut streams = streams_a;
        let (reader, _writer, from) = streams.next().await.unwrap();
        assert_eq!("b", &from);

        reader
            .read(8, |buf| {
                assert_eq!(&[9, 10, 11, 12, 13, 14, 15, 16], &buf);
                Ok(((), 8))
            })
            .await
            .unwrap();
    };

    let b_task = async move {
        let mut new_peers = new_peers_b;
        let new_peers = [new_peers.next().await.unwrap(), new_peers.next().await.unwrap()];
        assert!(new_peers.iter().any(|x| x == "a"));
        assert!(new_peers.iter().any(|x| x == "router"));

        let (_reader, peer_writer) = stream::stream();
        let (peer_reader, writer) = stream::stream();
        b.connect_to_peer(peer_reader, peer_writer, "a").await.unwrap();

        writer
            .write(8, |buf| {
                buf[..8].copy_from_slice(&[9, 10, 11, 12, 13, 14, 15, 16]);
                Ok(8)
            })
            .unwrap();

        let mut streams = streams_b;
        let (reader, _writer, from) = streams.next().await.unwrap();
        assert_eq!("a", &from);

        reader
            .read(8, |buf| {
                assert_eq!(&[1, 2, 3, 4, 5, 6, 7, 8], &buf);
                Ok(((), 8))
            })
            .await
            .unwrap();
    };

    futures::pin_mut!(a_task);
    futures::pin_mut!(b_task);

    futures::future::join(a_task, b_task).await;
}

#[fuchsia::test]
async fn connection_node_test() {
    let (new_peer_sender_a, mut new_peers) = unbounded();
    let (new_peer_sender_b, _new_peers_b) = unbounded();
    let (a, a_incoming_conns) =
        connection::ConnectionNode::new("a", "test", new_peer_sender_a).unwrap();
    let (b, b_incoming_conns) =
        connection::ConnectionNode::new("b", "test", new_peer_sender_b).unwrap();

    let _conn = fasync::Task::spawn(connect_nodes(a.node(), b.node()).await);
    let _a_runner = fasync::Task::spawn(async move {
        futures::pin_mut!(a_incoming_conns);
        if a_incoming_conns.next().await.is_some() {
            unreachable!("Got connection from node 'a'")
        }
    });

    let new_peer = new_peers.next().await.unwrap();
    assert_eq!("b", &new_peer);

    let (_reader, peer_writer) = stream::stream();
    let (peer_reader, writer) = stream::stream();
    let _conn = a.connect_to_peer("b", peer_reader, peer_writer).await.unwrap();

    writer
        .write(8, |buf| {
            buf[..8].copy_from_slice(&[1, 2, 3, 4, 5, 6, 7, 8]);
            Ok(8)
        })
        .unwrap();

    futures::pin_mut!(b_incoming_conns);
    let conn = b_incoming_conns.next().await.unwrap();
    assert_eq!("a", conn.from());

    let (reader, _writer) = conn.bind_stream(0).await.unwrap();

    reader
        .read(8, |buf| {
            assert_eq!(&[1, 2, 3, 4, 5, 6, 7, 8], &buf);
            Ok(((), 8))
        })
        .await
        .unwrap();
}

#[fuchsia::test]
async fn connection_node_test_duplex() {
    let (new_peer_sender_a, mut new_peers) = unbounded();
    let (new_peer_sender_b, _new_peers_b) = unbounded();
    let (a, a_incoming_conns) =
        connection::ConnectionNode::new("a", "test", new_peer_sender_a).unwrap();
    let (b, b_incoming_conns) =
        connection::ConnectionNode::new("b", "test", new_peer_sender_b).unwrap();

    let _conn = fasync::Task::spawn(connect_nodes(a.node(), b.node()).await);
    let _a_runner = fasync::Task::spawn(async move {
        futures::pin_mut!(a_incoming_conns);
        if a_incoming_conns.next().await.is_some() {
            unreachable!("Got connection from node 'a'")
        }
    });

    let new_peer = new_peers.next().await.unwrap();
    assert_eq!("b", &new_peer);

    let (_reader, peer_writer) = stream::stream();
    let (peer_reader, writer) = stream::stream();
    let conn_a = a.connect_to_peer("b", peer_reader, peer_writer).await.unwrap();

    writer
        .write(8, |buf| {
            buf[..8].copy_from_slice(&[1, 2, 3, 4, 5, 6, 7, 8]);
            Ok(8)
        })
        .unwrap();

    futures::pin_mut!(b_incoming_conns);
    let conn_b = b_incoming_conns.next().await.unwrap();
    assert_eq!("a", conn_b.from());

    let (reader, _writer) = conn_b.bind_stream(0).await.unwrap();

    reader
        .read(8, |buf| {
            assert_eq!(&[1, 2, 3, 4, 5, 6, 7, 8], &buf);
            Ok(((), 8))
        })
        .await
        .unwrap();

    let (_reader, peer_writer) = stream::stream();
    let (peer_reader, writer) = stream::stream();
    let b_to_a = conn_b.alloc_stream(peer_reader, peer_writer).await.unwrap();

    writer
        .write(8, |buf| {
            buf[..8].copy_from_slice(&[9, 10, 11, 12, 13, 14, 15, 16]);
            Ok(8)
        })
        .unwrap();

    let (reader, _writer) = conn_a.bind_stream(b_to_a).await.unwrap();

    reader
        .read(8, |buf| {
            assert_eq!(&[9, 10, 11, 12, 13, 14, 15, 16], &buf);
            Ok(((), 8))
        })
        .await
        .unwrap();
}

#[fuchsia::test]
async fn connection_node_test_with_router() {
    let (new_peer_sender_a, mut new_peers) = unbounded();
    let (new_peer_sender_b, _new_peers_b) = unbounded();
    let (new_peer_sender_router, _new_peers_router) = unbounded();
    let (a, a_incoming_conns) =
        connection::ConnectionNode::new("a", "test", new_peer_sender_a).unwrap();
    let (b, b_incoming_conns) =
        connection::ConnectionNode::new("b", "test", new_peer_sender_b).unwrap();
    let (router, router_incoming_conns) = connection::ConnectionNode::new_with_router(
        "router",
        "test",
        std::time::Duration::from_millis(500),
        new_peer_sender_router,
    )
    .unwrap();

    let _conn_a = fasync::Task::spawn(connect_nodes(a.node(), router.node()).await);
    let _a_runner = fasync::Task::spawn(async move {
        futures::pin_mut!(a_incoming_conns);
        if a_incoming_conns.next().await.is_some() {
            unreachable!("Got connection from node 'a'")
        }
    });

    let _conn_b = fasync::Task::spawn(connect_nodes(b.node(), router.node()).await);
    let _router_runner = fasync::Task::spawn(async move {
        futures::pin_mut!(router_incoming_conns);
        if router_incoming_conns.next().await.is_some() {
            unreachable!("Got connection from router node")
        }
    });

    let new_peer_1 = new_peers.next().await.unwrap();
    let new_peer_2 = new_peers.next().await.unwrap();
    assert!([new_peer_1.as_str(), new_peer_2.as_str()].contains(&"b"));
    assert!([new_peer_1.as_str(), new_peer_2.as_str()].contains(&"router"));

    let (_reader, peer_writer) = stream::stream();
    let (peer_reader, writer) = stream::stream();
    let _conn = a.connect_to_peer("b", peer_reader, peer_writer).await.unwrap();

    writer
        .write(8, |buf| {
            buf[..8].copy_from_slice(&[1, 2, 3, 4, 5, 6, 7, 8]);
            Ok(8)
        })
        .unwrap();

    futures::pin_mut!(b_incoming_conns);
    let conn = b_incoming_conns.next().await.unwrap();
    assert_eq!("a", conn.from());

    let (reader, _writer) = conn.bind_stream(0).await.unwrap();

    reader
        .read(8, |buf| {
            assert_eq!(&[1, 2, 3, 4, 5, 6, 7, 8], &buf);
            Ok(((), 8))
        })
        .await
        .unwrap();
}
