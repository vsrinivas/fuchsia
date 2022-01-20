// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{Canceled, ResponseHandler};
use crate::AsyncCondition;
use core::fmt::Debug;
use parking_lot::Mutex;
use static_assertions::const_assert;
use std::num::NonZeroU8;
use std::sync::{Arc, Weak};

/// A type for keeping track of expected responses to
/// Spinel requests. Also in charge of TID allocation.
///
/// This type is used by [`FrameHandler`], which also
/// handles transmission.
///
/// # General Usage
///
/// Before a Spinel command is sent to the device, the
/// [`ResponseHandler`] associated with it is registered
/// using the `register_handler()` method. That method
/// is asynchronous---if there are no available TIDs,
/// it will block until one becomes available. The TID
/// is returned by `register_handler()`.
pub struct RequestTracker {
    /// Mutex-protected array keeping track of outstanding
    /// transactions. TID zero is not used, so the indexes
    /// are offset by one. (index = tid - 1)
    ///
    /// Each slot has an optional weak reference to a
    /// mutex-protected [`ResponseHandler`]. A given "slot"
    /// is considered open if the option is `None`. TIDs
    /// are allocated from open slots.
    requests: Mutex<[Option<Weak<Mutex<dyn ResponseHandler>>>; Self::MAX_REQUESTS as usize]>,

    /// An asynchronous condition that is triggered whenever
    /// a TID is freed up for re-use. This will unblock any pending
    /// calls to `register_handler()`.
    condition: AsyncCondition,
}

impl Debug for RequestTracker {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("RequestTracker")
            .field("requests", &()) // TODO: May want to actually dump transactions
            .field("condition", &self.condition)
            .finish()
    }
}

impl Default for RequestTracker {
    fn default() -> RequestTracker {
        RequestTracker { requests: Default::default(), condition: Default::default() }
    }
}

// Constrain MAX_REQUESTS to be larger than or equal to 1.
const_assert!(RequestTracker::MAX_REQUESTS >= 1);

// Constrain MAX_REQUESTS to be less than or equal to 15.
// This is a Spinel protocol limitation.
const_assert!(RequestTracker::MAX_REQUESTS <= 15);

impl RequestTracker {
    /// The maximum number of in-flight requests.
    ///
    /// This value MUST be between 1 and 15, inclusive.
    const MAX_REQUESTS: u8 = 4;

    /// Registers a response handler for a future response, returning
    /// a TID.
    ///
    /// This will block if all request slots are taken.
    pub async fn register_handler(&self, handler: &Arc<Mutex<dyn ResponseHandler>>) -> NonZeroU8 {
        loop {
            {
                let mut requests = self.requests.lock();

                // First check for empty slots.
                for (i, item) in requests.iter_mut().enumerate() {
                    if item.is_none() {
                        *item = Some(Arc::downgrade(handler));

                        // SAFETY: `i` is used +1 and `i` will never be 255,
                        //         so it will never roll-over.
                        unsafe {
                            // Safety-specific compile-time constraint.
                            // This constraint should be kept here, even if
                            // redundant, to make it clear that safety is
                            // dependent on this assertion.
                            const_assert!(RequestTracker::MAX_REQUESTS < 255);

                            return NonZeroU8::new_unchecked(i as u8 + 1);
                        }
                    }
                }
            }

            // All slots are currently allocated. Wait for one to free up.
            self.condition.wait().await;
        }
    }

    /// Extracts the response handler associated with
    /// the given TID, if any.
    pub fn retrieve_handler(&self, tid: NonZeroU8) -> Option<Arc<Mutex<dyn ResponseHandler>>> {
        // Minus 1 because we never use TID zero
        let i = tid.get() as usize - 1;

        // Extract our handler from our list.
        let ret = {
            let mut requests = self.requests.lock();
            match requests.get_mut(i as usize) {
                Some(x) => x.take(),
                None => return None,
            }
        }
        .and_then(|x| x.upgrade());

        if ret.is_some() {
            // If we successfully extracted our handler, that means
            // this TID has been freed up! Trigger the condition so
            // that anyone who is waiting on a TID can use it.
            self.condition.trigger();
        }

        ret
    }

    /// This method is called whenever the device resets.
    pub fn clear(&self) {
        let mut requests = self.requests.lock();
        for request in requests.iter_mut() {
            request
                .take()
                .and_then(|x| x.upgrade())
                .and_then(|x| x.lock().on_response(Err(Canceled)).ok());
        }
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;
    use crate::spinel::*;
    use assert_matches::assert_matches;
    use futures::prelude::*;
    use futures::task::Poll;

    #[test]
    fn test_request_tracker_cancel() {
        let request_tracker = RequestTracker::default();

        let flag = Arc::new(Mutex::new(false));
        let flag_copy = flag.clone();

        // Create our response handler as a closure. This is invoked whenever
        // we get a response or the transaction is cancelled.
        //
        // This works because closures with this signature have a blanket
        // implementation of the `ResponseHandler` trait.
        let handler = move |response: Result<SpinelFrameRef<'_>, Canceled>| {
            println!("Response handler invoked with {:?}", response);
            assert_matches!(response, Err(Canceled));
            *flag_copy.lock() = true;
            Ok(())
        };

        // Simultaneously put our closure in an ArcMutex and cast it
        // as a `dyn ResponseHandler`. This will be held onto by
        // the enclosing future.
        let handler = Arc::new(Mutex::new(Some(handler))) as Arc<Mutex<dyn ResponseHandler>>;

        // Register our response handler with the request tracker,
        // giving us our TID.
        let tid = request_tracker.register_handler(&handler).now_or_never().unwrap();

        request_tracker.clear();

        assert_eq!(*flag.lock(), true);

        assert!(request_tracker.retrieve_handler(tid).is_none());
    }

    #[test]
    fn test_request_tracker_retrieve_handler() {
        let request_tracker = RequestTracker::default();

        let flag = Arc::new(Mutex::new(false));
        let flag_copy = flag.clone();

        // Create our response handler as a closure. This is invoked whenever
        // we get a response or the transaction is cancelled.
        //
        // This works because closures with this signature have a blanket
        // implementation of the `ResponseHandler` trait.
        let handler = move |response: Result<SpinelFrameRef<'_>, Canceled>| {
            println!("Response handler invoked with {:?}", response);
            assert_matches!(response, Err(Canceled));
            *flag_copy.lock() = true;
            Ok(())
        };

        // Simultaneously put our closure in an ArcMutex and cast it
        // as a `dyn ResponseHandler`. This will be held onto by
        // the enclosing future.
        let handler = Arc::new(Mutex::new(Some(handler))) as Arc<Mutex<dyn ResponseHandler>>;

        // Register our response handler with the request tracker,
        // giving us our TID.
        let tid = request_tracker.register_handler(&handler).now_or_never().unwrap();

        let handler = request_tracker.retrieve_handler(tid);

        assert!(handler.is_some());

        let handler = handler.unwrap();

        // This is a separate line from above to
        // keep the type in scope.
        let mut handler = handler.lock();

        assert_matches!(handler.on_response(Err(Canceled)), Ok(()));

        assert_eq!(*flag.lock(), true);

        assert!(request_tracker.retrieve_handler(tid).is_none());
    }

    #[test]
    fn test_request_tracker_max_requests() {
        let noop_waker = futures::task::noop_waker();
        let mut noop_context = futures::task::Context::from_waker(&noop_waker);

        let request_tracker = RequestTracker::default();

        let handler = move |_response: Result<SpinelFrameRef<'_>, Canceled>| Ok(());
        let handler = Arc::new(Mutex::new(Some(handler))) as Arc<Mutex<dyn ResponseHandler>>;

        // This part assumes MAX_REQUESTS==4.
        // If that changes, update this test.
        const_assert!(RequestTracker::MAX_REQUESTS == 4);
        let tid1 = request_tracker.register_handler(&handler).now_or_never().unwrap();
        let tid2 = request_tracker.register_handler(&handler).now_or_never().unwrap();
        let tid3 = request_tracker.register_handler(&handler).now_or_never().unwrap();
        let tid4 = request_tracker.register_handler(&handler).now_or_never().unwrap();

        assert_ne!(tid1, tid2);
        assert_ne!(tid2, tid3);
        assert_ne!(tid3, tid4);

        let mut future_tid = request_tracker.register_handler(&handler).boxed();

        assert_eq!(future_tid.poll_unpin(&mut noop_context), Poll::Pending);

        assert!(request_tracker.retrieve_handler(tid1).is_some());

        assert_matches!(future_tid.poll_unpin(&mut noop_context), Poll::Ready(_));
    }
}
