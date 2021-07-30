// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    core::{
        pin::Pin,
        task::{Context, Poll},
    },
    fuchsia_async::{self as fasync, DurationExt},
    fuchsia_zircon as zx,
    futures::{
        stream::{FusedStream, Stream},
        FutureExt,
    },
};

/// Default frequency used for rings.
const DEFAULT_FREQUENCY: zx::Duration = zx::Duration::from_seconds(5);

/// A Ringer is a Stream that will output an Item at a fixed frequency.
///
/// The Ringer will emit items when `ringing` is true. The Ringer is terminated when `ringing` is
/// false.
pub struct Ringer {
    /// Determines how frequently an item will be emitted from the Ringer stream when a call is
    /// incoming.
    frequency: zx::Duration,
    /// Internal timer used to trigger the next ring.
    timer: Option<fasync::Timer>,
    /// Stream should produce a ring.
    ringing: bool,
}

impl Default for Ringer {
    /// Create a new Ringer with the `DEFAULT_FREQUENCY`.
    fn default() -> Self {
        Self { frequency: DEFAULT_FREQUENCY, timer: None, ringing: false }
    }
}

impl Ringer {
    /// Set the Ringer to `ring`.
    pub fn ring(&mut self, ring: bool) {
        self.ringing = ring;
    }

    #[cfg(test)]
    pub fn ringing(&self) -> bool {
        self.ringing
    }
}

impl Stream for Ringer {
    type Item = ();
    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        if self.is_terminated() {
            return Poll::Ready(None);
        }

        // Copy self.frequency to avoid double borrow.
        let freq = self.frequency;

        match &mut self.timer {
            None => {
                let timer = fasync::Timer::new(self.frequency.after_now());
                self.timer = Some(timer);
                Poll::Ready(Some(()))
            }
            Some(timer) => match timer.poll_unpin(cx) {
                Poll::Ready(()) => {
                    timer.reset(freq.after_now());
                    Poll::Ready(Some(()))
                }
                Poll::Pending => Poll::Pending,
            },
        }
    }
}

impl FusedStream for Ringer {
    fn is_terminated(&self) -> bool {
        !self.ringing
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures::StreamExt;
    use matches::assert_matches;

    #[fuchsia::test]
    fn ringer_fused_stream_implementation() {
        let mut ringer = Ringer::default();
        assert!(ringer.is_terminated());
        assert!(!ringer.ringing());
        ringer.ring(true);
        assert!(!ringer.is_terminated());
        assert!(ringer.ringing());
        ringer.ring(false);
        assert!(ringer.is_terminated());
        assert!(!ringer.ringing());
    }

    #[fuchsia::test]
    fn ring_stream_output_frequency() {
        let mut exec = fasync::TestExecutor::new_with_fake_time().unwrap();
        exec.set_fake_time(fasync::Time::from_nanos(0));
        exec.wake_expired_timers();

        // A new ringer returns None
        let mut ringer = Ringer::default();
        let result = exec.run_until_stalled(&mut ringer.next());
        assert_matches!(result, Poll::Ready(None));

        // An Item is returned immediately after a new incoming call is set.
        ringer.ring(true);
        let result = exec.run_until_stalled(&mut ringer.next());
        assert_matches!(result, Poll::Ready(Some(())));

        // The stream is Pending
        let result = exec.run_until_stalled(&mut ringer.next());
        assert_matches!(result, Poll::Pending);

        // Advance the time by less than DEFAULT_FREQUENCY.
        exec.set_fake_time(fasync::Time::after(DEFAULT_FREQUENCY / 2));
        exec.wake_expired_timers();

        // The stream is Pending
        let result = exec.run_until_stalled(&mut ringer.next());
        assert_matches!(result, Poll::Pending);

        exec.set_fake_time(fasync::Time::after(DEFAULT_FREQUENCY));
        exec.wake_expired_timers();

        // After DEFAULT_FREQUENCY, a new Item is Ready.
        let result = exec.run_until_stalled(&mut ringer.next());
        assert_matches!(result, Poll::Ready(Some(())));

        // The stream is Pending
        let result = exec.run_until_stalled(&mut ringer.next());
        assert_matches!(result, Poll::Pending);

        // The stream is terminated when the call is cleared.
        ringer.ring(false);
        let result = exec.run_until_stalled(&mut ringer.next());
        assert_matches!(result, Poll::Ready(None));
    }
}
