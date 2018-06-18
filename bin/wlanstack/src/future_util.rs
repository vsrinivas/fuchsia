// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::prelude::*;
use futures::task;
use futures::stream::Fuse;

pub struct GroupAvailable<S> where S: Stream {
    stream: Fuse<S>,
    error: Option<S::Error>,
}

impl<S> Stream for GroupAvailable<S> where S: Stream {
    type Item = Vec<S::Item>;
    type Error = S::Error;

    fn poll_next(&mut self, cx: &mut task::Context) -> Poll<Option<Self::Item>, Self::Error> {
        if let Some(e) = self.error.take() {
            return Err(e);
        }
        let mut batch = match self.stream.poll_next(cx) {
            Ok(Async::Ready(Some(item))) => vec![item],
            Ok(Async::Ready(None)) => return Ok(Async::Ready(None)),
            Ok(Async::Pending) => return Ok(Async::Pending),
            Err(e) => return Err(e),
        };
        loop {
            match self.stream.poll_next(cx) {
                Ok(Async::Ready(Some(item))) => batch.push(item),
                Ok(Async::Ready(None)) | Ok(Async::Pending) => break,
                Err(e) => {
                    self.error = Some(e);
                    break;
                }
            }
        }
        Ok(Async::Ready(Some(batch)))
    }
}

pub trait GroupAvailableExt: Stream {
    /// An adaptor for grouping readily available messages into a single Vec item.
    ///
    /// Similar to StreamExt.chunks(), except the size of produced batches can be arbitrary,
    /// and only depends on how many items were available when the stream was polled.
    fn group_available(self) -> GroupAvailable<Self>
        where Self: Sized
    {
        GroupAvailable {
            stream: self.fuse(),
            error: None
        }
    }
}

impl<T> GroupAvailableExt for T where T: Stream + ?Sized {}

#[cfg(test)]
mod tests {
    use async;
    use futures::prelude::*;
    use futures::stream;
    use super::GroupAvailableExt;
    use futures::channel::mpsc;

    #[test]
    fn empty() {
        let mut exec = async::Executor::new().expect("Failed to create an executor");
        let (item, _) = exec.run_singlethreaded(stream::empty::<(), Never>().group_available().next())
            .unwrap_or_else(|(e, _)| e.never_into());
        assert!(item.is_none());
    }

    #[test]
    fn pending() {
        let mut exec = async::Executor::new().expect("Failed to create an executor");
        let always_pending = stream::poll_fn::<(), Never, _>(|_cx| Ok(Async::Pending));
        let mut fut = always_pending.group_available().next();
        let a = exec.run_until_stalled(&mut fut)
            .unwrap_or_else(|(e, _)| e.never_into());
        assert!(a.is_pending());
    }

    #[test]
    fn group_available_items() {
        let mut exec = async::Executor::new().expect("Failed to create an executor");
        let (send, recv) = mpsc::unbounded();

        send.unbounded_send(10i32).unwrap();
        send.unbounded_send(20i32).unwrap();
        let s = recv.group_available();
        let (item, s) = exec.run_singlethreaded(s.next())
            .unwrap_or_else(|(e, _)| e.never_into());
        assert_eq!(Some(vec![10i32, 20i32]), item);

        send.unbounded_send(30i32).unwrap();
        let (item, _) = exec.run_singlethreaded(s.next())
            .unwrap_or_else(|(e, _)| e.never_into());
        assert_eq!(Some(vec![30i32]), item);
    }

    #[test]
    fn buffer_error() {
        let mut exec = async::Executor::new().expect("Failed to create an executor");

        let s = stream::iter_result(vec![Ok(10i32), Ok(20i32), Err(-30i32)])
            .group_available();
        let (item, s) = exec.run_singlethreaded(s.next())
            .map_err(|(e, _)| e)
            .expect("expected a successful value");
        assert_eq!(Some(vec![10i32, 20i32]), item);

        let res = exec.run_singlethreaded(s.next())
            .map(|(r, _)| r)
            .map_err(|(e, _)| e);
        assert_eq!(Err(-30i32), res);
    }
}