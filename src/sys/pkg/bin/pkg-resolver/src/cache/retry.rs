// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{FetchError, FetchErrorKind},
    fuchsia_backoff::Backoff,
    rand::Rng,
    std::time::Duration,
};

const MAX_FETCH_FLAKE_RETRIES: u32 = 1;
const MAX_RATE_LIMIT_RETRY_DELAY: Duration = Duration::from_secs(32);

const MAX_RETRY_DELAY: Duration = Duration::from_secs(15);

/// Returns a [fuchsia_backoff::Backoff] strategy suitable for retrying blob fetches.
pub fn blob_fetch() -> impl Backoff<super::FetchError> {
    MaxCumulative::<HttpErrors>::default()
}

/// HttpErrors implements a [fuchsia_backoff::Backoff] strategy for http errors.
#[derive(Default)]
struct HttpErrors {
    backoffs: u32,
    flake_retries: u32,
}

impl Backoff<FetchError> for HttpErrors {
    fn next_backoff(&mut self, err: &FetchError) -> Option<Duration> {
        match err.kind() {
            // Handle rate limit requests using truncated exponential backoff.
            FetchErrorKind::NetworkRateLimit => {
                let delay = 1u64
                    .checked_shl(self.backoffs)
                    .map(Duration::from_secs)
                    .unwrap_or(MAX_RATE_LIMIT_RETRY_DELAY);
                let jitter = Duration::from_millis(rand::thread_rng().gen_range(0, 1001));
                self.backoffs += 1;

                Some(std::cmp::min(delay + jitter, MAX_RATE_LIMIT_RETRY_DELAY))
            }

            // Retry other networking related errors once with no delay.
            FetchErrorKind::Network if self.flake_retries < MAX_FETCH_FLAKE_RETRIES => {
                self.flake_retries += 1;
                Some(Duration::from_secs(0))
            }

            // Immediately fail on other errors.
            FetchErrorKind::Network | FetchErrorKind::Other => None,
        }
    }
}

/// MaxCumulative implements a [fuchsia_backoff::Backoff] strategy wrapping an inner strategy with a
/// globally enforced maximum delay duration of [MAX_RETRY_DELAY].
#[derive(Default)]
struct MaxCumulative<B> {
    inner: B,
    delayed: Duration,
}

impl<B, E> Backoff<E> for MaxCumulative<B>
where
    B: Backoff<E>,
{
    fn next_backoff(&mut self, err: &E) -> Option<Duration> {
        match self.inner.next_backoff(err) {
            Some(delay) => {
                if self.delayed + delay > MAX_RETRY_DELAY {
                    None
                } else {
                    self.delayed += delay;
                    Some(delay)
                }
            }
            None => None,
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, hyper::StatusCode, matches::assert_matches};

    #[test]
    fn http_errors_aborts_on_io_error() {
        assert_eq!(
            HttpErrors::default().next_backoff(&FetchError::CreateBlob(
                pkgfs::install::BlobCreateError::AlreadyExists
            )),
            None
        );
    }

    #[test]
    fn http_errors_retries_network_error_once() {
        let mut backoff = HttpErrors::default();
        let err = FetchError::BadHttpStatus {
            code: StatusCode::INTERNAL_SERVER_ERROR,
            uri: "fake-uri".into(),
        };
        assert_eq!(backoff.next_backoff(&err), Some(Duration::from_secs(0)));
        assert_eq!(backoff.next_backoff(&err), None);
    }

    fn make_rate_limit_error() -> FetchError {
        FetchError::BadHttpStatus { code: StatusCode::TOO_MANY_REQUESTS, uri: "fake-uri".into() }
    }

    #[test]
    fn http_errors_429_always_retries_with_delay() {
        let mut backoff = HttpErrors::default();
        for _ in 0..10 {
            assert_matches!(
                backoff.next_backoff(&make_rate_limit_error()),
                Some(delay) if delay > Duration::from_secs(0)
            );
        }
    }

    #[test]
    fn http_errors_429_caps_max_delay() {
        let mut backoff = HttpErrors::default();
        for _ in 0..10 {
            let delay = backoff.next_backoff(&make_rate_limit_error()).unwrap();
            assert!(
                delay <= MAX_RATE_LIMIT_RETRY_DELAY,
                "{:?} <= {:?}",
                delay,
                MAX_RATE_LIMIT_RETRY_DELAY
            );
        }
    }

    #[test]
    fn http_errors_429_retries_with_increasingish_timeout() {
        let mut backoff = HttpErrors::default();
        let mut last_delay = backoff.next_backoff(&make_rate_limit_error()).unwrap();

        // If the first backoff chooses a jitter value of exactly 1s, it is possible for the
        // second backoff to choose a jitter value of exactly 0s, resulting in 2 delays with
        // the same value. Handle that specific initial case manually by allowing the
        // comparison to be >= instead of >.
        if last_delay == Duration::from_secs(2) {
            let next_delay = backoff.next_backoff(&make_rate_limit_error()).unwrap();
            assert!(next_delay >= last_delay, "{:?} >= {:?}", next_delay, last_delay);
            last_delay = next_delay;
        }

        while last_delay < MAX_RATE_LIMIT_RETRY_DELAY {
            let next_delay = backoff.next_backoff(&make_rate_limit_error()).unwrap();
            assert!(next_delay > last_delay, "{:?} > {:?}", next_delay, last_delay);
            last_delay = next_delay;
        }
    }

    #[derive(Default)]
    struct ConstDelay(Duration);

    impl<E> Backoff<E> for ConstDelay {
        fn next_backoff(&mut self, _err: &E) -> Option<Duration> {
            Some(self.0.clone())
        }
    }

    #[test]
    fn max_cumulative_stays_within_limit() {
        const DELAY: Duration = Duration::from_secs(2);
        let expected_retries = MAX_RETRY_DELAY.as_millis() / DELAY.as_millis();
        let mut backoff = MaxCumulative { inner: ConstDelay(DELAY), ..Default::default() };

        for _ in 0..expected_retries {
            assert_eq!(backoff.next_backoff(&()), Some(DELAY));
        }
        assert_eq!(backoff.next_backoff(&()), None);
    }
}
