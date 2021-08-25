// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This crate tests the behavior of the test-harness crate and associated macros. It is defined
//! as a separate crate because it needs to test how the macros exported in `test-harness-macro`
//! call into fns in `test-harness`. This type of test cannot be done from within `test-harness`
//! itself, as the `test-harness-macro`-defined macros call `test-harness` fns using the
//! `::test-harness::.*` style name resolution, and this name is not accessible from within the
//! `test-harness` crate itself.

#![cfg(test)]
mod test {
    use anyhow::Error;
    use test_harness;

    #[test_harness::run_singlethreaded_test]
    async fn trivial_unit_harness(_unit_harness: ()) -> Result<(), Error> {
        Ok(())
    }

    // This mod tests the ability for TestHarnesses to share state via the `TestHarness::init`'s
    // `SharedState` map parameter.
    mod shared_state {
        use {
            super::*,
            anyhow::format_err,
            fuchsia, fuchsia_async as fasync,
            futures::{
                channel::oneshot,
                future::{self, BoxFuture},
                pin_mut, FutureExt,
            },
            matches::assert_matches,
            parking_lot::Mutex,
            std::{sync::Arc, task::Poll},
            test_harness::{SharedState, TestHarness},
        };
        const SHARED_KEY: &'static str = "U64";
        const INITIAL_VAL: u64 = 12;
        struct IntHarness {
            pub val: Arc<Mutex<u64>>,
        }

        impl TestHarness for IntHarness {
            type Env = ();
            type Runner = future::Pending<Result<(), Error>>;

            fn init(
                shared_state: &Arc<SharedState>,
            ) -> BoxFuture<'static, Result<(Self, Self::Env, Self::Runner), Error>> {
                let shared = shared_state.get(SHARED_KEY).unwrap_or_else(|| {
                    let _ = shared_state.try_insert(SHARED_KEY, Mutex::new(INITIAL_VAL));
                    shared_state.get(SHARED_KEY).unwrap()
                });
                async move { shared.map(|s| (IntHarness { val: s }, (), future::pending())) }
                    .boxed()
            }

            fn terminate(_env: Self::Env) -> BoxFuture<'static, Result<(), Error>> {
                future::ok(()).boxed()
            }
        }

        #[test_harness::run_singlethreaded_test]
        async fn harnesses_share_state(
            (harness1, harness2): (IntHarness, IntHarness),
        ) -> Result<(), Error> {
            assert_eq!(*harness1.val.lock(), INITIAL_VAL);
            assert_eq!(*harness2.val.lock(), INITIAL_VAL);
            *harness1.val.lock() += 1;
            assert_eq!(*harness2.val.lock(), INITIAL_VAL + 1);
            *harness2.val.lock() += 1;
            assert_eq!(*harness1.val.lock(), INITIAL_VAL + 2);
            Ok(())
        }

        #[fuchsia::test]
        fn get_or_insert_with_preemption() {
            let mut exec = fasync::TestExecutor::new().unwrap();
            let s = SharedState::default();
            let (sender, receiver) = oneshot::channel::<()>();
            let inserter1 = || {
                async {
                    // inserter_1 won't complete until we send the oneshot.
                    receiver.await.unwrap();
                    Ok(12)
                }
            };
            let insert_fut_1 = s.get_or_insert_with(SHARED_KEY, inserter1);
            pin_mut!(insert_fut_1);
            // `insert_fut_1` should hang on the oneshot without putting anything into SharedState.
            assert_matches!(exec.run_until_stalled(&mut insert_fut_1), Poll::Pending);
            assert_matches!(s.get::<i32>(SHARED_KEY), None);

            let insert_fut_2 = s.get_or_insert_with(SHARED_KEY, || async { Ok(13) });
            // `insert_fut_2` should succeed immediately and add 13 to the SharedState.
            let res2 = exec.run_singlethreaded(insert_fut_2).expect("failed to get or insert");
            assert_eq!(*res2, 13);

            // Allow insert_fut_1 to proceed
            sender.send(()).unwrap();
            let res1 = exec.run_until_stalled(&mut insert_fut_1);
            // insert_fut_1 should realize it was preempted by insert_fut_2 and return 13 without
            // overwriting the existing value.
            match res1 {
                Poll::Ready(Ok(val)) => assert_eq!(*val, 13),
                _ => panic!("expected insert_fut_1 to complete successfully"),
            };
            match s.get::<i32>(SHARED_KEY) {
                Some(Ok(val)) => assert_eq!(*val, 13),
                _ => panic!("expected 13 to be present in SharedState"),
            };
        }

        #[fuchsia::test]
        async fn get_or_insert_with_errors() {
            let s = SharedState::default();
            // An error inserting should be propagated by get_or_insert.
            let error_attempt: Result<Arc<i32>, Error> =
                s.get_or_insert_with(SHARED_KEY, || async { Err(format_err!("Uh oh")) }).await;
            assert_matches!(error_attempt, Err(_));

            // try_insert should succeed, as the previous inserter failed.
            assert_matches!(s.try_insert(SHARED_KEY, 12), Ok(_));

            // get_or_insert should return the already-inserted value even though `inserter` returns
            // an error, because `key` already existed in the map.
            let success: Result<Arc<i32>, Error> = s
                .get_or_insert_with(SHARED_KEY, || async { Err(format_err!("Uh oh part 2")) })
                .await;
            assert_eq!(*success.unwrap(), 12);
        }
    }
}
