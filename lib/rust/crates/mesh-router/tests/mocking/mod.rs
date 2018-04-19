use failure::Error;
use futures::{executor::LocalPool,
              future::{ok, result, FutureResult},
              task::Context,
              Async,
              Future,
              FutureExt,
              Sink};
use mesh_protocol;
use mesh_router;
use std::{boxed::FnBox, cell::RefCell, collections::VecDeque, fmt, rc::Rc, thread};

////////////////////////////////////////////////////////////////////////////////////////////////
// mocking framework

struct MockCB<F>(String, F);
fn desc<F>(cb: &MockCB<F>) -> &str {
    match cb {
        MockCB(s, _) => s,
    }
}

// link mocks

pub struct MockOutgoingLink {
    next: VecDeque<MockCB<Box<FnBox(mesh_protocol::RoutingHeader) -> MockChunkSink>>>,
}

impl MockOutgoingLink {
    pub fn new() -> Rc<RefCell<MockOutgoingLink>> {
        Rc::new(RefCell::new(MockOutgoingLink {
            next: VecDeque::new(),
        }))
    }

    pub fn expect_forward(
        &mut self, desc: &str, f: Box<FnBox(mesh_protocol::RoutingHeader) -> MockChunkSink>,
    ) {
        self.next.push_back(MockCB(desc.to_string(), f));
    }

    pub fn validate(&self) -> bool {
        let mut ok = true;

        for cmd in &self.next {
            println!("Expected forward: {}", desc(cmd));
            ok = false;
        }

        ok
    }
}

impl mesh_router::OutgoingLink for MockOutgoingLink {
    type Msg = MockChunkSink;

    fn begin_msg(&mut self, rh: mesh_protocol::RoutingHeader) -> Self::Msg {
        match self.next.pop_front() {
            Some(MockCB(_, sink_factory)) => sink_factory(rh),
            None => panic!("Unexpected forward request"),
        }
    }
}

impl Drop for MockOutgoingLink {
    fn drop(&mut self) {
        if !thread::panicking() {
            assert!(self.validate());
        } else {
            self.validate();
        }
    }
}

// stream handler mocks

pub struct MockStreamHolder<L> {
    stream: Option<mesh_router::StreamData>,
    // shared with MockNodeHandler
    next: Rc<RefCell<VecDeque<MockCB<NodeHandlerOp<L>>>>>,
}

impl<L> mesh_router::StreamDataHolder for MockStreamHolder<L> {
    fn stream_data(&self) -> &mesh_router::StreamData {
        match &self.stream {
            Some(x) => x,
            None => panic!("Stream not yet created"),
        }
    }

    fn stream_data_mut(&mut self) -> &mut mesh_router::StreamData {
        match &mut self.stream {
            Some(x) => x,
            None => panic!("Stream not yet created"),
        }
    }
}

// node handler mocks

enum NodeHandlerOp<L> {
    Intro(
        Rc<RefCell<MockStreamHolder<L>>>,
        Box<FnBox(Rc<RefCell<MockStreamHolder<L>>>, Vec<u8>) -> Result<(), Error>>,
    ),
    NewStream(
        Rc<RefCell<MockStreamHolder<L>>>,
        Box<FnBox(Rc<RefCell<MockStreamHolder<L>>>, Vec<u8>) -> Result<(), Error>>,
    ),
    // per-stream ops
    StreamBeginMessage(
        Rc<RefCell<MockStreamHolder<L>>>,
        Box<FnBox() -> MockChunkSink>,
    ),
    StreamFork(
        Rc<RefCell<MockStreamHolder<L>>>,
        Rc<RefCell<MockStreamHolder<L>>>,
        Box<FnBox(Rc<RefCell<MockStreamHolder<L>>>, Vec<u8>) -> Result<(), Error>>,
    ),
}

pub struct MockNodeHandler<L> {
    next: Rc<RefCell<VecDeque<MockCB<NodeHandlerOp<L>>>>>,
}

impl<L> MockNodeHandler<L> {
    pub fn new() -> Rc<RefCell<MockNodeHandler<L>>> {
        Rc::new(RefCell::new(MockNodeHandler {
            next: Rc::new(RefCell::new(VecDeque::new())),
        }))
    }

    fn new_stream(&mut self) -> Rc<RefCell<MockStreamHolder<L>>> {
        Rc::new(RefCell::new(MockStreamHolder {
            stream: None,
            next: self.next.clone(),
        }))
    }

    pub fn expect_intro(
        &mut self, desc: &str,
        f: Box<FnBox(Rc<RefCell<MockStreamHolder<L>>>, Vec<u8>) -> Result<(), Error>>,
    ) -> Rc<RefCell<MockStreamHolder<L>>> {
        let s = self.new_stream();
        self.next
            .borrow_mut()
            .push_back(MockCB(desc.to_string(), NodeHandlerOp::Intro(s.clone(), f)));
        s
    }

    pub fn expect_new_stream(
        &mut self, desc: &str,
        f: Box<FnBox(Rc<RefCell<MockStreamHolder<L>>>, Vec<u8>) -> Result<(), Error>>,
    ) -> Rc<RefCell<MockStreamHolder<L>>> {
        let s = self.new_stream();
        self.next.borrow_mut().push_back(MockCB(
            desc.to_string(),
            NodeHandlerOp::NewStream(s.clone(), f),
        ));
        s
    }

    pub fn expect_stream_begin_msg(
        &mut self, desc: &str, stream: Rc<RefCell<MockStreamHolder<L>>>,
        f: Box<FnBox() -> MockChunkSink>,
    ) {
        self.next.borrow_mut().push_back(MockCB(
            desc.to_string(),
            NodeHandlerOp::StreamBeginMessage(stream, f),
        ));
    }

    pub fn expect_fork(
        &mut self, desc: &str, stream: Rc<RefCell<MockStreamHolder<L>>>,
        f: Box<FnBox(Rc<RefCell<MockStreamHolder<L>>>, Vec<u8>) -> Result<(), Error>>,
    ) -> Rc<RefCell<MockStreamHolder<L>>> {
        let s = self.new_stream();
        self.next.borrow_mut().push_back(MockCB(
            desc.to_string(),
            NodeHandlerOp::StreamFork(stream, s.clone(), f),
        ));
        s
    }

    pub fn validate(&self) -> bool {
        let mut ok = true;

        for x in self.next.borrow().iter() {
            println!("Expected {:?}", desc(x));
            ok = false;
        }

        ok
    }

    fn run_stream_factory(
        out_stream: Rc<RefCell<MockStreamHolder<L>>>,
        factory: Box<FnBox(Rc<RefCell<MockStreamHolder<L>>>, Vec<u8>) -> Result<(), Error>>,
        stream_data: mesh_router::StreamData, arg: Vec<u8>,
    ) -> Result<Rc<RefCell<MockStreamHolder<L>>>, Error> {
        assert!(out_stream.borrow().stream.is_none());
        out_stream.borrow_mut().stream = Some(stream_data);
        factory(out_stream.clone(), arg)?;
        Ok(out_stream)
    }
}

impl<L> mesh_router::NodeHandler for MockNodeHandler<L> {
    type MessageStream = MockStreamHolder<L>;
    type Msg = MockChunkSink;
    type IntroFuture = FutureResult<Rc<RefCell<Self::MessageStream>>, Error>;
    type ForkedFuture = FutureResult<Rc<RefCell<Self::MessageStream>>, Error>;

    fn intro(
        &mut self, stream: mesh_router::StreamData, arg: Vec<u8>,
    ) -> FutureResult<Rc<RefCell<MockStreamHolder<L>>>, Error> {
        match self.next.borrow_mut().pop_front() {
            Some(MockCB(_, NodeHandlerOp::Intro(s, handler_factory))) => {
                result(Self::run_stream_factory(s, handler_factory, stream, arg))
            }
            Some(MockCB(desc, _)) => panic!("Expected intro request, got {}", desc),
            None => panic!("Unexpected intro request"),
        }
    }

    fn new_stream(
        &mut self, stream: mesh_router::StreamData, arg: &[u8],
    ) -> Rc<RefCell<MockStreamHolder<L>>> {
        match self.next.borrow_mut().pop_front() {
            Some(MockCB(_, NodeHandlerOp::NewStream(s, handler_factory))) => {
                Self::run_stream_factory(s, handler_factory, stream, arg.to_vec()).unwrap()
            }
            Some(MockCB(desc, _)) => panic!("Expected create request, got {}", desc),
            None => panic!("Unexpected create request"),
        }
    }

    fn stream_begin_msg(
        &mut self, stream: Rc<RefCell<Self::MessageStream>>, seq: u64, len: u64,
    ) -> MockChunkSink {
        match self.next.borrow_mut().pop_front() {
            Some(MockCB(_, NodeHandlerOp::StreamBeginMessage(s, sink_factory))) => {
                assert!(Rc::ptr_eq(&s, &stream));
                sink_factory()
            }
            Some(MockCB(desc, _)) => panic!("Expected recv request, got {}", desc),
            None => panic!("Unexpected recv request"),
        }
    }

    fn stream_fork(
        &mut self, stream: Rc<RefCell<Self::MessageStream>>,
        new_stream_data: mesh_router::StreamData, arg: Vec<u8>,
    ) -> FutureResult<Rc<RefCell<MockStreamHolder<L>>>, Error> {
        match self.next.borrow_mut().pop_front() {
            Some(MockCB(_, NodeHandlerOp::StreamFork(from, to, handler_factory))) => {
                assert!(Rc::ptr_eq(&from, &stream));
                result(Self::run_stream_factory(
                    to,
                    handler_factory,
                    new_stream_data,
                    arg,
                ))
            }
            Some(MockCB(desc, _)) => panic!("Expected fork request, got {}", desc),
            None => panic!("Unexpected fork request"),
        }
    }
}

impl<L> Drop for MockNodeHandler<L> {
    fn drop(&mut self) {
        if !thread::panicking() {
            assert!(self.validate());
        } else {
            self.validate();
        }
    }
}

// chunk sink mocks

enum ChunkSinkOp {
    // Context is dropped because lifetimes make this FnBox uncallable
    PollReady(Box<FnBox() -> Result<Async<()>, Error>>),
    StartSend(Box<FnBox(mesh_router::Chunk) -> Result<(), Error>>),
    PollFlush(Box<FnBox() -> Result<Async<()>, Error>>),
    PollClose(Box<FnBox() -> Result<Async<()>, Error>>),
}

pub struct ChunkSinkController {
    next: VecDeque<MockCB<ChunkSinkOp>>,
}

pub struct MockChunkSink {
    controller: Rc<RefCell<ChunkSinkController>>,
}

impl ChunkSinkController {
    pub fn expect_poll_ready(
        &mut self, desc: &str, f: Box<FnBox() -> Result<Async<()>, Error>>,
    ) -> &mut Self {
        self.next
            .push_back(MockCB(desc.to_string(), ChunkSinkOp::PollReady(f)));
        self
    }

    pub fn expect_start_send(
        &mut self, desc: &str, f: Box<FnBox(mesh_router::Chunk) -> Result<(), Error>>,
    ) -> &mut Self {
        self.next
            .push_back(MockCB(desc.to_string(), ChunkSinkOp::StartSend(f)));
        self
    }

    pub fn expect_poll_flush(
        &mut self, desc: &str, f: Box<FnBox() -> Result<Async<()>, Error>>,
    ) -> &mut Self {
        self.next
            .push_back(MockCB(desc.to_string(), ChunkSinkOp::PollFlush(f)));
        self
    }

    pub fn expect_poll_close(
        &mut self, desc: &str, f: Box<FnBox() -> Result<Async<()>, Error>>,
    ) -> &mut Self {
        self.next
            .push_back(MockCB(desc.to_string(), ChunkSinkOp::PollClose(f)));
        self
    }

    pub fn validate(&self) -> bool {
        let mut ok = true;

        for x in &self.next {
            println!("Expected {:?}", desc(x));
            ok = false;
        }

        ok
    }
}

impl Sink for MockChunkSink {
    type SinkItem = mesh_router::Chunk;
    type SinkError = Error;

    fn poll_ready(&mut self, cx: &mut Context) -> Result<Async<()>, Error> {
        match self.controller.borrow_mut().next.pop_front() {
            Some(MockCB(_, ChunkSinkOp::PollReady(f))) => f(),
            Some(MockCB(desc, _)) => panic!("Expected poll_ready, got {}", desc),
            None => panic!("Unexpected poll_ready"),
        }
    }

    fn start_send(&mut self, ch: mesh_router::Chunk) -> Result<(), Error> {
        match self.controller.borrow_mut().next.pop_front() {
            Some(MockCB(_, ChunkSinkOp::StartSend(f))) => f(ch),
            Some(MockCB(desc, _)) => panic!("Expected start_send, got {}", desc),
            None => panic!("Unexpected start_send"),
        }
    }

    fn poll_flush(&mut self, cx: &mut Context) -> Result<Async<()>, Error> {
        match self.controller.borrow_mut().next.pop_front() {
            Some(MockCB(_, ChunkSinkOp::PollFlush(f))) => f(),
            Some(MockCB(desc, _)) => panic!("Expected poll_flush, got {}", desc),
            None => panic!("Unexpected poll_flush"),
        }
    }

    fn poll_close(&mut self, cx: &mut Context) -> Result<Async<()>, Error> {
        match self.controller.borrow_mut().next.pop_front() {
            Some(MockCB(_, ChunkSinkOp::PollClose(f))) => f(),
            Some(MockCB(desc, _)) => panic!("Expected poll_close, got {}", desc),
            None => panic!("Unexpected poll_close"),
        }
    }
}

impl MockChunkSink {
    pub fn new() -> (MockChunkSink, Rc<RefCell<ChunkSinkController>>) {
        let mut controller = Rc::new(RefCell::new(ChunkSinkController {
            next: VecDeque::new(),
        }));
        let link = MockChunkSink {
            controller: controller.clone(),
        };
        (link, controller)
    }
}

impl Drop for ChunkSinkController {
    fn drop(&mut self) {
        if !thread::panicking() {
            assert!(self.validate());
        } else {
            self.validate();
        }
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////
// future support

pub fn complete<F>(fut: F)
where
    F: Future + 'static,
    <F as Future>::Error: fmt::Debug,
{
    let mut pool = LocalPool::new();
    let mut exec = pool.executor();
    exec.spawn_local(fut.then(|r| {
        r.unwrap();
        ok(())
    }));
    pool.run(&mut exec);
}
