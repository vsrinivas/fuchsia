// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use failure::{Error, ResultExt};
use fidl::endpoints::ClientEnd;
use fuchsia_async as fasync;
use futures::{TryFutureExt, TryStreamExt};
use parking_lot::Mutex;
use std::collections::HashSet;
use std::collections::{vec_deque, VecDeque};
use std::sync::Arc;

use fidl_fuchsia_logger::{
    LogFilterOptions, LogLevelFilter, LogListenerMarker, LogListenerProxy, LogMessage, LogRequest,
    LogRequestStream, LogSinkRequest, LogSinkRequestStream,
};

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
            || self.min_severity.map(|min_sev| min_sev > log_message.severity).unwrap_or(false)
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
        if let Err(e) = self.listener.log_many(&mut log_messages.iter_mut().map(|x| &mut **x)) {
            if e.is_closed() {
                ListenerStatus::Stale
            } else {
                eprintln!("Error calling listener: {:?}", e);
                ListenerStatus::Fine
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
            if e.is_closed() {
                ListenerStatus::Stale
            } else {
                eprintln!("Error calling listener: {:?}", e);
                ListenerStatus::Fine
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
struct IterMut<'a, T> {
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
        MemoryBoundedBuffer { inner: VecDeque::new(), capacity: capacity, total_size: 0 }
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

    pub fn iter_mut(&mut self) -> IterMut<'_, T> {
        IterMut { inner: self.inner.iter_mut() }
    }
}

struct LogManagerShared {
    listeners: Vec<ListenerWrapper>,
    log_msg_buffer: MemoryBoundedBuffer<LogMessage>,
}

#[derive(Clone)]
pub struct LogManager {
    shared_members: Arc<Mutex<LogManagerShared>>,
}

impl LogManager {
    pub fn new() -> Self {
        Self {
            shared_members: Arc::new(Mutex::new(LogManagerShared {
                listeners: Vec::new(),
                log_msg_buffer: MemoryBoundedBuffer::new(OLD_MSGS_BUF_SIZE),
            })),
        }
    }

    pub fn spawn_klogger(&self) -> Result<(), Error> {
        let mut kernel_logger =
            klogger::KernelLogger::create().context("failed to read kernel logs")?;

        let mut itr = kernel_logger.log_stream();
        while let Some(res) = itr.next() {
            match res {
                Ok((log_msg, size)) => self.process_log(log_msg, size),
                Err(e) => {
                    println!("encountered an error from the kernel log iterator: {}", e);
                    break;
                }
            }
        }

        let klog_stream = klogger::listen(kernel_logger);
        let manager = self.clone();
        fasync::spawn(
            klog_stream
                .map_ok(move |(log_msg, size)| {
                    manager.process_log(log_msg, size);
                })
                .try_collect::<()>()
                .unwrap_or_else(|e| eprintln!("failed to read kernel logs: {:?}", e)),
        );
        Ok(())
    }
    pub fn spawn_log_manager(&self, stream: LogRequestStream) {
        let state = Arc::new(self.clone());
        fasync::spawn(
            stream
                .map_ok(move |req| {
                    let state = state.clone();
                    match req {
                        LogRequest::Listen { log_listener, options, .. } => {
                            state.pump_messages(log_listener, options, false)
                        }
                        LogRequest::DumpLogs { log_listener, options, .. } => {
                            state.pump_messages(log_listener, options, true)
                        }
                    }
                })
                .try_collect::<()>()
                .unwrap_or_else(|e| eprintln!("Log manager failed: {:?}", e)),
        )
    }

    fn process_log(&self, mut log_msg: LogMessage, size: usize) {
        let mut shared_members = self.shared_members.lock();
        run_listeners(&mut shared_members.listeners, &mut log_msg);
        shared_members.log_msg_buffer.push(log_msg, size);
    }

    pub fn spawn_log_sink(&self, stream: LogSinkRequestStream) {
        let state = Arc::new(self.clone());
        fasync::spawn(
            stream
                .map_ok(move |req| {
                    let state = state.clone();
                    let LogSinkRequest::Connect { socket, .. } = req;
                    let ls = match logger::LoggerStream::new(socket) {
                        Err(e) => {
                            eprintln!("Logger: Failed to create tokio socket: {:?}", e);
                            // TODO: close channel
                            return;
                        }
                        Ok(ls) => ls,
                    };

                    let f = ls
                        .map_ok(move |(log_msg, size)| {
                            state.process_log(log_msg, size);
                        })
                        .try_collect::<()>();

                    fasync::spawn(f.unwrap_or_else(|e| {
                        eprintln!("Logger: Stream failed {:?}", e);
                    }));
                })
                .try_collect::<()>()
                .unwrap_or_else(|e| eprintln!("Log sink failed: {:?}", e)),
        )
    }

    fn pump_messages(
        &self,
        log_listener: ClientEnd<LogListenerMarker>,
        options: Option<Box<LogFilterOptions>>,
        dump_logs: bool,
    ) {
        let ll = match log_listener.into_proxy() {
            Ok(ll) => ll,
            Err(e) => {
                eprintln!("Logger: Error getting listener proxy: {:?}", e);
                // TODO: close channel
                return;
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
                return;
            }
            for tag in &lw.tags {
                if tag.len() > fidl_fuchsia_logger::MAX_TAG_LEN_BYTES as usize {
                    // TODO: close channel
                    return;
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
        let mut shared_members = self.shared_members.lock();
        {
            let mut log_length = 0;
            let mut v = vec![];
            for (msg, s) in shared_members.log_msg_buffer.iter_mut() {
                if lw.filter(msg) {
                    if log_length + s > fidl_fuchsia_logger::MAX_LOG_MANY_SIZE_BYTES as usize {
                        if ListenerStatus::Fine != lw.send_filtered_logs(&mut v) {
                            return;
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
                    return;
                }
            }
        }

        if !dump_logs {
            shared_members.listeners.push(lw);
        } else {
            let _ = lw.listener.done();
        }
    }
}

fn run_listeners(listeners: &mut Vec<ListenerWrapper>, log_message: &mut LogMessage) {
    listeners.retain(|l| l.send_log(log_message) == ListenerStatus::Fine);
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::logs::logger::fx_log_packet_t,
        fidl_fuchsia_logger::{LogFilterOptions, LogMarker, LogProxy, LogSinkMarker, LogSinkProxy},
        fuchsia_zircon as zx,
        std::collections::HashSet,
        validating_log_listener::{validate_log_dump, validate_log_stream},
    };

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

    #[fasync::run_singlethreaded(test)]
    async fn test_log_manager_simple() {
        TestHarness::new().manager_test(false).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_log_manager_dump() {
        TestHarness::new().manager_test(true).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_filter_by_pid() {
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
        let options = LogFilterOptions {
            filter_by_pid: true,
            pid: 1,
            filter_by_tid: false,
            tid: 0,
            min_severity: LogLevelFilter::None,
            verbosity: 0,
            tags: vec![],
        };

        TestHarness::new()
            .filter_test(vec![lm].into_iter().collect(), vec![p, p2], Some(options))
            .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_filter_by_tid() {
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
        let options = LogFilterOptions {
            filter_by_pid: false,
            pid: 1,
            filter_by_tid: true,
            tid: 1,
            min_severity: LogLevelFilter::None,
            verbosity: 0,
            tags: vec![],
        };

        TestHarness::new()
            .filter_test(vec![lm].into_iter().collect(), vec![p, p2], Some(options))
            .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_filter_by_min_severity() {
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
        let options = LogFilterOptions {
            filter_by_pid: false,
            pid: 1,
            filter_by_tid: false,
            tid: 1,
            min_severity: LogLevelFilter::Error,
            verbosity: 0,
            tags: vec![],
        };

        TestHarness::new()
            .filter_test(vec![lm].into_iter().collect(), vec![p, p2], Some(options))
            .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_filter_by_combination() {
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
        let options = LogFilterOptions {
            filter_by_pid: true,
            pid: 0,
            filter_by_tid: false,
            tid: 1,
            min_severity: LogLevelFilter::Error,
            verbosity: 0,
            tags: vec![],
        };

        TestHarness::new()
            .filter_test(vec![lm].into_iter().collect(), vec![p, p2, p3], Some(options))
            .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_filter_by_tags() {
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
        let options = LogFilterOptions {
            filter_by_pid: false,
            pid: 1,
            filter_by_tid: false,
            tid: 1,
            min_severity: LogLevelFilter::None,
            verbosity: 0,
            tags: vec![String::from("BBBBB"), String::from("DDDDD")],
        };

        TestHarness::new()
            .filter_test(vec![lm1, lm2].into_iter().collect(), vec![p, p2], Some(options))
            .await;
    }

    struct TestHarness {
        log_proxy: LogProxy,
        log_sink_proxy: LogSinkProxy,
        sin: zx::Socket,
    }

    impl TestHarness {
        fn new() -> Self {
            let (sin, sout) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
            let shared_members = Arc::new(Mutex::new(LogManagerShared {
                listeners: Vec::new(),
                log_msg_buffer: MemoryBoundedBuffer::new(OLD_MSGS_BUF_SIZE),
            }));

            let lm = LogManager { shared_members: shared_members };

            let (log_proxy, log_stream) =
                fidl::endpoints::create_proxy_and_stream::<LogMarker>().unwrap();
            lm.spawn_log_manager(log_stream);

            let (log_sink_proxy, log_sink_stream) =
                fidl::endpoints::create_proxy_and_stream::<LogSinkMarker>().unwrap();
            lm.spawn_log_sink(log_sink_stream);

            log_sink_proxy.connect(sout).expect("unable to connect out socket to log sink");

            Self { log_proxy, log_sink_proxy, sin }
        }

        async fn filter_test(
            self,
            expected: HashSet<LogMessage>,
            packets: Vec<fx_log_packet_t>,
            filter_options: Option<LogFilterOptions>,
        ) {
            for mut p in packets {
                self.sin.write(to_u8_slice(&mut p)).unwrap();
            }

            validate_log_stream(expected, self.log_proxy, filter_options).await;
        }

        async fn manager_test(self, test_dump_logs: bool) {
            let mut p = setup_default_packet();
            self.sin.write(to_u8_slice(&mut p)).unwrap();

            p.metadata.severity = LogLevelFilter::Info.into_primitive().into();
            self.sin.write(to_u8_slice(&mut p)).unwrap();

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

            p.metadata.pid = 2;
            self.sin.write(to_u8_slice(&mut p)).unwrap();

            if test_dump_logs {
                validate_log_dump(vec![lm1, lm2, lm3], self.log_proxy, None).await;
            } else {
                validate_log_stream(vec![lm1, lm2, lm3], self.log_proxy, None).await;
            }
        }
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
        x[offset..(offset + size)].iter_mut().for_each(|x| *x = value);
    }
}
