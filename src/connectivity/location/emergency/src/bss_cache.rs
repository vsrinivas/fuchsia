// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fidl::Error as FidlError,
    fidl_fuchsia_wlan_policy::{
        Bss as WlanPolicyBss, ScanErrorCode, ScanResult, ScanResultIteratorProxyInterface,
    },
    fuchsia_async::futures::{
        future::BoxFuture,
        task::{Context, Poll},
        Future, FutureExt, Stream, StreamExt,
    },
    std::{collections::BTreeMap, pin::Pin},
    thiserror::Error,
};

#[async_trait(?Send)]
pub trait BssCache {
    /// Updates the cache with BSSes from `new_bsses`.
    async fn update<I: ScanResultIteratorProxyInterface>(
        &mut self,
        new_bsses: I,
    ) -> Result<(), UpdateError>;

    /// Returns an iterator over the known BSSes.
    fn iter(&self) -> Box<dyn Iterator<Item = (&'_ BssId, &'_ Bss)> + '_>;
}

/// A cache for WLAN Basic Service Sets, also known as WLAN base-stations.
#[derive(Default)]
pub struct RealBssCache {
    bss_map: BTreeMap<BssId, Bss>,
}

pub type BssId = [u8; BSS_ADDR_LEN_BYTES];

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Bss {
    pub(crate) rssi: Option<i8>,
    pub(crate) frequency: Option<u32>,
}

#[derive(Clone, Copy, Debug, Error, PartialEq)]
pub enum UpdateError {
    #[error("found BSSes, but no BSS IDs")]
    NoBssIds,
    #[error("found no BSSes")]
    NoBsses,
    #[error("connection to iterator failed")]
    Ipc,
    #[error("iterator reported error")]
    Service,
}

// Length of a BSS ID. Governed by IEEE standards.
const BSS_ADDR_LEN_BYTES: usize = 6;

// Upper bound on the number of BSSes cached. Our goals in tuning this value are:
// * retain enough BSSes to give a tight radius of confidence, and
// * minimize the potential for exhausting memory
//
// This value was chosen somewhat arbitrarily, and may be tuned based on field
// data about the radius of confidence we achieve.
const MAX_BSSES: usize = 20;

// Upper bound on the number of IPCs to `ScanResultIterator`. Every non-terminal
// IPC should yield at least one `ScanResult`. And, in normal operation, we expect
// that a `ScanResult` will have at least one BSS. Hence, we set this limit to
// equal the limit on BSSes.
const MAX_IPCS: usize = MAX_BSSES;

impl RealBssCache {
    pub fn new() -> Self {
        Default::default()
    }

    fn prune_to_size(&mut self) {
        if let Some(first_overflowed_bssid) = self.bss_map.keys().cloned().nth(MAX_BSSES) {
            self.bss_map.split_off(&first_overflowed_bssid);
        }
    }
}

#[async_trait(?Send)]
impl BssCache for RealBssCache {
    async fn update<I: ScanResultIteratorProxyInterface>(
        &mut self,
        new_bsses: I,
    ) -> Result<(), UpdateError> {
        let mut iterator_service_result = ScanResultStream::new(new_bsses)
            .take(MAX_IPCS)
            .collect::<Vec<Result<Vec<ScanResult>, UpdateError>>>()
            .await
            .into_iter()
            .peekable();
        // If we have no results, report the appropriate error.
        match iterator_service_result.peek() {
            None => return Err(UpdateError::NoBsses), // First IPC yielded empty set.
            Some(Err(e)) => return Err(*e),           // First IPC yielded error.
            Some(Ok(_)) => (),
        };

        let mut bss_list = iterator_service_result
            .filter_map(|res| res.ok()) // Since we have at least one result, ignore errors.
            .flatten() // Flatten per-IPC `Vec`s into single `Vec`.
            .flat_map(|network| network.entries) // Project `Vec` of BSSes out of each network.
            .flatten() // Flatten per-network `Vec`s of BSSes to single `Vec`.
            .peekable();
        if bss_list.peek().is_none() {
            return Err(UpdateError::NoBsses);
        };

        let mut valid_bss_list = bss_list
            .filter_map(|bss: WlanPolicyBss| match bss.bssid {
                Some(id) => Some((id, Bss { rssi: bss.rssi, frequency: bss.frequency })),
                None => None,
            })
            .peekable();
        if valid_bss_list.peek().is_none() {
            return Err(UpdateError::NoBssIds);
        }

        self.bss_map = valid_bss_list.collect();
        self.prune_to_size();
        Ok(())
    }

    fn iter(&self) -> Box<dyn Iterator<Item = (&'_ BssId, &'_ Bss)> + '_> {
        Box::new(self.bss_map.iter())
    }
}

type GetNextResponse = Result<Result<Vec<ScanResult>, ScanErrorCode>, FidlError>;

// ScanResultStream adapts the FIDL `ScanResultIterator` into a Rust
// `Stream`.  For more details, see the documentation for
// `ScanResultStream::poll_next()`, below.
struct ScanResultStream<'a, F, I>
where
    F: Future<Output = GetNextResponse> + Send,
    I: ScanResultIteratorProxyInterface<GetNextResponseFut = F>,
{
    iterator_service: Option<I>,
    pending_ipc: Option<BoxFuture<'a, F::Output>>,
}

impl<'a, F, I> ScanResultStream<'a, F, I>
where
    F: Future<Output = GetNextResponse> + Send,
    I: ScanResultIteratorProxyInterface<GetNextResponseFut = F>,
{
    fn new(iterator_service: I) -> Self {
        Self { iterator_service: Some(iterator_service), pending_ipc: None }
    }
}

impl<'a, F, I> Unpin for ScanResultStream<'a, F, I>
where
    F: Future<Output = GetNextResponse> + Send,
    I: ScanResultIteratorProxyInterface<GetNextResponseFut = F>,
{
}

impl<'a, F, I> Stream for ScanResultStream<'a, F, I>
where
    F: Future<Output = GetNextResponse> + Send + 'a,
    I: ScanResultIteratorProxyInterface<GetNextResponseFut = F>,
{
    type Item = Result<Vec<ScanResult>, UpdateError>;

    // Calls through to `ScanResultIterator.GetNext()`.
    // * If the call yields a `FidlError` or a `ScanErrorCode`, returns an `UpdateError`,
    //   and ensures that subsequent calls yield None.
    // * If the call yields an empty `Vec` yields None.
    // * Otherwise, returns the `Vec` from the call to `GetNext()`.
    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        // Note: we `take()` `iterator_service` here, and only replace
        // `iterator_service` if
        // a) the IPC yielded a `ScanResult`, or
        // b) the IPC is still pending.
        //
        // By doing so, we ensure that, if the IPC yielded an empty
        // result, or an error, then subsequent calls to `poll_next()`
        // a) will _not_ issue an IPC, and
        // b) will return None.
        let iterator_service = match self.iterator_service.take() {
            Some(is) => is,
            None => return Poll::Ready(None),
        };
        let mut fut = match self.pending_ipc.take() {
            Some(ipc) => ipc,
            None => iterator_service.get_next().boxed(),
        };
        match fut.poll_unpin(cx) {
            Poll::Pending => {
                self.pending_ipc = Some(fut);
                self.iterator_service = Some(iterator_service);
                Poll::Pending
            }
            Poll::Ready(fidl_result) => Poll::Ready(match flatten_get_next_error(fidl_result) {
                Ok(res) => {
                    if res.is_empty() {
                        None
                    } else {
                        self.iterator_service = Some(iterator_service);
                        Some(Ok(res))
                    }
                }
                Err(e) => Some(Err(e)),
            }),
        }
    }
}

fn flatten_get_next_error(fidl_result: GetNextResponse) -> Result<Vec<ScanResult>, UpdateError> {
    match fidl_result {
        Ok(service_result) => match service_result {
            Ok(scan_results) => Ok(scan_results),
            Err(_) => Err(UpdateError::Service),
        },
        Err(_) => Err(UpdateError::Ipc),
    }
}

#[cfg(test)]
mod tests {
    mod single_call_success {
        use {
            super::super::*,
            fidl_fuchsia_wlan_policy::{
                Compatibility::Supported, NetworkIdentifier, SecurityType::Wpa2,
            },
            fuchsia_async as fasync,
            matches::assert_matches,
            std::convert::TryFrom,
            test_doubles::FakeScanResultIterator,
        };

        #[fasync::run_until_stalled(test)]
        async fn caches_single_bss_with_just_bss_data() {
            let mut cache = RealBssCache::new();
            let result = cache
                .update(FakeScanResultIterator::new_single_step(vec![ScanResult {
                    id: None,
                    entries: Some(vec![WlanPolicyBss {
                        bssid: Some([0, 1, 2, 3, 4, 5]),
                        rssi: None,
                        frequency: None,
                        timestamp_nanos: None,
                        ..WlanPolicyBss::EMPTY
                    }]),
                    compatibility: None,
                    ..ScanResult::EMPTY
                }]))
                .await;
            assert_eq!(result, Ok(()));
            assert_eq!(
                cache.iter().next(),
                Some((&[0, 1, 2, 3, 4, 5], &Bss { rssi: None, frequency: None }))
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn caches_single_bss_with_all_data() {
            let mut cache = RealBssCache::new();
            let result = cache
                .update(FakeScanResultIterator::new_single_step(vec![ScanResult {
                    id: Some(NetworkIdentifier { ssid: vec![b'a'], type_: Wpa2 }),
                    entries: Some(vec![WlanPolicyBss {
                        bssid: Some([0, 1, 2, 3, 4, 5]),
                        rssi: Some(-1),
                        frequency: Some(2412),
                        timestamp_nanos: Some(1),
                        ..WlanPolicyBss::EMPTY
                    }]),
                    compatibility: Some(Supported),
                    ..ScanResult::EMPTY
                }]))
                .await;
            assert_eq!(result, Ok(()));
            assert_eq!(
                cache.iter().next(),
                Some((&[0, 1, 2, 3, 4, 5], &Bss { rssi: Some(-1), frequency: Some(2412) }))
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn caches_multiple_bsses_from_single_network() {
            let mut cache = RealBssCache::new();
            let result = cache
                .update(FakeScanResultIterator::new_single_step(vec![ScanResult {
                    id: None,
                    entries: Some(vec![
                        WlanPolicyBss {
                            bssid: Some([0, 0, 0, 0, 0, 0]),
                            rssi: None,
                            frequency: None,
                            timestamp_nanos: None,
                            ..WlanPolicyBss::EMPTY
                        },
                        WlanPolicyBss {
                            bssid: Some([1, 1, 1, 1, 1, 1]),
                            rssi: None,
                            frequency: None,
                            timestamp_nanos: None,
                            ..WlanPolicyBss::EMPTY
                        },
                    ]),
                    compatibility: None,
                    ..ScanResult::EMPTY
                }]))
                .await;
            assert_eq!(result, Ok(()));

            let bsses: BTreeMap<&BssId, &Bss> = cache.iter().collect();
            assert_eq!(bsses.get(&[0, 0, 0, 0, 0, 0]), Some(&&Bss { rssi: None, frequency: None }));
            assert_eq!(bsses.get(&[1, 1, 1, 1, 1, 1]), Some(&&Bss { rssi: None, frequency: None }));
        }

        #[fasync::run_until_stalled(test)]
        async fn deduplicates_bsses_from_single_network() {
            let mut cache = RealBssCache::new();
            let result = cache
                .update(FakeScanResultIterator::new_single_step(vec![ScanResult {
                    id: None,
                    entries: Some(vec![
                        WlanPolicyBss {
                            bssid: Some([0, 1, 2, 3, 4, 5]),
                            rssi: Some(-1),
                            frequency: Some(2412),
                            timestamp_nanos: Some(1),
                            ..WlanPolicyBss::EMPTY
                        },
                        WlanPolicyBss {
                            bssid: Some([0, 1, 2, 3, 4, 5]),
                            rssi: Some(-2),
                            frequency: Some(2432),
                            timestamp_nanos: Some(2),
                            ..WlanPolicyBss::EMPTY
                        },
                    ]),
                    compatibility: None,
                    ..ScanResult::EMPTY
                }]))
                .await;
            assert_eq!(result, Ok(()));

            let mut bsses = cache.iter();
            assert_matches!(bsses.next(), Some((&[0, 1, 2, 3, 4, 5], _)));
            assert_eq!(bsses.next(), None);
        }

        #[fasync::run_until_stalled(test)]
        async fn caches_multiple_bsses_from_multiple_networks() {
            let mut cache = RealBssCache::new();
            let result = cache
                .update(FakeScanResultIterator::new_single_step(vec![
                    ScanResult {
                        id: None,
                        entries: Some(vec![WlanPolicyBss {
                            bssid: Some([0, 0, 0, 0, 0, 0]),
                            rssi: None,
                            frequency: None,
                            timestamp_nanos: None,
                            ..WlanPolicyBss::EMPTY
                        }]),
                        compatibility: None,
                        ..ScanResult::EMPTY
                    },
                    ScanResult {
                        id: None,
                        entries: Some(vec![WlanPolicyBss {
                            bssid: Some([1, 1, 1, 1, 1, 1]),
                            rssi: None,
                            frequency: None,
                            timestamp_nanos: None,
                            ..WlanPolicyBss::EMPTY
                        }]),
                        compatibility: None,
                        ..ScanResult::EMPTY
                    },
                ]))
                .await;
            assert_eq!(result, Ok(()));

            let bsses: BTreeMap<&BssId, &Bss> = cache.iter().collect();
            assert_eq!(bsses.get(&[0, 0, 0, 0, 0, 0]), Some(&&Bss { rssi: None, frequency: None }));
            assert_eq!(bsses.get(&[1, 1, 1, 1, 1, 1]), Some(&&Bss { rssi: None, frequency: None }));
        }

        #[fasync::run_until_stalled(test)]
        async fn deduplicates_bsses_from_multiple_networks() {
            let mut cache = RealBssCache::new();
            let result = cache
                .update(FakeScanResultIterator::new_single_step(vec![
                    ScanResult {
                        id: None,
                        entries: Some(vec![WlanPolicyBss {
                            bssid: Some([0, 1, 2, 3, 4, 5]),
                            rssi: Some(-1),
                            frequency: Some(2412),
                            timestamp_nanos: Some(1),
                            ..WlanPolicyBss::EMPTY
                        }]),
                        compatibility: None,
                        ..ScanResult::EMPTY
                    },
                    ScanResult {
                        id: None,
                        entries: Some(vec![WlanPolicyBss {
                            bssid: Some([0, 1, 2, 3, 4, 5]),
                            rssi: Some(-2),
                            frequency: Some(2432),
                            timestamp_nanos: Some(2),
                            ..WlanPolicyBss::EMPTY
                        }]),
                        compatibility: None,
                        ..ScanResult::EMPTY
                    },
                ]))
                .await;
            assert_eq!(result, Ok(()));

            let mut bsses = cache.iter();
            assert_matches!(bsses.next(), Some((&[0, 1, 2, 3, 4, 5], _)));
            assert_eq!(bsses.next(), None);
        }

        #[fasync::run_until_stalled(test)]
        async fn honors_max_bss_limit() {
            let mut cache = RealBssCache::new();
            let bsses: Vec<_> = (0..MAX_BSSES + 1)
                .map(|i| WlanPolicyBss {
                    bssid: Some(
                        BssId::try_from(&i.to_le_bytes()[0..BSS_ADDR_LEN_BYTES])
                            .expect("internal error"),
                    ),
                    rssi: None,
                    frequency: None,
                    timestamp_nanos: None,
                    ..WlanPolicyBss::EMPTY
                })
                .collect();
            let scan_results = vec![ScanResult {
                id: None,
                entries: Some(bsses),
                compatibility: None,
                ..ScanResult::EMPTY
            }];
            let _ = cache.update(FakeScanResultIterator::new_single_step(scan_results)).await;
            assert_eq!(cache.iter().count(), MAX_BSSES);
        }

        #[fasync::run_until_stalled(test)]
        async fn does_not_count_bad_bsses_toward_max_bss_limit() {
            let mut cache = RealBssCache::new();
            let bad_bss = std::iter::once(WlanPolicyBss {
                bssid: None,
                rssi: None,
                frequency: None,
                timestamp_nanos: None,
                ..WlanPolicyBss::EMPTY
            });
            let good_bsses = (0..MAX_BSSES).map(|i| WlanPolicyBss {
                bssid: Some(
                    BssId::try_from(&i.to_le_bytes()[0..BSS_ADDR_LEN_BYTES])
                        .expect("internal error"),
                ),
                rssi: None,
                frequency: None,
                timestamp_nanos: None,
                ..WlanPolicyBss::EMPTY
            });
            let bsses: Vec<_> = bad_bss.chain(good_bsses).collect();
            let scan_results = vec![ScanResult {
                id: None,
                entries: Some(bsses),
                compatibility: None,
                ..ScanResult::EMPTY
            }];
            let _ = cache.update(FakeScanResultIterator::new_single_step(scan_results)).await;
            assert_eq!(cache.iter().count(), MAX_BSSES);
        }

        #[fasync::run_until_stalled(test)]
        async fn does_not_count_duplicate_bsses_toward_max_bss_limit() {
            let mut cache = RealBssCache::new();
            let duplicate_bsses = vec![
                WlanPolicyBss {
                    bssid: Some([0, 0, 0, 0, 0, 0]),
                    rssi: None,
                    frequency: None,
                    timestamp_nanos: None,
                    ..WlanPolicyBss::EMPTY
                },
                WlanPolicyBss {
                    bssid: Some([0, 0, 0, 0, 0, 0]),
                    rssi: None,
                    frequency: None,
                    timestamp_nanos: None,
                    ..WlanPolicyBss::EMPTY
                },
            ];
            let unique_bsses = (1..MAX_BSSES).map(|i| WlanPolicyBss {
                bssid: Some(
                    BssId::try_from(&i.to_le_bytes()[0..BSS_ADDR_LEN_BYTES])
                        .expect("internal error"),
                ),
                rssi: None,
                frequency: None,
                timestamp_nanos: None,
                ..WlanPolicyBss::EMPTY
            });
            let bsses: Vec<_> = duplicate_bsses.into_iter().chain(unique_bsses).collect();
            let scan_results = vec![ScanResult {
                id: None,
                entries: Some(bsses),
                compatibility: None,
                ..ScanResult::EMPTY
            }];
            let _ = cache.update(FakeScanResultIterator::new_single_step(scan_results)).await;
            assert_eq!(cache.iter().count(), MAX_BSSES);
        }
    }

    mod single_call_failure {
        use {
            super::super::*,
            fuchsia_async as fasync,
            test_doubles::{FakeScanResultIterator, StubScanResultIterator},
        };

        #[fasync::run_until_stalled(test)]
        async fn returns_ipc_error_on_fidl_error() {
            assert_eq!(
                RealBssCache::new()
                    .update(StubScanResultIterator::new(|| Err(fidl::Error::InvalidHeader)))
                    .await,
                Err(UpdateError::Ipc)
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn returns_service_error_on_general_scan_error() {
            assert_eq!(
                RealBssCache::new()
                    .update(StubScanResultIterator::new(|| Ok(Err(ScanErrorCode::GeneralError))))
                    .await,
                Err(UpdateError::Service)
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn returns_no_bsses_error_on_empty_scan_results() {
            assert_eq!(
                RealBssCache::new().update(FakeScanResultIterator::new_single_step(vec![])).await,
                Err(UpdateError::NoBsses)
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn returns_no_bsses_error_on_network_without_entries_vector() {
            assert_eq!(
                RealBssCache::new()
                    .update(FakeScanResultIterator::new_single_step(vec![ScanResult {
                        id: None,
                        entries: None,
                        compatibility: None,
                        ..ScanResult::EMPTY
                    }]))
                    .await,
                Err(UpdateError::NoBsses)
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn returns_no_bsses_error_on_network_with_empty_entries_vector() {
            assert_eq!(
                RealBssCache::new()
                    .update(FakeScanResultIterator::new_single_step(vec![ScanResult {
                        id: None,
                        entries: Some(Vec::new()),
                        compatibility: None,
                        ..ScanResult::EMPTY
                    }]))
                    .await,
                Err(UpdateError::NoBsses)
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn returns_no_bss_ids_error_on_bss_without_bssid() {
            assert_eq!(
                RealBssCache::new()
                    .update(FakeScanResultIterator::new_single_step(vec![ScanResult {
                        id: None,
                        entries: Some(vec![WlanPolicyBss {
                            bssid: None,
                            rssi: Some(-1),
                            frequency: Some(2414),
                            timestamp_nanos: Some(1),
                            ..WlanPolicyBss::EMPTY
                        }]),
                        compatibility: None,
                        ..ScanResult::EMPTY
                    }]))
                    .await,
                Err(UpdateError::NoBssIds),
            );
        }
    }

    mod multiple_calls {
        use {
            super::super::*,
            fidl_fuchsia_wlan_policy::{
                Compatibility::Supported, NetworkIdentifier, SecurityType::Wpa2,
            },
            fuchsia_async as fasync,
            test_doubles::FakeScanResultIterator,
        };

        #[fasync::run_until_stalled(test)]
        async fn is_non_empty_after_new_non_empty_data() {
            let mut cache = RealBssCache::new();
            let _ = cache
                .update(FakeScanResultIterator::new_single_step(vec![ScanResult {
                    id: None,
                    entries: Some(vec![WlanPolicyBss {
                        bssid: Some([0, 0, 0, 0, 0, 0]),
                        rssi: None,
                        frequency: None,
                        timestamp_nanos: None,
                        ..WlanPolicyBss::EMPTY
                    }]),
                    compatibility: None,
                    ..ScanResult::EMPTY
                }]))
                .await;
            let _ = cache
                .update(FakeScanResultIterator::new_single_step(vec![ScanResult {
                    id: None,
                    entries: Some(vec![WlanPolicyBss {
                        bssid: Some([1, 1, 1, 1, 1, 1]),
                        rssi: None,
                        frequency: None,
                        timestamp_nanos: None,
                        ..WlanPolicyBss::EMPTY
                    }]),
                    compatibility: None,
                    ..ScanResult::EMPTY
                }]))
                .await;

            // Note: we refrain from making a stronger assertion
            // (e.g. that only the new BSS is retained), to avoid
            // needing to revise this test if we change the caching
            // policy in the future.
            //
            // Said differently, we believe that
            // a) the assertion below will hold true under any reasonable
            //    caching policy, and
            // b) there is no pressing need to validate the more specific
            //    behavior of the current caching policy.
            assert!(cache.iter().next().is_some());
        }

        #[fasync::run_until_stalled(test)]
        async fn is_non_empty_after_new_empty_data() {
            let mut cache = RealBssCache::new();
            let _ = cache
                .update(FakeScanResultIterator::new_single_step(vec![ScanResult {
                    id: None,
                    entries: Some(vec![WlanPolicyBss {
                        bssid: Some([0, 0, 0, 0, 0, 0]),
                        rssi: None,
                        frequency: None,
                        timestamp_nanos: None,
                        ..WlanPolicyBss::EMPTY
                    }]),
                    compatibility: None,
                    ..ScanResult::EMPTY
                }]))
                .await;

            // Note: we populate everything except `entries.bssid`, to
            // ensure that the implementation doesn't short-circuit
            // due to any other data being missing.
            let _ = cache
                .update(FakeScanResultIterator::new_single_step(vec![ScanResult {
                    id: Some(NetworkIdentifier { ssid: vec![b'a'], type_: Wpa2 }),
                    entries: Some(vec![WlanPolicyBss {
                        bssid: None,
                        rssi: Some(-1),
                        frequency: Some(2412),
                        timestamp_nanos: Some(1),
                        ..WlanPolicyBss::EMPTY
                    }]),
                    compatibility: Some(Supported),
                    ..ScanResult::EMPTY
                }]))
                .await;

            // Note: for now, we make the assumption that having
            // _some_ location information is better than having none,
            // even if the data we have is old. If this changes, we
            // should remove this test.
            assert!(cache.iter().next().is_some());
        }
    }

    mod multi_step_iteration {
        use {super::super::*, fuchsia_async as fasync, test_doubles::FakeScanResultIterator};

        #[fasync::run_until_stalled(test)]
        async fn reads_all_scan_results() {
            let mut cache = RealBssCache::new();
            let result = cache
                .update(FakeScanResultIterator::new_multi_step(vec![
                    vec![ScanResult {
                        id: None,
                        entries: Some(vec![WlanPolicyBss {
                            bssid: Some([0, 0, 0, 0, 0, 0]),
                            rssi: None,
                            frequency: None,
                            timestamp_nanos: None,
                            ..WlanPolicyBss::EMPTY
                        }]),
                        compatibility: None,
                        ..ScanResult::EMPTY
                    }],
                    vec![ScanResult {
                        id: None,
                        entries: Some(vec![WlanPolicyBss {
                            bssid: Some([1, 1, 1, 1, 1, 1]),
                            rssi: None,
                            frequency: None,
                            timestamp_nanos: None,
                            ..WlanPolicyBss::EMPTY
                        }]),
                        compatibility: None,
                        ..ScanResult::EMPTY
                    }],
                ]))
                .await;
            assert_eq!(result, Ok(()));
            assert_eq!(2, cache.iter().count());
        }

        #[fasync::run_until_stalled(test)]
        async fn finds_later_bsses_even_if_first_iteration_yields_no_bsses() {
            let mut cache = RealBssCache::new();
            let result = cache
                .update(FakeScanResultIterator::new_multi_step(vec![
                    vec![ScanResult {
                        id: None,
                        entries: None,
                        compatibility: None,
                        ..ScanResult::EMPTY
                    }],
                    vec![ScanResult {
                        id: None,
                        entries: Some(vec![WlanPolicyBss {
                            bssid: Some([0, 0, 0, 0, 0, 0]),
                            rssi: None,
                            frequency: None,
                            timestamp_nanos: None,
                            ..WlanPolicyBss::EMPTY
                        }]),
                        compatibility: None,
                        ..ScanResult::EMPTY
                    }],
                ]))
                .await;
            assert_eq!(result, Ok(()));
            assert_eq!(1, cache.iter().count());
        }

        #[fasync::run_until_stalled(test)]
        async fn finds_later_bss_ids_even_if_first_iteration_yields_no_bss_ids() {
            let mut cache = RealBssCache::new();
            let result = cache
                .update(FakeScanResultIterator::new_multi_step(vec![
                    vec![ScanResult {
                        id: None,
                        entries: Some(vec![WlanPolicyBss {
                            bssid: None,
                            rssi: None,
                            frequency: None,
                            timestamp_nanos: None,
                            ..WlanPolicyBss::EMPTY
                        }]),

                        compatibility: None,
                        ..ScanResult::EMPTY
                    }],
                    vec![ScanResult {
                        id: None,
                        entries: Some(vec![WlanPolicyBss {
                            bssid: Some([0, 0, 0, 0, 0, 0]),
                            rssi: None,
                            frequency: None,
                            timestamp_nanos: None,
                            ..WlanPolicyBss::EMPTY
                        }]),
                        compatibility: None,
                        ..ScanResult::EMPTY
                    }],
                ]))
                .await;
            assert_eq!(result, Ok(()));
            assert_eq!(1, cache.iter().count());
        }
    }

    mod ipc_interactions {
        use {
            super::super::*,
            fidl_fuchsia_wlan_policy::{NetworkIdentifier, SecurityType::Wpa2},
            fuchsia_async::{self as fasync, futures},
            std::convert::TryFrom,
            test_doubles::{RawStubScanResultIterator, StubScanResultIterator},
        };

        #[fasync::run_until_stalled(test)]
        async fn stops_sending_ipcs_when_get_next_yields_fidl_error() {
            let mut cache = RealBssCache::new();
            let mut scan_results = vec![Err(fidl::Error::InvalidHeader)].into_iter();
            let _ = cache
                .update(StubScanResultIterator::new(|| {
                    scan_results.next().expect("already consumed all `scan_results`")
                }))
                .await;
        }

        #[fasync::run_until_stalled(test)]
        async fn stops_sending_ipcs_when_get_next_yields_scan_error() {
            let mut cache = RealBssCache::new();
            let mut scan_results = vec![Ok(Err(ScanErrorCode::GeneralError))].into_iter();
            let _ = cache
                .update(StubScanResultIterator::new(|| {
                    scan_results.next().expect("already consumed all `scan_results`")
                }))
                .await;
        }

        #[fasync::run_until_stalled(test)]
        async fn stops_sending_ipcs_when_get_next_yields_empty_vec() {
            let mut cache = RealBssCache::new();
            let mut scan_results = vec![Ok(Ok(vec![]))].into_iter();
            let _ = cache
                .update(StubScanResultIterator::new(|| {
                    scan_results.next().expect("already consumed all `scan_results`")
                }))
                .await;
        }

        #[fasync::run_until_stalled(test)]
        async fn drives_pending_ipc_to_completion() {
            let mut cache = RealBssCache::new();
            let mut poll_results = vec![Poll::Pending, Poll::Ready(Ok(Ok(vec![])))].into_iter();
            let mut futures = vec![futures::future::poll_fn(|cx| {
                let res = poll_results.next().expect("already consumed all `poll_results`");
                cx.waker().wake_by_ref();
                res
            })]
            .into_iter();
            let _ = cache
                .update(RawStubScanResultIterator::new(|| {
                    futures.next().expect("already consumed all `futures`")
                }))
                .await;
        }

        #[fasync::run_until_stalled(test)]
        async fn honors_max_ipc_limit() {
            let mut cache = RealBssCache::new();
            let mut scan_results = (0..MAX_IPCS)
                .map(|i| {
                    Ok(Ok(vec![ScanResult {
                        id: Some(NetworkIdentifier { ssid: i.to_le_bytes().to_vec(), type_: Wpa2 }),
                        entries: Some(vec![WlanPolicyBss {
                            bssid: Some(
                                BssId::try_from(&i.to_le_bytes()[0..BSS_ADDR_LEN_BYTES])
                                    .expect("internal error"),
                            ),
                            rssi: None,
                            frequency: None,
                            timestamp_nanos: None,
                            ..WlanPolicyBss::EMPTY
                        }]),
                        compatibility: None,
                        ..ScanResult::EMPTY
                    }]))
                })
                .collect::<Vec<_>>()
                .into_iter();
            let _ = cache
                .update(StubScanResultIterator::new(|| {
                    scan_results.next().expect("already consumed all `scan_results`")
                }))
                .await;
        }
    }
}

#[cfg(test)]
mod test_doubles {
    use {
        super::*,
        fuchsia_async::futures::future::{ready, Ready},
        std::sync::RwLock,
    };

    // Test double that returns scan results from initially provided data.
    // After exhausting the initial data, perpetually returns an empty `Vec`.
    // Useful for testing success cases.
    pub(super) struct FakeScanResultIterator {
        // Why do we need an RwLock here?
        //
        // 1) We need to return a `Vec<ScanResult>`
        // 2) `ScanResult` is not `Copy` or `Clone`
        // 3) Given 1 and 2, `get_next()` needs to move data
        //
        // Given just the constraints above, we might consider `Cell` or `RefCell`. However,
        // `ScanResultIteratorProxyInterface` is `Sync`, while `Cell` and `RefCell` are not.
        scan_results: RwLock<Vec<Vec<ScanResult>>>,
    }

    // Test double that invokes a function which yields a `GetNextResponse`.
    // Useful for testing error handling.
    pub(super) struct StubScanResultIterator<F: FnMut() -> GetNextResponse>(RwLock<F>);

    // Test double that invokes a function with yields a `GetNextResponse` `Future`.
    // Useful for testing interaction with asynchronous IPCs.
    pub(super) struct RawStubScanResultIterator<F, R>(RwLock<F>)
    where
        F: FnMut() -> R,
        R: Future<Output = GetNextResponse>;

    impl FakeScanResultIterator {
        // Returns an iterator which yields `scan_results` all at once.
        pub(super) fn new_single_step(scan_results: Vec<ScanResult>) -> Self {
            Self::new_multi_step(vec![scan_results])
        }

        // Returns an iterator which yields one element of `scan_results` at a time.
        // Note, however, that each element is _itself_ a `Vec<ScanResult>`.
        pub(super) fn new_multi_step(scan_results: Vec<Vec<ScanResult>>) -> Self {
            Self { scan_results: RwLock::new(scan_results) }
        }
    }

    impl ScanResultIteratorProxyInterface for FakeScanResultIterator {
        type GetNextResponseFut = Ready<GetNextResponse>;

        fn get_next(&self) -> Self::GetNextResponseFut {
            let mut scan_results = self.scan_results.write().expect("internal error");
            ready(Ok(Ok(if scan_results.is_empty() { Vec::new() } else { scan_results.remove(0) })))
        }
    }

    impl<F: FnMut() -> GetNextResponse + Send + Sync> StubScanResultIterator<F> {
        pub(super) fn new(get_next: F) -> Self {
            Self(RwLock::new(get_next))
        }
    }

    impl<F: FnMut() -> GetNextResponse + Send + Sync> ScanResultIteratorProxyInterface
        for StubScanResultIterator<F>
    {
        type GetNextResponseFut = Ready<GetNextResponse>;

        fn get_next(&self) -> Self::GetNextResponseFut {
            // Note: the `&mut *` here is due to https://github.com/rust-lang/rust/issues/65489
            let response_func = &mut *self.0.write().expect("internal error");
            ready(response_func())
        }
    }

    impl<F, R> RawStubScanResultIterator<F, R>
    where
        F: FnMut() -> R + Send + Sync,
        R: Future<Output = GetNextResponse> + Send,
    {
        pub(super) fn new(get_next: F) -> Self {
            Self(RwLock::new(get_next))
        }
    }

    impl<F, R> ScanResultIteratorProxyInterface for RawStubScanResultIterator<F, R>
    where
        F: FnMut() -> R + Send + Sync,
        R: Future<Output = GetNextResponse> + Send,
    {
        type GetNextResponseFut = R;

        fn get_next(&self) -> Self::GetNextResponseFut {
            // Note: the `&mut *` here is due to https://github.com/rust-lang/rust/issues/65489
            let response_func = &mut *self.0.write().expect("internal error");
            response_func()
        }
    }

    mod tests {
        mod fake_scan_result_iterator {
            use {super::super::*, fuchsia_async as fasync};

            #[fasync::run_until_stalled(test)]
            async fn single_step_yields_all_scan_results_at_once() {
                let iter = FakeScanResultIterator::new_single_step(vec![
                    ScanResult {
                        id: None,
                        entries: None,
                        compatibility: None,
                        ..ScanResult::EMPTY
                    },
                    ScanResult {
                        id: None,
                        entries: None,
                        compatibility: None,
                        ..ScanResult::EMPTY
                    },
                ]);
                assert_eq!(2, iter.get_next().await.unwrap().unwrap().len());
            }

            #[fasync::run_until_stalled(test)]
            async fn initially_empty_iterator_yields_empty_vec() {
                let iter = FakeScanResultIterator::new_single_step(Vec::new());
                assert_eq!(Vec::<ScanResult>::new(), iter.get_next().await.unwrap().unwrap());
            }

            #[fasync::run_until_stalled(test)]
            async fn emptied_iterator_yields_empty_vec() {
                let iter = FakeScanResultIterator::new_single_step(vec![ScanResult {
                    id: None,
                    entries: None,
                    compatibility: None,
                    ..ScanResult::EMPTY
                }]);
                let _ = iter.get_next().await.unwrap().unwrap();
                assert_eq!(Vec::<ScanResult>::new(), iter.get_next().await.unwrap().unwrap());
            }

            #[fasync::run_until_stalled(test)]
            async fn multi_step_yields_scan_results_iteratively() {
                let iter = FakeScanResultIterator::new_multi_step(vec![
                    vec![ScanResult {
                        id: None,
                        entries: None,
                        compatibility: None,
                        ..ScanResult::EMPTY
                    }],
                    vec![ScanResult {
                        id: None,
                        entries: None,
                        compatibility: None,
                        ..ScanResult::EMPTY
                    }],
                ]);
                assert_eq!(1, iter.get_next().await.unwrap().unwrap().len());
                assert_eq!(1, iter.get_next().await.unwrap().unwrap().len());
                assert_eq!(0, iter.get_next().await.unwrap().unwrap().len());
            }
        }
    }
}
