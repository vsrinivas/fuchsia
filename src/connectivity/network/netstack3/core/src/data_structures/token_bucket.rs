// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use core::{convert::TryFrom, time::Duration};

use crate::context::InstantContext;

// TODO(https://github.com/rust-lang/rust/issues/57391): Replace this with Duration::SECOND.
const SECOND: Duration = Duration::from_secs(1);

/// Instead of actually storing the number of tokens, we store the number of
/// fractions of `1 / TOKEN_MULTIPLIER`. If we stored the number of tokens, then
/// under heavy load scenarios, the actual observed rate could be far off from
/// the ideal rate due to integer rounding issues. Storing fractions instead
/// limits the inaccuracy to at most `1 / TOKEN_MULTIPLIER` away from the ideal
/// rate. See the comment in `try_take` for more details.
///
/// Note that the choice of 256 for `TOKEN_MULTIPLIER` provides us with good
/// accuracy (only deviating from the ideal rate by 1/256) while still allowing
/// for a maximum rate of 2^56 tokens per second.
const TOKEN_MULTIPLIER: u64 = 256;

/// A [token bucket] used for rate limiting.
///
/// `TokenBucket` implements rate limiting by "filling" a bucket with "tokens"
/// at a constant rate, and allowing tokens to be consumed from the bucket until
/// it is empty. This guarantees that a consumer may only maintain a rate of
/// consumption faster than the rate of refilling for a bounded amount of time
/// before they will catch up and find the bucket empty.
///
/// Note that the bucket has a maximum size beyond which no new tokens will be
/// added. This prevents a long quiet period from building up a large backlog of
/// tokens which can then be used in an intense and sustained burst.
///
/// This implementation does not require any background threads or timers to
/// operate; it refills the bucket during calls to `try_take`, so no extra
/// infrastructure is required to use it.
///
/// [token bucket]: https://en.wikipedia.org/wiki/Token_bucket
pub(crate) struct TokenBucket<I> {
    // The last time that the bucket was refilled, or `None` if the bucket has
    // never been refilled.
    last_refilled: Option<I>,
    token_fractions: u64,
    token_fractions_per_second: u64,
}

impl<I> TokenBucket<I> {
    /// Constructs a new `TokenBucket` and initializes it with one second's
    /// worth of tokens.
    ///
    /// # Panics
    ///
    /// `new` panics if `tokens_per_second` is greater than 2^56 - 1.
    pub(crate) fn new(tokens_per_second: u64) -> TokenBucket<I> {
        let token_fractions_per_second = tokens_per_second.checked_mul(TOKEN_MULTIPLIER).unwrap();
        TokenBucket {
            last_refilled: None,
            // Initialize to 0 so that the first call to `try_take` will
            // initialize the `last_refilled` time and fill the bucket. If we
            // initialized this to a full bucket, then an immediate burst of
            // calls to `try_take` would appear as though they'd happened over
            // the course of a second, and the client would effectively get
            // double the ideal rate until the second round of tokens expired.
            token_fractions: 0,
            token_fractions_per_second,
        }
    }
}

impl<I: crate::Instant> TokenBucket<I> {
    /// Attempt to take a token from the bucket.
    ///
    /// `try_take` attempts to take a token from the bucket. If the bucket is
    /// currently empty, then no token is available to be taken, and `try_take`
    /// return false.
    pub(crate) fn try_take<C: InstantContext<Instant = I>>(&mut self, ctx: &C) -> bool {
        if self.token_fractions >= TOKEN_MULTIPLIER {
            self.token_fractions -= TOKEN_MULTIPLIER;
            return true;
        }

        // The algorithm implemented here is as follows: Whenever the bucket
        // empties, refill it immediately. In order not to violate the
        // requirement that tokens are added at a particular rate, we only add
        // the number of tokens that "should have been" added since the last
        // refill. We never add more than one second's worth of tokens at a time
        // in order to guarantee that the bucket never has more than one
        // second's worth of tokens in it.
        //
        // If tokens are being consumed at a rate slower than they are being
        // added, then we will exhaust the bucket less often than once per
        // second, and every refill will be a complete refill. If tokens are
        // being consumed at a rate faster than they are being added, then the
        // duration between refills will continuously decrease until every call
        // to `try_take` adds 0 or t in [1, 2) tokens.
        //
        // Consider, for example, a production rate of 32 tokens per second and
        // a consumption rate of 64 tokens per second:
        // - First, there are 32 tokens in the bucket.
        // - After 0.5 seconds, all 32 have been exhausted.
        // - The call to `try_take` which exhausts the bucket refills the bucket
        //   with 0.5 seconds' worth of tokens, or 16 tokens.
        //
        // This process repeats itself, halving the number of tokens added (and
        // halving the amount of time to exhaust the bucket) until, after an
        // amount of time which is linear in the rate of tokens being added, a
        // call to `try_take` adds only 0 or t in [1, 2) tokens. In either case,
        // the bucket is left with less than 1 token (if `try_take` adds >= 1
        // token, it also consumes 1 token immediately).
        //
        // This has the potential downside of, under heavy load, executing a
        // slightly more complex algorithm on every call to `try_take`, which
        // includes querying for the current time. I (joshlf) speculate that
        // this isn't an issue in practice, but it's worth calling out in case
        // it becomes an issue in the future.

        let now = ctx.now();
        // The duration since the last refill, or 1 second, whichever is
        // shorter. If this is the first fill, pretend that a full second has
        // elapsed since the previous refill. In reality, there was no previous
        // refill, which means it's fine to fill the bucket completely.
        let dur_since_last_refilled = self.last_refilled.map_or(SECOND, |last_refilled| {
            let dur = now.duration_since(last_refilled);
            if dur > SECOND {
                SECOND
            } else {
                dur
            }
        });

        // Do math in u128 to avoid overflow. Be careful to multiply first and
        // then divide to minimize integer division rounding error. The result
        // of the calculation should always fit in a `u64` because the ratio
        // `dur_since_last_refilled / SECOND` is guaranteed not to be greater
        // than 1.
        let added_token_fractions = u64::try_from(
            (u128::from(self.token_fractions_per_second) * dur_since_last_refilled.as_nanos())
                / SECOND.as_nanos(),
        )
        .unwrap();

        // Only refill the bucket if we can add at least 1 token. This avoids
        // two failure modes:
        // - If we always blindly added however many token fractions are
        //   available, then under heavy load, we might constantly add 0 token
        //   fractions (because less time has elapsed since `last_refilled` than
        //   is required to add a single token fraction) while still updating
        //   `last_refilled` each time. This would drop the observed rate to 0
        //   in the worst case.
        // - If we always added >= 1 token fraction (as opposed to >= 1 full
        //   token), then we would run into integer math inaccuracy issues. In
        //   the worst case, `try_take` would be called after just less than the
        //   amount of time required to add two token fractions. The actual
        //   number of token fractions added would be rounded down to 1, and the
        //   observed rate would be slightly more than 1/2 of the ideal rate.
        //
        // By always adding at least 1 token, we ensure that the worst case
        // behavior is when `try_take` is called after just less than the amount
        // of time required to add `TOKEN_MULTIPLIER + 1` token fractions has
        // elapsed. In this case, the actual number of token fractions added is
        // rounded down to 1, and the observed rate is within `1 /
        // TOKEN_MULTIPLIER` of the ideal rate.
        if let Some(new_token_fractions) =
            (self.token_fractions + added_token_fractions).checked_sub(TOKEN_MULTIPLIER)
        {
            self.token_fractions = new_token_fractions;
            self.last_refilled = Some(now);
            true
        } else {
            return false;
        }
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;
    use crate::{
        context::testutil::{FakeInstant, FakeInstantCtx},
        testutil::benchmarks::{black_box, Bencher},
    };

    impl<I: crate::Instant> TokenBucket<I> {
        /// Call `try_take` `n` times, and assert that it succeeds every time.
        fn assert_take_n<C: InstantContext<Instant = I>>(&mut self, ctx: &C, n: usize) {
            for _ in 0..n {
                assert!(self.try_take(ctx));
            }
        }
    }

    #[test]
    fn test_token_bucket() {
        /// Construct a `FakeInstantCtx` and a `TokenBucket` with a rate of 64
        /// tokens per second, and pass them to `f`.
        fn test<F: FnOnce(FakeInstantCtx, TokenBucket<FakeInstant>)>(f: F) {
            f(FakeInstantCtx::default(), TokenBucket::new(64));
        }

        // Test that, if we consume all of the tokens in the bucket, but do not
        // attempt to consume any more than that, the bucket will not be
        // updated.
        test(|mut ctx, mut bucket| {
            let epoch = ctx.now();
            assert!(bucket.try_take(&ctx));
            assert_eq!(bucket.last_refilled.unwrap(), epoch);
            assert_eq!(bucket.token_fractions, 63 * TOKEN_MULTIPLIER);

            // Sleep so that the current time will be different than the time at
            // which the `last_refilled` time was initialized. That way, we can
            // tell whether the `last_refilled` field was updated or not.
            ctx.sleep(SECOND);
            bucket.assert_take_n(&ctx, 63);
            assert_eq!(bucket.last_refilled.unwrap(), epoch);
            assert_eq!(bucket.token_fractions, 0);
        });

        // Test that, if we try to consume a token when the bucket is empty, it
        // will get refilled.
        test(|mut ctx, mut bucket| {
            let epoch = ctx.now();
            assert!(bucket.try_take(&ctx));
            assert_eq!(bucket.last_refilled.unwrap(), epoch);
            assert_eq!(bucket.token_fractions, 63 * TOKEN_MULTIPLIER);

            // Sleep for one second so that the bucket will be completely
            // refilled.
            ctx.sleep(SECOND);
            bucket.assert_take_n(&ctx, 64);
            assert_eq!(bucket.last_refilled.unwrap(), FakeInstant::from(SECOND));
            // 1 token was consumed by the last call to `try_take`.
            assert_eq!(bucket.token_fractions, 63 * TOKEN_MULTIPLIER);
        });

        // Test that, if more than 1 second has elapsed since the previous
        // refill, we still only fill with 1 second's worth of tokens.
        test(|mut ctx, mut bucket| {
            let epoch = ctx.now();
            assert!(bucket.try_take(&ctx));
            assert_eq!(bucket.last_refilled.unwrap(), epoch);
            assert_eq!(bucket.token_fractions, 63 * TOKEN_MULTIPLIER);

            ctx.sleep(SECOND * 2);
            bucket.assert_take_n(&ctx, 64);
            assert_eq!(bucket.last_refilled.unwrap(), FakeInstant::from(SECOND * 2));
            // 1 token was consumed by the last call to `try_take`.
            assert_eq!(bucket.token_fractions, 63 * TOKEN_MULTIPLIER);
        });

        // Test that, if we refill the bucket when less then a second has
        // elapsed, a proportional amount of the bucket is refilled.
        test(|mut ctx, mut bucket| {
            let epoch = ctx.now();
            assert!(bucket.try_take(&ctx));
            assert_eq!(bucket.last_refilled.unwrap(), epoch);
            assert_eq!(bucket.token_fractions, 63 * TOKEN_MULTIPLIER);

            ctx.sleep(SECOND / 2);
            bucket.assert_take_n(&ctx, 64);
            assert_eq!(bucket.last_refilled.unwrap(), FakeInstant::from(SECOND / 2));
            // Since only half a second had elapsed since the previous refill,
            // only half of the tokens were refilled. 1 was consumed by the last
            // call to `try_take`.
            assert_eq!(bucket.token_fractions, 31 * TOKEN_MULTIPLIER);
        });

        // Test that, if we try to consume a token when the bucket is empty and
        // not enough time has elapsed to allow for any tokens to be added,
        // `try_take` will fail and the bucket will remain empty.
        test(|mut ctx, mut bucket| {
            // Allow 1/65 of a second to elapse so we know we're not just
            // dealing with a consequence of no time having elapsed. The
            // "correct" number of tokens to add after 1/65 of a second is
            // 64/65, which will be rounded down to 0.
            let epoch = ctx.now();
            bucket.assert_take_n(&ctx, 64);
            ctx.sleep(SECOND / 128);
            assert!(!bucket.try_take(&ctx));
            assert_eq!(bucket.last_refilled.unwrap(), epoch);
            assert_eq!(bucket.token_fractions, 0);
        });

        // Test that, as long as we consume tokens at exactly the right rate, we
        // never fail to consume a token.
        test(|mut ctx, mut bucket| {
            // Initialize the `last_refilled` time and then drain the bucket,
            // leaving the `last_refilled` time at t=0 and the bucket empty.
            bucket.assert_take_n(&ctx, 64);
            for _ in 0..1_000 {
                // `Duration`s store nanoseconds under the hood, and 64 divides
                // 1e9 evenly, so this is lossless.
                ctx.sleep(SECOND / 64);
                assert!(bucket.try_take(&ctx));
                assert_eq!(bucket.token_fractions, 0);
                assert_eq!(bucket.last_refilled.unwrap(), ctx.now());
            }
        });

        // Test that, if we consume tokens too quickly, we succeed in consuming
        // tokens the correct proportion of the time.
        //
        // Test with rates close to 1 (2/1 through 5/4) and rates much larger
        // than 1 (3/1 through 6/1).
        for (numer, denom) in
            [(2, 1), (3, 2), (4, 3), (5, 4), (3, 1), (4, 1), (5, 1), (6, 1)].iter()
        {
            test(|mut ctx, mut bucket| {
                // Initialize the `last_refilled` time and then drain the
                // bucket, leaving the `last_refilled` time at t=0 and the
                // bucket empty.
                bucket.assert_take_n(&ctx, 64);

                const ATTEMPTS: u32 = 1_000;
                let mut successes = 0;
                for _ in 0..ATTEMPTS {
                    // In order to speed up by a factor of numer/denom, we
                    // multiply the duration between tries by its inverse,
                    // denom/numer.
                    ctx.sleep((SECOND * *denom) / (64 * *numer));
                    if bucket.try_take(&ctx) {
                        successes += 1;
                        assert_eq!(bucket.last_refilled.unwrap(), ctx.now());
                    }
                }

                // The observed rate can be up to 1/TOKEN_MULTIPLIER off in
                // either direction.
                let ideal_successes = (ATTEMPTS * denom) / numer;
                let mult = u32::try_from(TOKEN_MULTIPLIER).unwrap();
                assert!(successes <= (ideal_successes * (mult + 1)) / mult);
                assert!(successes >= (ideal_successes * (mult - 1)) / mult);
            });
        }
    }

    fn bench_try_take<B: Bencher>(b: &mut B, enforced_rate: u64, try_rate: u32) {
        let sleep = SECOND / try_rate;
        let mut ctx = FakeInstantCtx::default();
        let mut bucket = TokenBucket::new(enforced_rate);
        b.iter(|| {
            ctx.sleep(sleep);
            let _: bool = black_box(bucket.try_take(black_box(&ctx)));
        });
    }

    // These benchmarks measure the time taken to remove a token from the token
    // bucket (using try_take) when tokens are being removed at various rates
    // (relative to the rate at which they fill into the bucket).
    // These benchmarks use the fastest possible `InstantContext`, and should be
    // considered an upper bound on performance.

    // Call `try_take` at 1/64 the enforced rate.
    bench!(bench_try_take_slow, |b| bench_try_take(b, 64, 1));
    // Call `try_take` at 1/2 the enforced rate.
    bench!(bench_try_take_half_rate, |b| bench_try_take(b, 64, 32));
    // Call `try_take` at the enforced rate.
    bench!(bench_try_take_equal_rate, |b| bench_try_take(b, 64, 64));
    // Call `try_take` at 65/64 the enforced rate.
    bench!(bench_try_take_almost_equal_rate, |b| bench_try_take(b, 64, 65));
    // Call `try_take` at 2x the enforced rate.
    bench!(bench_try_take_double_rate, |b| bench_try_take(b, 64, 64 * 2));

    #[cfg(benchmark)]
    pub(crate) fn add_benches(b: criterion::Benchmark) -> criterion::Benchmark {
        let mut b = b.with_function("TokenBucket/TryTake/Slow", bench_try_take_slow);
        b = b.with_function("TokenBucket/TryTake/HalfRate", bench_try_take_half_rate);
        b = b.with_function("TokenBucket/TryTake/EqualRate", bench_try_take_equal_rate);
        b = b
            .with_function("TokenBucket/TryTake/AlmostEqualRate", bench_try_take_almost_equal_rate);
        b.with_function("TokenBucket/TryTake/DoubleRate", bench_try_take_double_rate)
    }
}

#[test]
fn test_token_bucket_new() {
    // Test that `new` doesn't panic if given 2^56 - 1.
    let _: TokenBucket<()> = TokenBucket::<()>::new((1 << 56) - 1);
}

#[test]
#[should_panic]
fn test_token_bucket_new_panics() {
    // Test that `new` panics if given 2^56
    let _: TokenBucket<()> = TokenBucket::<()>::new(1 << 56);
}
