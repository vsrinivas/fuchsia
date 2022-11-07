use std::sync::mpsc;

use futures_util::stream::StreamExt;
use async_broadcast::*;

use easy_parallel::Parallel;
use futures_lite::future::block_on;

#[test]
fn basic_sync() {
    let (s, mut r1) = broadcast(10);
    let mut r2 = r1.clone();

    s.try_broadcast(7).unwrap();
    assert_eq!(r1.try_recv().unwrap(), 7);
    assert_eq!(r2.try_recv().unwrap(), 7);

    let mut r3 = r1.clone();
    s.try_broadcast(8).unwrap();
    assert_eq!(r1.try_recv().unwrap(), 8);
    assert_eq!(r2.try_recv().unwrap(), 8);
    assert_eq!(r3.try_recv().unwrap(), 8);
}

#[test]
fn basic_async() {
    block_on(async {
        let (s, mut r1) = broadcast(10);
        let mut r2 = r1.clone();

        s.broadcast(7).await.unwrap();
        assert_eq!(r1.recv().await.unwrap(), 7);
        assert_eq!(r2.recv().await.unwrap(), 7);

        // Now let's try the Stream impl.
        let mut r3 = r1.clone();
        s.broadcast(8).await.unwrap();
        assert_eq!(r1.next().await.unwrap(), 8);
        assert_eq!(r2.next().await.unwrap(), 8);
        assert_eq!(r3.next().await.unwrap(), 8);
    });
}

#[test]
fn parallel() {
    let (s1, mut r1) = broadcast(2);
    let s2 = s1.clone();
    let mut r2 = r1.clone();

    let (sender_sync_send, sender_sync_recv) = mpsc::channel();
    let (receiver_sync_send, receiver_sync_recv) = mpsc::channel();

    Parallel::new()
        .add(move || {
            sender_sync_recv.recv().unwrap();

            s1.try_broadcast(7).unwrap();
            s2.try_broadcast(8).unwrap();
            assert!(s2.try_broadcast(9).unwrap_err().is_full());
            assert!(s1.try_broadcast(10).unwrap_err().is_full());
            receiver_sync_send.send(()).unwrap();

            drop(s1);
            drop(s2);
            receiver_sync_send.send(()).unwrap();
        })
        .add(move || {
            assert_eq!(r1.try_recv(), Err(TryRecvError::Empty));
            assert_eq!(r2.try_recv(), Err(TryRecvError::Empty));
            sender_sync_send.send(()).unwrap();

            receiver_sync_recv.recv().unwrap();
            assert_eq!(r1.try_recv().unwrap(), 7);
            assert_eq!(r1.try_recv().unwrap(), 8);
            assert_eq!(r2.try_recv().unwrap(), 7);
            assert_eq!(r2.try_recv().unwrap(), 8);

            receiver_sync_recv.recv().unwrap();
            assert_eq!(r1.try_recv(), Err(TryRecvError::Closed));
            assert_eq!(r2.try_recv(), Err(TryRecvError::Closed));
        })
        .run();
}

#[test]
fn parallel_async() {
    let (s1, mut r1) = broadcast(2);
    let s2 = s1.clone();
    let mut r2 = r1.clone();

    let (sender_sync_send, sender_sync_recv) = mpsc::channel();
    let (receiver_sync_send, receiver_sync_recv) = mpsc::channel();

    Parallel::new()
        .add(move || block_on(async move {
            sender_sync_recv.recv().unwrap();

            s1.broadcast(7).await.unwrap();
            s2.broadcast(8).await.unwrap();
            assert!(s2.try_broadcast(9).unwrap_err().is_full());
            assert!(s1.try_broadcast(10).unwrap_err().is_full());
            receiver_sync_send.send(()).unwrap();

            s1.broadcast(9).await.unwrap();
            s2.broadcast(10).await.unwrap();

            drop(s1);
            drop(s2);
            receiver_sync_send.send(()).unwrap();
        }))
        .add(move || block_on(async move {
            assert_eq!(r1.try_recv(), Err(TryRecvError::Empty));
            assert_eq!(r2.try_recv(), Err(TryRecvError::Empty));
            sender_sync_send.send(()).unwrap();

            receiver_sync_recv.recv().unwrap();
            assert_eq!(r1.next().await.unwrap(), 7);
            assert_eq!(r2.next().await.unwrap(), 7);
            assert_eq!(r1.recv().await.unwrap(), 8);
            assert_eq!(r2.recv().await.unwrap(), 8);

            receiver_sync_recv.recv().unwrap();
            assert_eq!(r1.next().await.unwrap(), 9);
            assert_eq!(r2.next().await.unwrap(), 9);

            assert_eq!(r1.recv().await.unwrap(), 10);
            assert_eq!(r2.recv().await.unwrap(), 10);

            assert_eq!(r1.recv().await, Err(RecvError));
            assert_eq!(r2.recv().await, Err(RecvError));
        }))
        .run();
}
