//! Support for creating futures that represent timeouts.
//!
//! This module contains the `Delay` type which is a future that will resolve
//! at a particular point in the future.

use std::fmt;
use std::future::Future;
use std::pin::Pin;
use std::sync::atomic::AtomicUsize;
use std::sync::atomic::Ordering::SeqCst;
use std::sync::{Arc, Mutex};
use std::task::{Context, Poll};
use std::time::{Duration, Instant};

use crate::arc_list::Node;
use crate::AtomicWaker;
use crate::{ScheduledTimer, TimerHandle};

/// A future representing the notification that an elapsed duration has
/// occurred.
///
/// This is created through the `Delay::new` or `Delay::new_at` methods
/// indicating when the future should fire at.  Note that these futures are not
/// intended for high resolution timers, but rather they will likely fire some
/// granularity after the exact instant that they're otherwise indicated to
/// fire at.
pub struct Delay {
    state: Option<Arc<Node<ScheduledTimer>>>,
    when: Instant,
}

impl Delay {
    /// Creates a new future which will fire at `dur` time into the future.
    ///
    /// The returned object will be bound to the default timer for this thread.
    /// The default timer will be spun up in a helper thread on first use.
    #[inline]
    pub fn new(dur: Duration) -> Delay {
        Delay::new_handle(Instant::now() + dur, Default::default())
    }

    /// Return the `Instant` when this delay will fire.
    #[inline]
    pub fn when(&self) -> Instant {
        self.when
    }

    /// Creates a new future which will fire at the time specified by `at`.
    ///
    /// The returned instance of `Delay` will be bound to the timer specified by
    /// the `handle` argument.
    pub(crate) fn new_handle(at: Instant, handle: TimerHandle) -> Delay {
        let inner = match handle.inner.upgrade() {
            Some(i) => i,
            None => {
                return Delay {
                    state: None,
                    when: at,
                }
            }
        };
        let state = Arc::new(Node::new(ScheduledTimer {
            at: Mutex::new(Some(at)),
            state: AtomicUsize::new(0),
            waker: AtomicWaker::new(),
            inner: handle.inner,
            slot: Mutex::new(None),
        }));

        // If we fail to actually push our node then we've become an inert
        // timer, meaning that we'll want to immediately return an error from
        // `poll`.
        if inner.list.push(&state).is_err() {
            return Delay {
                state: None,
                when: at,
            };
        }

        inner.waker.wake();
        Delay {
            state: Some(state),
            when: at,
        }
    }

    /// Resets this timeout to an new timeout which will fire at the time
    /// specified by `at`.
    #[inline]
    pub fn reset(&mut self, at: Instant) {
        self.when = at;
        if self._reset(self.when).is_err() {
            self.state = None
        }
    }

    fn _reset(&mut self, at: Instant) -> Result<(), ()> {
        let state = match self.state {
            Some(ref state) => state,
            None => return Err(()),
        };
        if let Some(timeouts) = state.inner.upgrade() {
            let mut bits = state.state.load(SeqCst);
            loop {
                // If we've been invalidated, cancel this reset
                if bits & 0b10 != 0 {
                    return Err(());
                }
                let new = bits.wrapping_add(0b100) & !0b11;
                match state.state.compare_exchange(bits, new, SeqCst, SeqCst) {
                    Ok(_) => break,
                    Err(s) => bits = s,
                }
            }
            *state.at.lock().unwrap() = Some(at);
            // If we fail to push our node then we've become an inert timer, so
            // we'll want to clear our `state` field accordingly
            timeouts.list.push(state)?;
            timeouts.waker.wake();
        }

        Ok(())
    }
}

impl Future for Delay {
    type Output = ();

    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let state = match self.state {
            Some(ref state) => state,
            None => panic!("timer has gone away"),
        };

        if state.state.load(SeqCst) & 1 != 0 {
            return Poll::Ready(());
        }

        state.waker.register(&cx.waker());

        // Now that we've registered, do the full check of our own internal
        // state. If we've fired the first bit is set, and if we've been
        // invalidated the second bit is set.
        match state.state.load(SeqCst) {
            n if n & 0b01 != 0 => Poll::Ready(()),
            n if n & 0b10 != 0 => panic!("timer has gone away"),
            _ => Poll::Pending,
        }
    }
}

impl Drop for Delay {
    fn drop(&mut self) {
        let state = match self.state {
            Some(ref s) => s,
            None => return,
        };
        if let Some(timeouts) = state.inner.upgrade() {
            *state.at.lock().unwrap() = None;
            if timeouts.list.push(state).is_ok() {
                timeouts.waker.wake();
            }
        }
    }
}

impl fmt::Debug for Delay {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> Result<(), fmt::Error> {
        f.debug_struct("Delay").field("when", &self.when).finish()
    }
}
