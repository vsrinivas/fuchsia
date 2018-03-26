// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![feature(const_size_of)]
#![feature(conservative_impl_trait)]

extern crate byteorder;
extern crate failure;
extern crate fidl;
extern crate fidl_logger;
extern crate fuchsia_app as app;
extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;
extern crate libc;
extern crate parking_lot;

#[macro_use]
extern crate futures;

use app::server::ServicesServer;
use failure::{Error, ResultExt};
use fidl::endpoints2::ServiceMarker;
use futures::future::ok as fok;
use futures::FutureExt;
use std::collections::HashSet;
use std::collections::VecDeque;
use std::sync::Arc;
use parking_lot::Mutex;
use futures::StreamExt;

use fidl_logger::{
    Log,
    LogImpl,
    LogMarker,
    LogListenerProxy,
    LogMessage,
    LogSink,
    LogSinkImpl,
    LogSinkMarker,
};

pub mod logger;

// Store 1000 log messages and delete on FIFO basis.
const OLD_MSGS_BUF_SIZE: usize = 1000;

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
    fn send_log(&self, log_message: &mut LogMessage) -> ListenerStatus {
        if self.pid.map(|pid| pid != log_message.pid).unwrap_or(false)
            || self.tid.map(|tid| tid != log_message.tid).unwrap_or(false)
            || self.min_severity
                .map(|min_sev| min_sev > log_message.severity)
                .unwrap_or(false)
        {
            return ListenerStatus::Fine;
        }

        if self.tags.len() != 0 {
            if !log_message.tags.iter().any(|tag| self.tags.contains(tag)) {
                return ListenerStatus::Fine;
            }
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

struct LogManagerShared {
    listeners: Vec<ListenerWrapper>,
    log_msg_buffer: VecDeque<LogMessage>,
}

#[derive(Clone)]
struct LogManager {
    shared_members: Arc<Mutex<LogManagerShared>>,
}

fn run_listeners(listeners: &mut Vec<ListenerWrapper>, log_message: &mut LogMessage) {
    listeners.retain(|l| l.send_log(log_message) == ListenerStatus::Fine);
}

fn spawn_log_manager(state: LogManager, chan: async::Channel) {
    async::spawn(LogImpl {
        state,
        listen: |state, log_listener, options, _controller| {
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
                if lw.tags.len() > logger::FX_LOG_MAX_TAGS {
                    // TODO: close channel
                    return fok(());
                }
                for tag in &lw.tags {
                    if tag.len() > logger::FX_LOG_MAX_TAG_LEN - 1 {
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
                if options.filter_by_severity {
                    lw.min_severity = Some(options.min_severity)
                }
            }

            let mut shared_members = state.shared_members.lock();
            for msg in &mut shared_members.log_msg_buffer {
                if ListenerStatus::Fine != lw.send_log(msg) {
                    return fok(());
                }
            }
            shared_members.listeners.push(lw);

            fok(())
        }
    }
    .serve(chan)
    .recover(|e| eprintln!("Log manager failed: {:?}", e)))
}

fn spawn_log_sink(state: LogManager, chan: async::Channel) {
    async::spawn(LogSinkImpl {
        state,
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
            let f = ls.for_each(move |mut log_msg| {
                let mut shared_members = shared_members.lock();
                run_listeners(&mut shared_members.listeners, &mut log_msg);
                shared_members.log_msg_buffer.push_front(log_msg);
                shared_members.log_msg_buffer.truncate(OLD_MSGS_BUF_SIZE);
                Ok(())
            }).map(|_s| ());

            async::spawn(f.recover(|e| {
                eprintln!("Logger: Stream failed {:?}", e);
            }));

            fok(())
        }
    }
    .serve(chan)
    .recover(|e| eprintln!("Log sink failed: {:?}", e)))
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
        log_msg_buffer: VecDeque::with_capacity(OLD_MSGS_BUF_SIZE),
    }));
    let shared_members_clone = shared_members.clone();
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

    Ok(executor.run(server_fut, 2).context("running server").map(|_| ())?) // 2 threads
}

#[cfg(test)]
mod tests {
    use super::*;

    use fidl_logger::{
        LogProxy,
        LogSinkProxy,
        LogFilterOptions,
        LogListenerImpl,
        LogListener,
        LogListenerMarker,
    };
    use logger::fx_log_packet_t;
    use zx::prelude::*;

    const FX_LOG_INFO: i32 = 0;
    const FX_LOG_WARNING: i32 = 1;
    const FX_LOG_ERROR: i32 = 2;

    struct LogListenerState {
        expected: Vec<LogMessage>,
        done: Arc<Mutex<bool>>,
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
        async::spawn(LogListenerImpl {
            state: ll,
            log: |ll, msg, _controller| {
                let len = ll.expected.len();
                assert_ne!(len, 0, "got extra message: {:?}", msg);
                // we can receive messages out of order
                ll.expected.retain(|e| e != &msg);
                assert_eq!(ll.expected.len(), len-1, "expected: {:?},\nmsg: {:?}", ll.expected, msg);
                if ll.expected.len() == 0 {
                    *ll.done.lock() = true;
                }
                fok(())
            },
        }
        .serve(chan)
        .recover(|e| panic!("test fail {:?}", e)))
    }

    fn setup_listener(
        ll: LogListenerState,
        lp: LogProxy,
        mut filter_options: Option<Box<LogFilterOptions>>,
    ) {
        let (remote, local) = zx::Channel::create().expect("failed to create zx channel");
        let mut remote_ptr = fidl::endpoints2::ClientEnd::<LogListenerMarker>::new(remote);
        let local = async::Channel::from_channel(local).expect("failed to make async channel");
        spawn_log_listener(ll, local);
        lp.listen(&mut remote_ptr, &mut filter_options).expect("failed to register listener");
    }

    fn setup_test() -> (async::Executor, LogProxy, LogSinkProxy, zx::Socket, zx::Socket) {
        let executor = async::Executor::new().expect("unable to create executor");
        let (sin, sout) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        let shared_members = Arc::new(Mutex::new(LogManagerShared {
            listeners: Vec::new(),
            log_msg_buffer: VecDeque::new(),
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
        p.metadata.severity = FX_LOG_WARNING;
        p.metadata.dropped_logs = 2;
        p.data[0] = 5;
        memset(&mut p.data[..], 1, 65, 5);
        memset(&mut p.data[..], 7, 66, 5);
        return p;
    }

    #[test]
    fn test_log_manager() {
        let (mut executor, log_proxy, log_sink_proxy, sin, mut sout) = setup_test();
        let mut p = setup_default_packet();
        sin.write(to_u8_slice(&mut p)).unwrap();
        log_sink_proxy.connect(&mut sout).expect("unable to connect");
        p.metadata.severity = FX_LOG_INFO;
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
        lm1.severity = FX_LOG_WARNING;
        let mut lm3 = copy_log_message(&lm2);
        lm3.pid = 2;
        let done = Arc::new(Mutex::new(false));
        let ls = LogListenerState {
            expected: vec![lm1, lm2, lm3],
            done: done.clone(),
        };
        setup_listener(ls, log_proxy, None);

        p.metadata.pid = 2;
        sin.write(to_u8_slice(&mut p)).unwrap();

        let tries = 10;
        for _ in 0..tries {
            if *done.lock() {
                break;
            }
            let timeout = async::Timer::<()>::new(10.millis().after_now()).unwrap();
            executor.run(timeout, 2).unwrap();
        }
        assert!(*done.lock(), "task should have completed by now");
    }

    fn filter_test_helper(
        expected: Vec<LogMessage>,
        packets: Vec<fx_log_packet_t>,
        filter_options: Option<Box<LogFilterOptions>>,
    ) {
        let (mut executor, log_proxy, log_sink_proxy, sin, mut sout) = setup_test();
        log_sink_proxy.connect(&mut sout).expect("unable to connect");
        let done = Arc::new(Mutex::new(false));
        let ls = LogListenerState {
            expected: expected,
            done: done.clone(),
        };
        setup_listener(ls, log_proxy, filter_options);
        for mut p in packets {
            sin.write(to_u8_slice(&mut p)).unwrap();
        }

        let tries = 100;
        for _ in 0..tries {
            if *done.lock() {
                break;
            }
            let timeout = async::Timer::<()>::new(10.millis().after_now()).unwrap();
            executor.run(timeout, 2).unwrap();
        }
        assert!(*done.lock(), "task should have completed by now");
    }

    #[test]
    fn test_filter_by_pid() {
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
        let options = Box::new(LogFilterOptions {
            filter_by_pid: true,
            pid: 1,
            filter_by_tid: false,
            tid: 0,
            filter_by_severity: false,
            min_severity: 0,
            tags: vec![],
        });
        filter_test_helper(vec![lm], vec![p, p2], Some(options));
    }

    #[test]
    fn test_filter_by_tid() {
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
        let options = Box::new(LogFilterOptions {
            filter_by_pid: false,
            pid: 1,
            filter_by_tid: true,
            tid: 1,
            filter_by_severity: false,
            min_severity: 0,
            tags: vec![],
        });
        filter_test_helper(vec![lm], vec![p, p2], Some(options));
    }

    #[test]
    fn test_filter_by_min_severity() {
        let p = setup_default_packet();
        let mut p2 = p.clone();
        p2.metadata.pid = 0;
        p2.metadata.tid = 0;
        p2.metadata.severity = FX_LOG_ERROR;
        let lm = LogMessage {
            pid: p2.metadata.pid,
            tid: p2.metadata.tid,
            time: p2.metadata.time,
            dropped_logs: p2.metadata.dropped_logs,
            severity: p2.metadata.severity,
            msg: String::from("BBBBB"),
            tags: vec![String::from("AAAAA")],
        };
        let options = Box::new(LogFilterOptions {
            filter_by_pid: false,
            pid: 1,
            filter_by_tid: false,
            tid: 1,
            filter_by_severity: true,
            min_severity: FX_LOG_ERROR,
            tags: vec![],
        });
        filter_test_helper(vec![lm], vec![p, p2], Some(options));
    }

    #[test]
    fn test_filter_by_combination() {
        let mut p = setup_default_packet();
        p.metadata.pid = 0;
        p.metadata.tid = 0;
        let mut p2 = p.clone();
        p2.metadata.severity = FX_LOG_ERROR;
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
        let options = Box::new(LogFilterOptions {
            filter_by_pid: true,
            pid: 0,
            filter_by_tid: false,
            tid: 1,
            filter_by_severity: true,
            min_severity: FX_LOG_ERROR,
            tags: vec![],
        });
        filter_test_helper(vec![lm], vec![p, p2, p3], Some(options));
    }

    #[test]
    fn test_filter_by_tags() {
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
        let options = Box::new(LogFilterOptions {
            filter_by_pid: false,
            pid: 1,
            filter_by_tid: false,
            tid: 1,
            filter_by_severity: false,
            min_severity: 0,
            tags: vec![String::from("BBBBB"), String::from("DDDDD")],
        });

        filter_test_helper(vec![lm1, lm2], vec![p, p2], Some(options));
    }
}
