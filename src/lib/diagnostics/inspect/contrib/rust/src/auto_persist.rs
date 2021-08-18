// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_diagnostics_persist::PersistResult,
    fuchsia_zircon::{self as zx, prelude::*},
    futures::{channel::mpsc, Future, StreamExt},
    injectable_time::{MonotonicTime, TimeSource},
    std::{
        ops::{Deref, DerefMut},
        sync::{
            atomic::{AtomicBool, Ordering},
            Arc,
        },
    },
    tracing::{error, info},
};

pub type PersistenceReqSender = mpsc::Sender<String>;

/// Wrapper around an Inspect node T so that after the node is accessed (and written to),
/// the corresponding Data Persistence tag would be sent through a channel so that it
/// can be forwarded to the Data Persistence Service.
pub struct AutoPersist<T> {
    inspect_node: T,
    persistence_tag: String,
    persistence_req_sender: PersistenceReqSender,
    sender_is_blocked: Arc<AtomicBool>,
}

impl<T> AutoPersist<T> {
    pub fn new(
        inspect_node: T,
        persistence_tag: &str,
        persistence_req_sender: PersistenceReqSender,
    ) -> Self {
        Self {
            inspect_node,
            persistence_tag: persistence_tag.to_string(),
            persistence_req_sender,
            sender_is_blocked: Arc::new(AtomicBool::new(false)),
        }
    }

    /// Return a guard that derefs to `inspect_node`. When the guard is dropped,
    /// `persistence_tag` is sent via the `persistence_req_sender`.
    pub fn get_mut(&mut self) -> AutoPersistGuard<'_, T> {
        AutoPersistGuard {
            inspect_node: &mut self.inspect_node,
            persistence_tag: &self.persistence_tag,
            persistence_req_sender: &mut self.persistence_req_sender,
            sender_is_blocked: Arc::clone(&self.sender_is_blocked),
        }
    }
}

pub struct AutoPersistGuard<'a, T> {
    inspect_node: &'a mut T,
    persistence_tag: &'a str,
    persistence_req_sender: &'a mut PersistenceReqSender,
    sender_is_blocked: Arc<AtomicBool>,
}

impl<'a, T> Deref for AutoPersistGuard<'a, T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        self.inspect_node
    }
}

impl<'a, T> DerefMut for AutoPersistGuard<'a, T> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        self.inspect_node
    }
}

impl<'a, T> Drop for AutoPersistGuard<'a, T> {
    fn drop(&mut self) {
        if self.persistence_req_sender.try_send(self.persistence_tag.to_string()).is_err() {
            // If sender has not been blocked before, set bool to true and log error message
            if let Ok(_) = self.sender_is_blocked.compare_exchange(
                false,
                true,
                Ordering::SeqCst,
                Ordering::SeqCst,
            ) {
                error!("PersistenceReqSender dropped a persistence request: either buffer is full or no receiver is waiting");
            }
        } else {
            // If sender has been blocked before, set bool to false and log message
            if let Ok(_) = self.sender_is_blocked.compare_exchange(
                true,
                false,
                Ordering::SeqCst,
                Ordering::SeqCst,
            ) {
                info!("PersistenceReqSender recovered and resumed sending");
            }
        }
    }
}

fn log_at_most_once_per_min_factory(
    time_source: impl TimeSource,
    mut log_fn: impl FnMut(String),
) -> impl FnMut(String) {
    let mut last_logged = None;
    move |message| {
        let now = zx::Time::from_nanos(time_source.now());
        let should_log = match last_logged {
            Some(last_logged) => (now - last_logged) >= 1.minutes(),
            None => true,
        };
        if should_log {
            log_fn(message);
            last_logged.replace(now);
        }
    }
}

// arbitrary value
const DEFAULT_BUFFER_SIZE: usize = 100;

/// Create a sender for sending Persistence tag, and a Future representing a sending thread
/// that forwards that tag to the Data Persistence service.
///
/// If the sending thread fails to forward a tag, or the Persistence Service returns an error
/// code, an error will be logged. However, an error is only logged at most once per minute
/// to avoid log spam.
pub fn create_persistence_req_sender(
    persistence_proxy: fidl_fuchsia_diagnostics_persist::DataPersistenceProxy,
) -> (PersistenceReqSender, impl Future<Output = ()>) {
    let (sender, mut receiver) = mpsc::channel::<String>(DEFAULT_BUFFER_SIZE);
    let fut = async move {
        let persistence_proxy = persistence_proxy.clone();
        let mut log_error =
            log_at_most_once_per_min_factory(MonotonicTime::new(), |e| error!("{}", e));
        while let Some(tag_name) = receiver.next().await {
            let resp = persistence_proxy.persist(&tag_name).await;
            match resp {
                Ok(PersistResult::Queued) => continue,
                Ok(other) => log_error(format!(
                    "Persistence Service returned an error for tag {}: {:?}",
                    tag_name, other
                )),
                Err(e) => log_error(format!(
                    "Failed to send request to Persistence Service for tag {}: {}",
                    tag_name, e
                )),
            }
        }
    };
    (sender, fut)
}

#[cfg(test)]
mod tests {
    use {
        super::*, fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_diagnostics_persist::DataPersistenceRequest, fuchsia_async as fasync,
        fuchsia_inspect::Inspector, futures::task::Poll, pin_utils::pin_mut, std::cell::RefCell,
    };

    #[test]
    fn test_auto_persist() {
        let (sender, mut receiver) = mpsc::channel::<String>(100);
        let inspector = Inspector::new();
        let node = inspector.root().create_child("node");
        let mut auto_persist_node = AutoPersist::new(node, "some-tag", sender);

        // There should be no message on the receiver end yet
        assert!(receiver.try_next().is_err());

        {
            let _guard = auto_persist_node.get_mut();
        }

        match receiver.try_next() {
            Ok(Some(tag)) => assert_eq!(tag, "some-tag"),
            _ => panic!("expect message in receiver"),
        }
    }

    #[test]
    fn test_create_persistence_req_sender() {
        let mut exec = fasync::TestExecutor::new().expect("creating executor should succeed");
        let (persistence_proxy, mut persistence_stream) =
            create_proxy_and_stream::<fidl_fuchsia_diagnostics_persist::DataPersistenceMarker>()
                .expect("creating persistence proxy and stream should succeed");
        let (mut req_sender, req_forwarder_fut) = create_persistence_req_sender(persistence_proxy);

        pin_mut!(req_forwarder_fut);

        // Nothing has happened yet, so these futures should be Pending
        match exec.run_until_stalled(&mut req_forwarder_fut) {
            Poll::Pending => (),
            other => panic!("unexpected variant: {:?}", other),
        };
        match exec.run_until_stalled(&mut persistence_stream.next()) {
            Poll::Pending => (),
            other => panic!("unexpected variant: {:?}", other),
        };

        assert!(req_sender.try_send("some-tag".to_string()).is_ok());

        // req_forwarder_fut still Pending because it's a loop
        match exec.run_until_stalled(&mut req_forwarder_fut) {
            Poll::Pending => (),
            other => panic!("unexpected variant: {:?}", other),
        };
        // There should be a message in the stream now
        match exec.run_until_stalled(&mut persistence_stream.next()) {
            Poll::Ready(Some(Ok(DataPersistenceRequest::Persist { tag, .. }))) => {
                assert_eq!(tag, "some-tag")
            }
            other => panic!("unexpected variant: {:?}", other),
        };
    }

    struct FakeTimeSource {
        now: Arc<RefCell<zx::Time>>,
    }

    impl TimeSource for FakeTimeSource {
        fn now(&self) -> i64 {
            self.now.borrow().into_nanos()
        }
    }

    #[test]
    fn test_log_at_most_once_per_min_factory() {
        let log_count = Arc::new(RefCell::new(0));
        let now = Arc::new(RefCell::new(zx::Time::from_nanos(0)));
        let fake_time_source = FakeTimeSource { now: now.clone() };
        let mut log =
            log_at_most_once_per_min_factory(fake_time_source, |_| *log_count.borrow_mut() += 1);

        log("message 1".to_string());
        assert_eq!(*log_count.borrow(), 1);

        // No time has passed, so log_count shouldn't increase
        log("message 2".to_string());
        assert_eq!(*log_count.borrow(), 1);

        {
            let mut now = now.borrow_mut();
            *now = now.saturating_add(30.seconds());
        }

        // Not enough time has passed, so log_count shouldn't increase
        log("message 3".to_string());
        assert_eq!(*log_count.borrow(), 1);

        {
            let mut now = now.borrow_mut();
            *now = now.saturating_add(30.seconds());
        }

        // Enough time has passed, so log_count should increase
        log("message 3".to_string());
        assert_eq!(*log_count.borrow(), 2);
    }
}
