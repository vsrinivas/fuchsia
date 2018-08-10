// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use Never;
use fidl_stats::IfaceStats;
use future_util::GroupAvailableExt;
use futures::channel::{oneshot, mpsc};
use futures::prelude::*;
use parking_lot::Mutex;
use std::sync::Arc;
use zx;

// TODO(gbonik): get rid of the Mutex when FIDL APIs make it possible
// Mutex is a workaround for the fact that Responder.send() takes a &mut.
pub type StatsRef = Arc<Mutex<IfaceStats>>;

type Responder = oneshot::Sender<StatsRef>;

pub struct StatsScheduler {
    queue: mpsc::UnboundedSender<Responder>
}

pub fn create_scheduler()
    -> (StatsScheduler, impl Stream<Item = StatsRequest>)
{
    let (sender, receiver) = mpsc::unbounded();
    let scheduler = StatsScheduler{ queue: sender };
    let req_stream = receiver
        .map(Ok).group_available()
        .map_ok(StatsRequest)
        .map(|x| x.unwrap_or_else(Never::into_any));
    (scheduler, req_stream)
}

impl StatsScheduler {
    pub fn get_stats(&self)
        -> impl Future<Output = Result<StatsRef, zx::Status>>
    {
        let (sender, receiver) = oneshot::channel();
        // Ignore the error: if the other side is closed, `sender` will be immediately
        // dropped, and `receiver` will be notified
        self.queue.unbounded_send(sender).unwrap_or_else(|_| ());
        receiver.map_err(|_| zx::Status::CANCELED)
    }
}

pub struct StatsRequest(Vec<Responder>);

impl StatsRequest {
    pub fn reply(self, stats: IfaceStats) {
        let stats = Arc::new(Mutex::new(stats));
        for responder in self.0 {
            responder.send(stats.clone()).unwrap_or_else(|_| ());
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use async;
    use fidl_stats::{Counter, DispatcherStats, IfaceStats, PacketCounter};

    #[test]
    fn schedule() {
        let mut exec = async::Executor::new().expect("Failed to create an executor");

        let (sched, req_stream) = create_scheduler();
        let mut fut1 = sched.get_stats();
        let mut fut2 = sched.get_stats();

        let mut counter = 0;
        let mut server = req_stream.for_each(move |req| {
            counter += 100;
            future::ready(req.reply(fake_iface_stats(counter)))
        });
        exec.run_until_stalled(&mut server);

        let res1 = exec.run_until_stalled(&mut fut1);
        match res1 {
            Poll::Ready(Ok(r)) => assert_eq!(fake_iface_stats(100), *r.lock()),
            Poll::Ready(Err(e)) => panic!("request future 1 returned an error: {:?}", e),
            Poll::Pending => panic!("request future 1 returned 'Pending'")
        }

        let res2 = exec.run_until_stalled(&mut fut2);
        match res2 {
            Poll::Ready(Ok(r)) => assert_eq!(fake_iface_stats(100), *r.lock()),
            Poll::Ready(Err(e)) => panic!("request future 2 returned an error: {:?}", e),
            Poll::Pending => panic!("request future 2 returned 'Pending'")
        }

        let mut fut3 = sched.get_stats();
        exec.run_until_stalled(&mut server);
        let res3 = exec.run_until_stalled(&mut fut3);
        match res3 {
            Poll::Ready(Ok(r)) => assert_eq!(fake_iface_stats(200), *r.lock()),
            Poll::Ready(Err(e)) => panic!("request future 3 returned an error: {:?}", e),
            Poll::Pending => panic!("request future 3 returned 'Pending'")
        }
    }

    #[test]
    fn canceled_if_server_dropped_after_request() {
        let mut exec = async::Executor::new().expect("Failed to create an executor");

        let (sched, req_stream) = create_scheduler();
        let mut fut = sched.get_stats();

        ::std::mem::drop(req_stream);

        let res = exec.run_until_stalled(&mut fut);
        if let Poll::Ready(Err(zx::Status::CANCELED)) = res {
            // OK
        } else {
            panic!("canceled error not found");
        }
    }

    #[test]
    fn canceled_if_server_dropped_before_request() {
        let mut exec = async::Executor::new().expect("Failed to create an executor");

        let (sched, req_stream) = create_scheduler();
        ::std::mem::drop(req_stream);

        let mut fut = sched.get_stats();

        let res = exec.run_until_stalled(&mut fut);
        if let Poll::Ready(Err(zx::Status::CANCELED)) = res {
            // OK
        } else {
            panic!("no cancelled error");
        }
    }

    fn fake_iface_stats(count: u64) -> IfaceStats {
        IfaceStats {
            dispatcher_stats: DispatcherStats {
                any_packet: fake_packet_counter(count),
                mgmt_frame: fake_packet_counter(count),
                ctrl_frame: fake_packet_counter(count),
                data_frame: fake_packet_counter(count),
            },
            mlme_stats: None,
        }
    }

    fn fake_packet_counter(count: u64) -> PacketCounter {
        PacketCounter {
            in_: fake_counter(count),
            out: fake_counter(count),
            drop: fake_counter(count),
        }
    }

    fn fake_counter(count: u64) -> Counter {
        Counter {
            count: count,
            name: "foo".to_string(),
        }
    }

}
