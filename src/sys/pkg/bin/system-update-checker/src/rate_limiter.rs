// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fuchsia_zircon as zx, parking_lot::Mutex};

pub(crate) trait Clock {
    fn now(&self) -> zx::Time;
}

pub(crate) struct Monotonic;

impl Clock for Monotonic {
    fn now(&self) -> zx::Time {
        zx::Time::get_monotonic()
    }
}

pub(crate) struct RateLimiter<C> {
    delay: zx::Duration,
    last_time: Mutex<Option<zx::Time>>,
    clock: C,
}

impl<C> RateLimiter<C>
where
    C: Clock,
{
    pub fn from_delay_and_clock(delay: zx::Duration, clock: C) -> Self {
        Self { delay, last_time: Mutex::new(None), clock }
    }

    /// Executes `f` only if it has been at least `delay` since `rate_limit` was last called.
    pub fn rate_limit(&self, f: impl FnOnce()) {
        let mut run_f = false;
        {
            let mut last_time = self.last_time.lock();
            let now = self.clock.now();
            match *last_time {
                Some(ref mut last_time) if *last_time + self.delay <= now => {
                    *last_time = now;
                    run_f = true;
                }
                Some(_) => {}
                None => {
                    last_time.replace(now);
                    run_f = true;
                }
            }
        }
        if run_f {
            f();
        }
    }
}

pub(crate) type RateLimiterMonotonic = RateLimiter<Monotonic>;

impl RateLimiterMonotonic {
    pub fn from_delay(delay: zx::Duration) -> Self {
        Self::from_delay_and_clock(delay, Monotonic)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        std::{cell::Cell, rc::Rc},
    };

    #[derive(Clone)]
    struct FakeClock {
        t: Rc<Cell<i64>>,
    }

    impl FakeClock {
        fn clock_and_setter(t: i64) -> (Self, Rc<Cell<i64>>) {
            let t = Rc::new(Cell::new(t));
            (Self { t: t.clone() }, t)
        }
    }

    impl Clock for FakeClock {
        fn now(&self) -> zx::Time {
            zx::Time::from_nanos(self.t.get())
        }
    }

    #[test]
    fn first_call_runs() {
        let (clock, _) = FakeClock::clock_and_setter(0);
        let rate_limiter = RateLimiter::from_delay_and_clock(zx::Duration::from_nanos(2), clock);
        let mut val = false;

        rate_limiter.rate_limit(|| {
            val = true;
        });

        assert_eq!(val, true);
    }

    #[test]
    fn second_call_runs_if_stale() {
        let (clock, clock_setter) = FakeClock::clock_and_setter(0);
        let rate_limiter = RateLimiter::from_delay_and_clock(zx::Duration::from_nanos(2), clock);
        let mut val = 0;

        rate_limiter.rate_limit(|| {
            val = 1;
        });

        clock_setter.set(2);
        rate_limiter.rate_limit(|| {
            val = 2;
        });

        assert_eq!(val, 2);
    }

    #[test]
    fn second_call_does_not_run_if_too_recent() {
        let (clock, clock_setter) = FakeClock::clock_and_setter(0);
        let rate_limiter = RateLimiter::from_delay_and_clock(zx::Duration::from_nanos(2), clock);
        let mut val = 0;

        rate_limiter.rate_limit(|| {
            val = 1;
        });

        clock_setter.set(1);
        rate_limiter.rate_limit(|| {
            val = 2;
        });

        assert_eq!(val, 1);
    }

    #[test]
    fn first_and_third_calls_run() {
        let (clock, clock_setter) = FakeClock::clock_and_setter(0);
        let rate_limiter = RateLimiter::from_delay_and_clock(zx::Duration::from_nanos(2), clock);
        let mut val = 0;

        rate_limiter.rate_limit(|| {
            val += 1;
        });

        clock_setter.set(1);
        rate_limiter.rate_limit(|| {
            val += 2;
        });

        clock_setter.set(2);
        rate_limiter.rate_limit(|| {
            val += 4;
        });

        assert_eq!(val, 5);
    }
}
