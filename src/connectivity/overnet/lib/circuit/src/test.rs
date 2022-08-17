// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::*;
use fuchsia_async as fasync;

/// Given two nodes that both exist in this process, connect them directly to oneanother. This
/// function runs the connection and must be polled continuously.
///
/// This is written as a test function. In short: it panics at the slightest irregularity.
async fn connect_nodes(a: &Node, b: &Node) -> impl std::future::Future<Output = ()> + Send + Sync {
    let mut a = a.connect_node(Quality::IN_PROCESS).await;
    let mut b = b.connect_node(Quality::IN_PROCESS).await;

    async move {
        let (a_control_reader, a_control_writer) = a.take_control_stream().unwrap();
        let (b_control_reader, b_control_writer) = b.take_control_stream().unwrap();

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

        let a_runner = a.take_run_future().unwrap();
        let b_runner = b.take_run_future().unwrap();

        let a_runner = async move {
            let _ = a_runner.await;
        };
        let b_runner = async move {
            let _ = b_runner.await;
        };

        futures::pin_mut!(a_runner);
        futures::pin_mut!(b_runner);

        let runners = futures::future::join(a_runner, b_runner);

        let mut a_new_streams = a.take_new_streams().unwrap();
        let mut b_new_streams = b.take_new_streams().unwrap();

        let a_to_b = async move {
            while let Some((reader, writer)) = a_new_streams.next().await {
                b.create_stream(reader, writer).await.unwrap();
            }
        };

        let b_to_a = async move {
            while let Some((reader, writer)) = b_new_streams.next().await {
                a.create_stream(reader, writer).await.unwrap();
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
    let mut a = Node::new("a", "test").unwrap();
    let mut b = Node::new("b", "test").unwrap();

    let _conn = fasync::Task::spawn(connect_nodes(&a, &b).await);

    let mut new_peers = a.take_new_peers().unwrap();
    let new_peer = new_peers.next().await.unwrap();
    assert_eq!("b", &new_peer);

    let (_reader, writer) = a.connect_to_peer("b").await.unwrap();

    writer
        .write(8, |buf| {
            buf[..8].copy_from_slice(&[1, 2, 3, 4, 5, 6, 7, 8]);
            Ok(8)
        })
        .unwrap();

    let mut streams = b.take_incoming_streams().unwrap();
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
    let mut a = Node::new("a", "test").unwrap();
    let mut b = Node::new("b", "test").unwrap();

    let _conn = fasync::Task::spawn(connect_nodes(&a, &b).await);

    let a_task = async move {
        let mut new_peers = a.take_new_peers().unwrap();
        let new_peer = new_peers.next().await.unwrap();
        assert_eq!("b", &new_peer);

        let (_reader, writer) = a.connect_to_peer("b").await.unwrap();

        writer
            .write(8, |buf| {
                buf[..8].copy_from_slice(&[1, 2, 3, 4, 5, 6, 7, 8]);
                Ok(8)
            })
            .unwrap();

        let mut streams = a.take_incoming_streams().unwrap();
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
        let mut new_peers = b.take_new_peers().unwrap();
        let new_peer = new_peers.next().await.unwrap();
        assert_eq!("a", &new_peer);

        let (_reader, writer) = b.connect_to_peer("a").await.unwrap();

        writer
            .write(8, |buf| {
                buf[..8].copy_from_slice(&[9, 10, 11, 12, 13, 14, 15, 16]);
                Ok(8)
            })
            .unwrap();

        let mut streams = b.take_incoming_streams().unwrap();
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
    let mut a = Node::new("a", "test").unwrap();
    let mut b = Node::new("b", "test").unwrap();
    let (router, router_task) =
        Node::new_with_router("router", "test", std::time::Duration::from_millis(100)).unwrap();

    let _conn_a = fasync::Task::spawn(connect_nodes(&a, &router).await);
    let _conn_b = fasync::Task::spawn(connect_nodes(&b, &router).await);
    let _router_task = fasync::Task::spawn(router_task);

    let a_task = async move {
        let mut new_peers = a.take_new_peers().unwrap();
        let new_peers = [new_peers.next().await.unwrap(), new_peers.next().await.unwrap()];
        assert!(new_peers.iter().any(|x| x == "b"));
        assert!(new_peers.iter().any(|x| x == "router"));

        let (_reader, writer) = a.connect_to_peer("b").await.unwrap();

        writer
            .write(8, |buf| {
                buf[..8].copy_from_slice(&[1, 2, 3, 4, 5, 6, 7, 8]);
                Ok(8)
            })
            .unwrap();

        let mut streams = a.take_incoming_streams().unwrap();
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
        let mut new_peers = b.take_new_peers().unwrap();
        let new_peers = [new_peers.next().await.unwrap(), new_peers.next().await.unwrap()];
        assert!(new_peers.iter().any(|x| x == "a"));
        assert!(new_peers.iter().any(|x| x == "router"));

        let (_reader, writer) = b.connect_to_peer("a").await.unwrap();

        writer
            .write(8, |buf| {
                buf[..8].copy_from_slice(&[9, 10, 11, 12, 13, 14, 15, 16]);
                Ok(8)
            })
            .unwrap();

        let mut streams = b.take_incoming_streams().unwrap();
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
