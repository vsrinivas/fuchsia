// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bytes::Bytes;
use failure::Error;
use futures::{task::Context, Async, Sink};
use mesh_router::Chunk;
use std::collections::VecDeque;

#[derive(Debug, Fail)]
enum LinearizationError {
    #[fail(
        display = "window overflow: queue tip={}, chunk=@{}+{}b, window={}",
        tip,
        chunk_offset,
        chunk_length,
        window
    )]
    WindowOverflow {
        tip: u64,
        chunk_offset: u64,
        chunk_length: u64,
        window: u64,
    },
    #[fail(display = "linearization completed with holes")]
    HolesFound,
}

pub trait LinearizeChunks {
    type Linearizer: Sink<SinkItem = Chunk, SinkError = Error>;
    fn with_chunks(self, window: u64) -> Self::Linearizer;
}

pub struct Linearizer<T> {
    target: T,
    tip: u64,
    // TODO(ctiller): this should be a binary tree of chunks to avoid n**2 reassembly behavior
    queue: VecDeque<Chunk>,
    window: u64,
}

impl<T> Linearizer<T> 
where
    T: Sink<SinkItem = Bytes, SinkError = Error>,
{
    // Do the first part of poll_flush, poll_ready: flushing buffered up pieces that were not
    // received in order that can now be presented in order
    // Result(Async::Ready(())) indicates that the caller should call through to the equivalent
    // function (poll_flush, poll_ready) on self.target
    fn poll_header(&mut self, cx: &mut Context) -> Result<Async<()>, Error> {
        loop {
            match self.queue.pop_front() {
                Some(chunk) => {
                    if chunk.offset == self.tip {
                        match self.target.poll_ready(cx)? {
                            Async::Pending => {
                                self.queue.push_front(chunk);
                                return Ok(Async::Pending);
                            }
                            Async::Ready(()) => self.target.start_send(chunk.data)?,
                        }
                    } else {
                        self.queue.push_front(chunk);
                        return Ok(Async::Ready(()));
                    }
                }
                None => {
                    return Ok(Async::Ready(()));
                }
            }
        }
    }
}

impl<T> Sink for Linearizer<T>
where
    T: Sink<SinkItem = Bytes, SinkError = Error>,
{
    type SinkItem = Chunk;
    type SinkError = Error;

    fn poll_ready(&mut self, cx: &mut Context) -> Result<Async<()>, Error> {
        try_ready!(self.poll_header(cx));
        self.target.poll_ready(cx)
    }

    fn poll_flush(&mut self, cx: &mut Context) -> Result<Async<()>, Error> {
        try_ready!(self.poll_header(cx));
        self.target.poll_flush(cx)
    }

    fn start_send(&mut self, chunk: Chunk) -> Result<(), Error> {
        let start_tip = self.tip;
        let chunk_offset = chunk.offset;

        // fast path: if this chunk completes the next part of the queue, send it straight through
        // bypassing the queue
        // for sources that send linearly, this ensures no allocation within queue
        if chunk_offset == start_tip {
            if match self.queue.front() {
                None => true,
                Some(Chunk { offset, .. }) => *offset >= chunk_offset + chunk.data.len() as u64,
            } {
                let len = chunk.data.len() as u64;
                let r = self.target.start_send(chunk.data);
                if r.is_ok() {
                    self.tip += len;
                }
                return r;
            }
        }
        // handle old chunks
        if chunk_offset < start_tip {
            let chunk_end = chunk_offset + chunk.data.len() as u64;
            if chunk_end <= start_tip {
                // chunk entirely before where we are
                return Ok(());
            } else {
                // chunk straddles the tip
                return self.start_send(Chunk {
                    offset: start_tip,
                    data: chunk.data.slice_from((start_tip - chunk_offset) as usize),
                });
            }
        }
        // chunk is somewhere past the tip
        // verify it's not also out of window
        if chunk_offset > start_tip + self.window {
            bail!(LinearizationError::WindowOverflow {
                tip: start_tip,
                chunk_offset,
                chunk_length: chunk.data.len() as u64,
                window: self.window,
            });
        }
        // start at the end of the queue and try to slot it in somewhere
        // assumption: most senders send mostly in order
        if self.queue.is_empty() {
            self.queue.push_back(chunk);
            Ok(())
        } else {
            // TODO(ctiller): this should really be converted to binary tree code to avoid n**2
            // behavior
            for i in (0..self.queue.len()).rev() {
                unimplemented!();
            }
            // chunk is before anything currently in the queue
            unimplemented!()
        }
    }

    fn poll_close(&mut self, cx: &mut Context) -> Result<Async<()>, Error> {
        if self.poll_flush(cx)?.is_pending() {
            return Ok(Async::Pending);
        }
        if !self.queue.is_empty() {
            bail!(LinearizationError::HolesFound);
        }
        self.target.poll_close(cx)
    }
}

impl<S> LinearizeChunks for S
where
    S: Sink<SinkItem = Bytes, SinkError = Error> + Sized,
{
    type Linearizer = Linearizer<S>;
    fn with_chunks(self, window: u64) -> Linearizer<S> {
        Linearizer {
            target: self,
            tip: 0,
            queue: VecDeque::new(),
            window,
        }
    }
}

#[cfg(test)]
mod tests {

    use bytes::Bytes;
    use chunk::{Chunk, LinearizeChunks};
    use failure::Error;
    use futures::{executor::LocalPool, future::ok, task::Context, Async, Future, FutureExt, Never,
                  Sink, SinkExt, StreamExt};
    use std::{boxed::FnBox, cell::RefCell, collections::VecDeque, rc::Rc, thread};

    #[test]
    fn no_op() {
        let (snk, snk_ctl) = MockDataSink::new();
        let chunk_snk = snk.with_chunks(512);
    }

    #[test]
    fn send_one_in_order() {
        let (snk, snk_ctl) = MockDataSink::new();
        let chunk_snk = snk.with_chunks(512);

        snk_ctl
            .borrow_mut()
            .expect_poll_ready("send", Box::new(|| Ok(Async::Ready(()))))
            .expect_start_send(
                "send",
                Box::new(|data: Bytes| {
                    assert_eq!(*data, [1, 2, 3]);
                    Ok(())
                }),
            )
            .expect_poll_flush("send", Box::new(|| Ok(Async::Ready(()))));

        complete(
            chunk_snk
                .send(Chunk {
                    offset: 0,
                    data: Bytes::from(vec![1, 2, 3]),
                })
                .then(|r| {
                    r.unwrap();
                    ok(())
                }),
        );
    }

    #[test]
    fn send_two_in_order() {
        let (snk, snk_ctl) = MockDataSink::new();
        let chunk_snk = snk.with_chunks(512);

        snk_ctl
            .borrow_mut()
            .expect_poll_ready("send", Box::new(|| Ok(Async::Ready(()))))
            .expect_start_send(
                "send",
                Box::new(|data: Bytes| {
                    assert_eq!(*data, [1, 2, 3]);
                    Ok(())
                }),
            )
            .expect_poll_flush("send", Box::new(|| Ok(Async::Ready(()))))
            .expect_poll_ready("send", Box::new(|| Ok(Async::Ready(()))))
            .expect_start_send(
                "send",
                Box::new(|data: Bytes| {
                    assert_eq!(*data, [4, 5, 6]);
                    Ok(())
                }),
            )
            .expect_poll_flush("send", Box::new(|| Ok(Async::Ready(()))));

        complete(
            chunk_snk
                .send(Chunk {
                    offset: 0,
                    data: Bytes::from(vec![1, 2, 3]),
                })
                .and_then(|s| {
                    s.send(Chunk {
                        offset: 3,
                        data: Bytes::from(vec![4, 5, 6]),
                    })
                })
                .then(|r| {
                    r.unwrap();
                    ok(())
                }),
        );
    }

    #[test]
    fn send_two_out_of_order() {
        let (snk, snk_ctl) = MockDataSink::new();
        let chunk_snk = snk.with_chunks(512);

        snk_ctl
            .borrow_mut()
            .expect_poll_ready("send", Box::new(|| Ok(Async::Ready(()))))
            .expect_poll_flush("send", Box::new(|| Ok(Async::Ready(()))))
            .expect_poll_ready("send", Box::new(|| Ok(Async::Ready(()))))
            .expect_start_send(
                "send",
                Box::new(|data: Bytes| {
                    assert_eq!(*data, [1, 2, 3]);
                    Ok(())
                }),
            )
            .expect_poll_ready("send", Box::new(|| Ok(Async::Ready(()))))
            .expect_start_send(
                "send",
                Box::new(|data: Bytes| {
                    assert_eq!(*data, [4, 5, 6]);
                    Ok(())
                }),
            )
            .expect_poll_flush("send", Box::new(|| Ok(Async::Ready(()))));

        complete(
            chunk_snk
                .send(Chunk {
                    offset: 3,
                    data: Bytes::from(vec![4, 5, 6]),
                })
                .and_then(|s| {
                    s.send(Chunk {
                        offset: 0,
                        data: Bytes::from(vec![1, 2, 3]),
                    })
                })
                .then(|r| {
                    r.unwrap();
                    ok(())
                }),
        );
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////
    /// test framework

    pub fn complete<F>(fut: F)
    where
        F: Future<Item = (), Error = Never> + 'static,
    {
        let mut pool = LocalPool::new();
        let mut exec = pool.executor();
        exec.spawn_local(fut);
        pool.run(&mut exec);
    }

    struct MockCB<F>(String, F);
    fn desc<F>(cb: &MockCB<F>) -> &str {
        match cb {
            MockCB(s, _) => s,
        }
    }

    pub struct DataSinkController {
        // Context is dropped because lifetimes make this FnBox uncallable
        next_poll_ready: VecDeque<MockCB<Box<FnBox() -> Result<Async<()>, Error>>>>,
        next_start_send: VecDeque<MockCB<Box<FnBox(Bytes) -> Result<(), Error>>>>,
        next_poll_flush: VecDeque<MockCB<Box<FnBox() -> Result<Async<()>, Error>>>>,
        next_poll_close: VecDeque<MockCB<Box<FnBox() -> Result<Async<()>, Error>>>>,
    }

    pub struct MockDataSink {
        controller: Rc<RefCell<DataSinkController>>,
    }

    impl DataSinkController {
        pub fn expect_poll_ready(
            &mut self, desc: &str, f: Box<FnBox() -> Result<Async<()>, Error>>,
        ) -> &mut Self {
            self.next_poll_ready.push_back(MockCB(desc.to_string(), f));
            return self;
        }

        pub fn expect_start_send(
            &mut self, desc: &str, f: Box<FnBox(Bytes) -> Result<(), Error>>,
        ) -> &mut Self {
            self.next_start_send.push_back(MockCB(desc.to_string(), f));
            return self;
        }

        pub fn expect_poll_flush(
            &mut self, desc: &str, f: Box<FnBox() -> Result<Async<()>, Error>>,
        ) -> &mut Self {
            self.next_poll_flush.push_back(MockCB(desc.to_string(), f));
            return self;
        }

        pub fn expect_poll_close(
            &mut self, desc: &str, f: Box<FnBox() -> Result<Async<()>, Error>>,
        ) -> &mut Self {
            self.next_poll_close.push_back(MockCB(desc.to_string(), f));
            return self;
        }

        pub fn validate(&self) -> bool {
            let mut ok = true;

            for poll_ready in &self.next_poll_ready {
                println!("Expected poll_ready: {}", desc(poll_ready));
                ok = false;
            }

            for start_send in &self.next_start_send {
                println!("Expected start_send: {}", desc(start_send));
                ok = false;
            }

            for poll_flush in &self.next_poll_flush {
                println!("Expected poll_flush: {}", desc(poll_flush));
                ok = false;
            }

            for poll_close in &self.next_poll_close {
                println!("Expected poll_close: {}", desc(poll_close));
                ok = false;
            }

            ok
        }
    }

    impl Sink for MockDataSink {
        type SinkItem = Bytes;
        type SinkError = Error;

        fn poll_ready(&mut self, cx: &mut Context) -> Result<Async<()>, Error> {
            match self.controller.borrow_mut().next_poll_ready.pop_front() {
                Some(MockCB(_, f)) => f(),
                None => panic!("Unexpected poll_ready"),
            }
        }

        fn start_send(&mut self, ch: Bytes) -> Result<(), Error> {
            match self.controller.borrow_mut().next_start_send.pop_front() {
                Some(MockCB(_, f)) => f(ch),
                None => panic!("Unexpected start_send"),
            }
        }

        fn poll_flush(&mut self, cx: &mut Context) -> Result<Async<()>, Error> {
            match self.controller.borrow_mut().next_poll_flush.pop_front() {
                Some(MockCB(_, f)) => f(),
                None => panic!("Unexpected poll_flush"),
            }
        }

        fn poll_close(&mut self, cx: &mut Context) -> Result<Async<()>, Error> {
            match self.controller.borrow_mut().next_poll_close.pop_front() {
                Some(MockCB(_, f)) => f(),
                None => panic!("Unexpected poll_close"),
            }
        }
    }

    impl MockDataSink {
        pub fn new() -> (MockDataSink, Rc<RefCell<DataSinkController>>) {
            let mut controller = Rc::new(RefCell::new(DataSinkController {
                next_poll_ready: VecDeque::new(),
                next_start_send: VecDeque::new(),
                next_poll_flush: VecDeque::new(),
                next_poll_close: VecDeque::new(),
            }));
            let link = MockDataSink {
                controller: controller.clone(),
            };
            (link, controller)
        }
    }

    impl Drop for DataSinkController {
        fn drop(&mut self) {
            if !thread::panicking() {
                assert!(self.validate());
            } else {
                self.validate();
            }
        }
    }

}
