// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

extern crate byteorder;
extern crate failure;
extern crate fidl;
extern crate fidl_fuchsia_logger;
extern crate fuchsia_app as app;
extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;
extern crate libc;
extern crate parking_lot;

#[macro_use]
extern crate futures;

use app::server::ServicesServer;
use failure::{Error, ResultExt};
use fidl::endpoints2::{ClientEnd, ServiceMarker};
use futures::future::ok as fok;
use futures::prelude::Never;
use futures::Future;
use futures::FutureExt;
use futures::StreamExt;
use parking_lot::Mutex;
use std::collections::HashSet;
use std::collections::{vec_deque, VecDeque};
use std::sync::Arc;

use fidl_fuchsia_logger::{Log, LogFilterOptions, LogImpl, LogLevelFilter, LogListenerMarker,
                          LogListenerProxy, LogMarker, LogMessage, LogSink, LogSinkImpl,
                          LogSinkMarker};

mod klogger;
mod logger;

// Store 4 MB of log messages and delete on FIFO basis.
const OLD_MSGS_BUF_SIZE: usize = 4 * 1024 * 1024;

struct ListenerWrapper {
    listener: LogListenerProxy,
    min_severity: Option<i32>,
    pid: Option<u64>,
    tid: Option<u64>,
    tags: HashSet<String>,
}

#[derive(PartialEq)]
enum ListenerStatus {
    Fine,
    Stale,
}

impl ListenerWrapper {
    fn filter(&self, log_message: &mut LogMessage) -> bool {
        if self.pid.map(|pid| pid != log_message.pid).unwrap_or(false)
            || self.tid.map(|tid| tid != log_message.tid).unwrap_or(false)
            || self.min_severity
                .map(|min_sev| min_sev > log_message.severity)
                .unwrap_or(false)
        {
            return false;
        }

        if self.tags.len() != 0 {
            if !log_message.tags.iter().any(|tag| self.tags.contains(tag)) {
                return false;
            }
        }
        return true;
    }

    /// This fn assumes that logs have already been filtered.
    fn send_filtered_logs(&self, log_messages: &mut Vec<&mut LogMessage>) -> ListenerStatus {
        if let Err(e) = self.listener
            .log_many(&mut log_messages.iter_mut().map(|x| &mut **x))
        {
            match e {
                fidl::Error::ClientRead(zx::Status::PEER_CLOSED)
                | fidl::Error::ClientWrite(zx::Status::PEER_CLOSED) => ListenerStatus::Stale,
                e => {
                    eprintln!("Error calling listener: {:?}", e);
                    ListenerStatus::Fine
                }
            }
        } else {
            ListenerStatus::Fine
        }
    }

    fn send_log(&self, log_message: &mut LogMessage) -> ListenerStatus {
        if !self.filter(log_message) {
            return ListenerStatus::Fine;
        }
        if let Err(e) = self.listener.log(log_message) {
            match e {
                fidl::Error::ClientRead(zx::Status::PEER_CLOSED)
                | fidl::Error::ClientWrite(zx::Status::PEER_CLOSED) => ListenerStatus::Stale,
                e => {
                    eprintln!("Error calling listener: {:?}", e);
                    ListenerStatus::Fine
                }
            }
        } else {
            ListenerStatus::Fine
        }
    }
}

/// A Memory bounded buffer. MemoryBoundedBuffer does not calculate the size of `item`,
/// rather it takes the size as argument and then maintains its internal buffer.
/// Oldest item(s) are deleted in the event of buffer overflow.
struct MemoryBoundedBuffer<T> {
    inner: VecDeque<(T, usize)>,
    total_size: usize,
    capacity: usize,
}

/// `MemoryBoundedBuffer` mutable iterator.
struct IterMut<'a, T: 'a> {
    inner: vec_deque::IterMut<'a, (T, usize)>,
}

impl<'a, T> Iterator for IterMut<'a, T> {
    type Item = (&'a mut T, usize);

    #[inline]
    fn next(&mut self) -> Option<(&'a mut T, usize)> {
        self.inner.next().map(|(t, s)| (t, *s))
    }

    #[inline]
    fn size_hint(&self) -> (usize, Option<usize>) {
        self.inner.size_hint()
    }
}

impl<T> MemoryBoundedBuffer<T> {
    /// capacity in bytes
    pub fn new(capacity: usize) -> MemoryBoundedBuffer<T> {
        assert!(capacity > 0, "capacity should be more than 0");
        MemoryBoundedBuffer {
            inner: VecDeque::new(),
            capacity: capacity,
            total_size: 0,
        }
    }

    /// size in bytes
    pub fn push(&mut self, item: T, size: usize) {
        self.inner.push_back((item, size));
        self.total_size += size;
        while self.total_size > self.capacity {
            if let Some((_i, s)) = self.inner.pop_front() {
                self.total_size -= s;
            } else {
                panic!("this should not happen");
            }
        }
    }

    pub fn iter_mut(&mut self) -> IterMut<T> {
        IterMut {
            inner: self.inner.iter_mut(),
        }
    }
}

struct LogManagerShared {
    listeners: Vec<ListenerWrapper>,
    log_msg_buffer: MemoryBoundedBuffer<LogMessage>,
}

#[derive(Clone)]
struct LogManager {
    shared_members: Arc<Mutex<LogManagerShared>>,
}

fn run_listeners(listeners: &mut Vec<ListenerWrapper>, log_message: &mut LogMessage) {
    listeners.retain(|l| l.send_log(log_message) == ListenerStatus::Fine);
}

fn log_manager_helper(
    state: &mut LogManager, log_listener: ClientEnd<LogListenerMarker>,
    options: Option<Box<LogFilterOptions>>, dump_logs: bool,
) -> impl Future<Item = (), Error = Never> {
    let ll = match log_listener.into_proxy() {
        Ok(ll) => ll,
        Err(e) => {
            eprintln!("Logger: Error getting listener proxy: {:?}", e);
            // TODO: close channel
            return fok(());
        }
    };

    let mut lw = ListenerWrapper {
        listener: ll,
        min_severity: None,
        pid: None,
        tid: None,
        tags: HashSet::new(),
    };

    if let Some(mut options) = options {
        lw.tags = options.tags.drain(..).collect();
        if lw.tags.len() > fidl_fuchsia_logger::MAX_TAGS as usize {
            // TODO: close channel
            return fok(());
        }
        for tag in &lw.tags {
            if tag.len() > fidl_fuchsia_logger::MAX_TAG_LEN as usize {
                // TODO: close channel
                return fok(());
            }
        }
        if options.filter_by_pid {
            lw.pid = Some(options.pid)
        }
        if options.filter_by_tid {
            lw.tid = Some(options.tid)
        }
        if options.verbosity > 0 {
            lw.min_severity = Some(-(options.verbosity as i32))
        } else if options.min_severity != LogLevelFilter::None {
            lw.min_severity = Some(options.min_severity as i32)
        }
    }
    let mut shared_members = state.shared_members.lock();
    {
        let mut log_length = 0;
        let mut v = vec![];
        for (msg, s) in shared_members.log_msg_buffer.iter_mut() {
            if lw.filter(msg) {
                if log_length + s > fidl_fuchsia_logger::MAX_LOG_MANY_SIZE as usize {
                    if ListenerStatus::Fine != lw.send_filtered_logs(&mut v) {
                        return fok(());
                    }
                    v.clear();
                    log_length = 0;
                }
                log_length = log_length + s;
                v.push(msg);
            }
        }
        if v.len() > 0 {
            if ListenerStatus::Fine != lw.send_filtered_logs(&mut v) {
                return fok(());
            }
        }
    }

    if !dump_logs {
        shared_members.listeners.push(lw);
    } else {
        let _ = lw.listener.done();
    }
    fok(())
}

fn spawn_log_manager(state: LogManager, chan: async::Channel) {
    async::spawn(
        LogImpl {
            state,
            on_open: |_, _| fok(()),
            dump_logs: |state, log_listener, options, _controller| {
                log_manager_helper(state, log_listener, options, true)
            },
            listen: |state, log_listener, options, _controller| {
                log_manager_helper(state, log_listener, options, false)
            },
        }.serve(chan)
            .recover(|e| eprintln!("Log manager failed: {:?}", e)),
    )
}

fn process_log(shared_members: Arc<Mutex<LogManagerShared>>, mut log_msg: LogMessage, size: usize) {
    let mut shared_members = shared_members.lock();
    run_listeners(&mut shared_members.listeners, &mut log_msg);
    shared_members.log_msg_buffer.push(log_msg, size);
}

fn spawn_log_sink(state: LogManager, chan: async::Channel) {
    async::spawn(
        LogSinkImpl {
            state,
            on_open: |_, _| fok(()),
            connect: |state, socket, _controller| {
                let ls = match logger::LoggerStream::new(socket) {
                    Err(e) => {
                        eprintln!("Logger: Failed to create tokio socket: {:?}", e);
                        // TODO: close channel
                        return fok(());
                    }
                    Ok(ls) => ls,
                };

                let shared_members = state.shared_members.clone();
                let f = ls.for_each(move |(log_msg, size)| {
                    process_log(shared_members.clone(), log_msg, size);
                    Ok(())
                }).map(|_s| ());

                async::spawn(f.recover(|e| {
                    eprintln!("Logger: Stream failed {:?}", e);
                }));

                fok(())
            },
        }.serve(chan)
            .recover(|e| eprintln!("Log sink failed: {:?}", e)),
    )
}

fn main() {
    if let Err(e) = main_wrapper() {
        eprintln!("LoggerService: Error: {:?}", e);
    }
}

fn main_wrapper() -> Result<(), Error> {
    let mut executor = async::Executor::new().context("unable to create executor")?;
    let shared_members = Arc::new(Mutex::new(LogManagerShared {
        listeners: Vec::new(),
        log_msg_buffer: MemoryBoundedBuffer::new(OLD_MSGS_BUF_SIZE),
    }));
    let shared_members_clone = shared_members.clone();
    let shared_members_clone2 = shared_members.clone();
    klogger::add_listener(move |log_msg, size| {
        process_log(shared_members_clone2.clone(), log_msg, size);
    }).context("failed to read kernel logs")?;
    let server_fut = ServicesServer::new()
        .add_service((LogMarker::NAME, move |chan| {
            let ls = LogManager {
                shared_members: shared_members.clone(),
            };
            spawn_log_manager(ls, chan);
        }))
        .add_service((LogSinkMarker::NAME, move |chan| {
            let ls = LogManager {
                shared_members: shared_members_clone.clone(),
            };
            spawn_log_sink(ls, chan)
        }))
        .start()
        .map_err(|e| e.context("error starting service server"))?;

    Ok(executor
        .run(server_fut, 3)
        .context("running server")
        .map(|_| ())?) // 3 threads
}

#[cfg(test)]
mod tests {
    extern crate timebomb;

    use self::timebomb::timeout_ms;
    use super::*;
    use std::sync::atomic::{AtomicBool, Ordering};

    use fidl::encoding2::OutOfLine;
    use fidl_fuchsia_logger::{LogFilterOptions, LogListener, LogListenerImpl, LogListenerMarker,
                              LogProxy, LogSinkProxy};
    use logger::fx_log_packet_t;
    use zx::prelude::*;

    mod memory_bounded_buffer {
        use super::*;

        #[test]
        fn test_simple() {
            let mut m = MemoryBoundedBuffer::new(12);
            m.push(1, 4);
            m.push(2, 4);
            m.push(3, 4);
            assert_eq!(
                &m.iter_mut().collect::<Vec<(&mut i32, usize)>>()[..],
                &[(&mut 1, 4), (&mut 2, 4), (&mut 3, 4)]
            );
        }

        #[test]
        fn test_bound() {
            let mut m = MemoryBoundedBuffer::new(12);
            m.push(1, 4);
            m.push(2, 4);
            m.push(3, 5);
            assert_eq!(
                &m.iter_mut().collect::<Vec<(&mut i32, usize)>>()[..],
                &[(&mut 2, 4), (&mut 3, 5)]
            );
            m.push(4, 4);
            m.push(5, 4);
            assert_eq!(
                &m.iter_mut().collect::<Vec<(&mut i32, usize)>>()[..],
                &[(&mut 4, 4), (&mut 5, 4)]
            );
        }
    }

    struct LogListenerState {
        expected: Vec<LogMessage>,
        done: Arc<AtomicBool>,
        closed: Arc<AtomicBool>,
        test_name: String,
    }
    impl LogListenerState {
        fn log(&mut self, msg: LogMessage) {
            let len = self.expected.len();
            assert_ne!(len, 0, "got extra message: {:?}", msg);
            // we can receive messages out of order
            self.expected.retain(|e| e != &msg);
            assert_eq!(
                self.expected.len(),
                len - 1,
                "expected: {:?},\nmsg: {:?}",
                self.expected,
                msg
            );
            if self.expected.len() == 0 {
                self.done.store(true, Ordering::Relaxed);
                println!("DEBUG: {}: setting done=true", self.test_name);
            }
        }
    }

    fn copy_log_message(log_message: &LogMessage) -> LogMessage {
        LogMessage {
            pid: log_message.pid,
            tid: log_message.tid,
            time: log_message.time,
            severity: log_message.severity,
            dropped_logs: log_message.dropped_logs,
            tags: log_message.tags.clone(),
            msg: log_message.msg.clone(),
        }
    }

    /// Function to convert fx_log_packet_t to &[u8].
    /// This function is safe as it works on `fx_log_packet_t` which
    /// doesn't have any uninitialized padding bits.
    fn to_u8_slice(p: &fx_log_packet_t) -> &[u8] {
        // This code just converts to &[u8] so no need to explicity drop it as memory
        // location would be freed as soon as p is dropped.
        unsafe {
            ::std::slice::from_raw_parts(
                (p as *const fx_log_packet_t) as *const u8,
                ::std::mem::size_of::<fx_log_packet_t>(),
            )
        }
    }

    fn memset<T: Copy>(x: &mut [T], offset: usize, value: T, size: usize) {
        x[offset..(offset + size)]
            .iter_mut()
            .for_each(|x| *x = value);
    }

    fn spawn_log_listener(ll: LogListenerState, chan: async::Channel) {
        async::spawn(
            LogListenerImpl {
                state: ll,
                on_open: |_, _| fok(()),
                done: |ll, _| {
                    println!("DEBUG: {}: done called", ll.test_name);
                    ll.closed.store(true, Ordering::Relaxed);
                    fok(())
                },
                log: |ll, msg, _controller| {
                    println!("DEBUG: {}: log called", ll.test_name);
                    ll.log(msg);
                    fok(())
                },
                log_many: |ll, msgs, _controller| {
                    println!(
                        "DEBUG: {}: logMany called, msgs.len(): {}",
                        ll.test_name,
                        msgs.len()
                    );
                    for msg in msgs {
                        ll.log(msg);
                    }
                    fok(())
                },
            }.serve(chan)
                .recover(|e| panic!("test fail {:?}", e)),
        )
    }

    fn setup_listener(
        ll: LogListenerState, lp: LogProxy, filter_options: Option<&mut LogFilterOptions>,
        dump_logs: bool,
    ) {
        let (remote, local) = zx::Channel::create().expect("failed to create zx channel");
        let remote_ptr = fidl::endpoints2::ClientEnd::<LogListenerMarker>::new(remote);
        let local = async::Channel::from_channel(local).expect("failed to make async channel");
        spawn_log_listener(ll, local);

        let filter_options = filter_options.map(OutOfLine);

        if dump_logs {
            lp.dump_logs(remote_ptr, filter_options)
                .expect("failed to register listener");
        } else {
            lp.listen(remote_ptr, filter_options)
                .expect("failed to register listener");
        }
    }

    fn setup_test() -> (
        async::Executor,
        LogProxy,
        LogSinkProxy,
        zx::Socket,
        zx::Socket,
    ) {
        let executor = async::Executor::new().expect("unable to create executor");
        let (sin, sout) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        let shared_members = Arc::new(Mutex::new(LogManagerShared {
            listeners: Vec::new(),
            log_msg_buffer: MemoryBoundedBuffer::new(OLD_MSGS_BUF_SIZE),
        }));

        let lm = LogManager {
            shared_members: shared_members,
        };

        let (client_end, server_end) = zx::Channel::create().expect("unable to create channel");
        let client_end = async::Channel::from_channel(client_end).unwrap();
        let log_proxy = LogProxy::new(client_end);
        let server_end = async::Channel::from_channel(server_end).expect("unable to asyncify");
        spawn_log_manager(lm.clone(), server_end);

        let (client_end, server_end) = zx::Channel::create().expect("unable to create channel");
        let client_end = async::Channel::from_channel(client_end).unwrap();
        let log_sink_proxy = LogSinkProxy::new(client_end);
        let server_end = async::Channel::from_channel(server_end).expect("unable to asyncify");
        spawn_log_sink(lm.clone(), server_end);

        (executor, log_proxy, log_sink_proxy, sin, sout)
    }

    fn setup_default_packet() -> fx_log_packet_t {
        let mut p: fx_log_packet_t = Default::default();
        p.metadata.pid = 1;
        p.metadata.tid = 1;
        p.metadata.severity = LogLevelFilter::Warn.into_primitive().into();
        p.metadata.dropped_logs = 2;
        p.data[0] = 5;
        memset(&mut p.data[..], 1, 65, 5);
        memset(&mut p.data[..], 7, 66, 5);
        return p;
    }

    fn test_log_manager_helper(test_dump_logs: bool) {
        let (mut executor, log_proxy, log_sink_proxy, sin, sout) = setup_test();
        let mut p = setup_default_packet();
        sin.write(to_u8_slice(&mut p)).unwrap();
        log_sink_proxy.connect(sout).expect("unable to connect");
        p.metadata.severity = LogLevelFilter::Info.into_primitive().into();
        sin.write(to_u8_slice(&mut p)).unwrap();

        let mut lm1 = LogMessage {
            time: p.metadata.time,
            pid: p.metadata.pid,
            tid: p.metadata.tid,
            dropped_logs: p.metadata.dropped_logs,
            severity: p.metadata.severity,
            msg: String::from("BBBBB"),
            tags: vec![String::from("AAAAA")],
        };
        let lm2 = copy_log_message(&lm1);
        lm1.severity = LogLevelFilter::Warn.into_primitive().into();
        let mut lm3 = copy_log_message(&lm2);
        lm3.pid = 2;
        let done = Arc::new(AtomicBool::new(false));
        let closed = Arc::new(AtomicBool::new(false));
        let ls = LogListenerState {
            expected: vec![lm1, lm2, lm3],
            done: done.clone(),
            closed: closed.clone(),
            test_name: "log_manager_test".to_string(),
        };

        setup_listener(ls, log_proxy, None, test_dump_logs);

        p.metadata.pid = 2;
        sin.write(to_u8_slice(&mut p)).unwrap();

        let tries = 10;
        for _ in 0..tries {
            if done.load(Ordering::Relaxed) || (test_dump_logs && closed.load(Ordering::Relaxed)) {
                break;
            }
            let timeout = async::Timer::<()>::new(100.millis().after_now());
            executor.run(timeout, 2).unwrap();
        }

        if test_dump_logs {
            assert!(
                closed.load(Ordering::Relaxed),
                "done fn should have been called"
            );
        } else {
            assert!(
                done.load(Ordering::Relaxed),
                "task should have completed by now"
            );
        }
    }

    #[test]
    fn test_log_manager_simple() {
        test_log_manager_helper(false);
    }

    #[test]
    fn test_log_manager_dump() {
        test_log_manager_helper(true);
    }

    fn filter_test_helper(
        expected: Vec<LogMessage>, packets: Vec<fx_log_packet_t>,
        filter_options: Option<&mut LogFilterOptions>, test_name: &str,
    ) {
        println!("DEBUG: {}: setup test", test_name);
        let (mut executor, log_proxy, log_sink_proxy, sin, sout) = setup_test();
        println!("DEBUG: {}: call connect", test_name);
        log_sink_proxy.connect(sout).expect("unable to connect");
        let done = Arc::new(AtomicBool::new(false));
        let ls = LogListenerState {
            expected: expected,
            done: done.clone(),
            closed: Arc::new(AtomicBool::new(false)),
            test_name: test_name.clone().to_string(),
        };
        println!("DEBUG: {}: call setup_listener", test_name);
        setup_listener(ls, log_proxy, filter_options, false);
        println!("DEBUG: {}: call write", test_name);
        for mut p in packets {
            sin.write(to_u8_slice(&mut p)).unwrap();
        }
        println!("DEBUG: {}: write returned", test_name);
        let tries = 10;

        for _ in 0..tries {
            if done.load(Ordering::Relaxed) {
                break;
            }
            let timeout = async::Timer::<()>::new(100.millis().after_now());
            println!("DEBUG: {}: wait on executor", test_name);
            executor.run(timeout, 2).unwrap();
            println!("DEBUG: {}: executor returned", test_name);
        }
        assert!(
            done.load(Ordering::Relaxed),
            "task should have completed by now"
        );
        println!("DEBUG: {}: assert done", test_name);
    }

    const TEST_TIMEOUT: u32 = 5000; // in ms

    #[test]
    fn test_filter_by_pid() {
        timeout_ms(
            || {
                let p = setup_default_packet();
                let mut p2 = p.clone();
                p2.metadata.pid = 0;
                let lm = LogMessage {
                    pid: p.metadata.pid,
                    tid: p.metadata.tid,
                    time: p.metadata.time,
                    dropped_logs: p.metadata.dropped_logs,
                    severity: p.metadata.severity,
                    msg: String::from("BBBBB"),
                    tags: vec![String::from("AAAAA")],
                };
                let options = &mut LogFilterOptions {
                    filter_by_pid: true,
                    pid: 1,
                    filter_by_tid: false,
                    tid: 0,
                    min_severity: LogLevelFilter::None,
                    verbosity: 0,
                    tags: vec![],
                };
                filter_test_helper(vec![lm], vec![p, p2], Some(options), "test_filter_by_pid");
            },
            TEST_TIMEOUT,
        );
    }

    #[test]
    fn test_filter_by_tid() {
        timeout_ms(
            || {
                let mut p = setup_default_packet();
                p.metadata.pid = 0;
                let mut p2 = p.clone();
                p2.metadata.tid = 0;
                let lm = LogMessage {
                    pid: p.metadata.pid,
                    tid: p.metadata.tid,
                    time: p.metadata.time,
                    dropped_logs: p.metadata.dropped_logs,
                    severity: p.metadata.severity,
                    msg: String::from("BBBBB"),
                    tags: vec![String::from("AAAAA")],
                };
                let options = &mut LogFilterOptions {
                    filter_by_pid: false,
                    pid: 1,
                    filter_by_tid: true,
                    tid: 1,
                    min_severity: LogLevelFilter::None,
                    verbosity: 0,
                    tags: vec![],
                };
                filter_test_helper(vec![lm], vec![p, p2], Some(options), "test_filter_by_tid");
            },
            TEST_TIMEOUT,
        );
    }

    #[test]
    fn test_filter_by_min_severity() {
        timeout_ms(
            || {
                let p = setup_default_packet();
                let mut p2 = p.clone();
                p2.metadata.pid = 0;
                p2.metadata.tid = 0;
                p2.metadata.severity = LogLevelFilter::Error.into_primitive().into();
                let lm = LogMessage {
                    pid: p2.metadata.pid,
                    tid: p2.metadata.tid,
                    time: p2.metadata.time,
                    dropped_logs: p2.metadata.dropped_logs,
                    severity: p2.metadata.severity,
                    msg: String::from("BBBBB"),
                    tags: vec![String::from("AAAAA")],
                };
                let options = &mut LogFilterOptions {
                    filter_by_pid: false,
                    pid: 1,
                    filter_by_tid: false,
                    tid: 1,
                    min_severity: LogLevelFilter::Error,
                    verbosity: 0,
                    tags: vec![],
                };
                filter_test_helper(
                    vec![lm],
                    vec![p, p2],
                    Some(options),
                    "test_filter_by_min_severity",
                );
            },
            TEST_TIMEOUT,
        );
    }

    #[test]
    fn test_filter_by_combination() {
        timeout_ms(
            || {
                let mut p = setup_default_packet();
                p.metadata.pid = 0;
                p.metadata.tid = 0;
                let mut p2 = p.clone();
                p2.metadata.severity = LogLevelFilter::Error.into_primitive().into();
                let mut p3 = p.clone();
                p3.metadata.pid = 1;
                let lm = LogMessage {
                    pid: p2.metadata.pid,
                    tid: p2.metadata.tid,
                    time: p2.metadata.time,
                    dropped_logs: p2.metadata.dropped_logs,
                    severity: p2.metadata.severity,
                    msg: String::from("BBBBB"),
                    tags: vec![String::from("AAAAA")],
                };
                let options = &mut LogFilterOptions {
                    filter_by_pid: true,
                    pid: 0,
                    filter_by_tid: false,
                    tid: 1,
                    min_severity: LogLevelFilter::Error,
                    verbosity: 0,
                    tags: vec![],
                };
                filter_test_helper(
                    vec![lm],
                    vec![p, p2, p3],
                    Some(options),
                    "test_filter_by_combination",
                );
            },
            TEST_TIMEOUT,
        );
    }

    #[test]
    fn test_filter_by_tags() {
        timeout_ms(
            || {
                let mut p = setup_default_packet();
                let mut p2 = p.clone();
                // p tags - "DDDDD"
                memset(&mut p.data[..], 1, 68, 5);

                p2.metadata.pid = 0;
                p2.metadata.tid = 0;
                p2.data[6] = 5;
                // p2 tag - "AAAAA", "BBBBB"
                // p2 msg - "CCCCC"
                memset(&mut p2.data[..], 13, 67, 5);

                let lm1 = LogMessage {
                    pid: p.metadata.pid,
                    tid: p.metadata.tid,
                    time: p.metadata.time,
                    dropped_logs: p.metadata.dropped_logs,
                    severity: p.metadata.severity,
                    msg: String::from("BBBBB"),
                    tags: vec![String::from("DDDDD")],
                };
                let lm2 = LogMessage {
                    pid: p2.metadata.pid,
                    tid: p2.metadata.tid,
                    time: p2.metadata.time,
                    dropped_logs: p2.metadata.dropped_logs,
                    severity: p2.metadata.severity,
                    msg: String::from("CCCCC"),
                    tags: vec![String::from("AAAAA"), String::from("BBBBB")],
                };
                let options = &mut LogFilterOptions {
                    filter_by_pid: false,
                    pid: 1,
                    filter_by_tid: false,
                    tid: 1,
                    min_severity: LogLevelFilter::None,
                    verbosity: 0,
                    tags: vec![String::from("BBBBB"), String::from("DDDDD")],
                };
                filter_test_helper(
                    vec![lm1, lm2],
                    vec![p, p2],
                    Some(options),
                    "test_filter_by_tags",
                );
            },
            TEST_TIMEOUT,
        );
    }
}
