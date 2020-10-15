// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common_utils::common::macros::{fx_err_and_bail, with_line};
use crate::kernel::types::{SerializableCpuStats, SerializableMemoryStats};
use anyhow::Error;
use fidl_fuchsia_kernel::{StatsMarker, StatsProxy};
use fuchsia_syslog::macros::*;
use parking_lot::{RwLock, RwLockUpgradableReadGuard};

/// Perform Kernel operations.
///
/// Note this object is shared among all threads created by server.
///
#[derive(Debug)]
pub struct KernelFacade {
    proxy: RwLock<Option<StatsProxy>>,
}

impl KernelFacade {
    pub fn new() -> KernelFacade {
        KernelFacade { proxy: RwLock::new(None) }
    }

    #[cfg(test)]
    fn new_with_proxy(proxy: StatsProxy) -> Self {
        Self { proxy: RwLock::new(Some(proxy)) }
    }

    fn get_proxy(&self) -> Result<StatsProxy, Error> {
        let tag = "KernelFacade::get_proxy";

        let lock = self.proxy.upgradable_read();
        if let Some(proxy) = lock.as_ref() {
            Ok(proxy.clone())
        } else {
            let (proxy, server) = match fidl::endpoints::create_proxy::<StatsMarker>() {
                Ok(r) => r,
                Err(e) => fx_err_and_bail!(
                    &with_line!(tag),
                    format_err!("Failed to get Kernel Stats proxy {:?}", e)
                ),
            };

            fdio::service_connect("/svc/fuchsia.kernel.Stats", server.into_channel())?;
            *RwLockUpgradableReadGuard::upgrade(lock) = Some(proxy.clone());
            Ok(proxy)
        }
    }

    pub async fn get_memory_stats(&self) -> Result<SerializableMemoryStats, Error> {
        Ok(Into::into(self.get_proxy()?.get_memory_stats().await?))
    }

    pub async fn get_cpu_stats(&self) -> Result<SerializableCpuStats, Error> {
        Ok(Into::into(self.get_proxy()?.get_cpu_stats().await?))
    }

    pub async fn get_all_stats(
        &self,
    ) -> Result<(SerializableMemoryStats, SerializableCpuStats), Error> {
        let proxy = self.get_proxy()?;
        let memory_stats = proxy.get_memory_stats().await?;
        let cpu_stats = proxy.get_cpu_stats().await?;
        Ok((Into::into(memory_stats), Into::into(cpu_stats)))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::kernel::types::SerializablePerCpuStats;
    use fidl_fuchsia_kernel::StatsRequest;
    use futures::{future::Future, join, stream::StreamExt};
    use matches::assert_matches;

    struct MockKernelBuilder {
        expected: Vec<Box<dyn FnOnce(StatsRequest) + Send + 'static>>,
    }

    impl MockKernelBuilder {
        fn new() -> Self {
            Self { expected: vec![] }
        }

        fn push(mut self, request: impl FnOnce(StatsRequest) + Send + 'static) -> Self {
            self.expected.push(Box::new(request));
            self
        }

        fn expect_get_memory_stats(self, res: SerializableMemoryStats) -> Self {
            self.push(move |req| match req {
                StatsRequest::GetMemoryStats { responder } => {
                    responder.send(Into::into(res)).unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_get_cpu_stats(self, res: SerializableCpuStats) -> Self {
            self.push(move |req| match req {
                StatsRequest::GetCpuStats { responder } => {
                    responder.send(&mut Into::into(res)).unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn expect_get_all_stats(
            self,
            memory: SerializableMemoryStats,
            cpu: SerializableCpuStats,
        ) -> Self {
            self.push(move |req| match req {
                StatsRequest::GetMemoryStats { responder } => {
                    responder.send(Into::into(memory)).unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
            .push(move |req| match req {
                StatsRequest::GetCpuStats { responder } => {
                    responder.send(&mut Into::into(cpu)).unwrap()
                }
                req => panic!("unexpected request: {:?}", req),
            })
        }

        fn build(self) -> (KernelFacade, impl Future<Output = ()>) {
            let (proxy, mut stream) =
                fidl::endpoints::create_proxy_and_stream::<StatsMarker>().unwrap();
            let fut = async move {
                for expected in self.expected {
                    expected(stream.next().await.unwrap().unwrap());
                }
                assert_matches!(stream.next().await, None);
            };

            (KernelFacade::new_with_proxy(proxy), fut)
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_memory_stats_ok() {
        let (facade, kernel) = MockKernelBuilder::new()
            .expect_get_memory_stats(SerializableMemoryStats {
                total_bytes: Some(2993),
                free_bytes: Some(2311),
                wired_bytes: Some(291),
                total_heap_bytes: Some(49),
                free_heap_bytes: Some(11),
                vmo_bytes: Some(888),
                mmu_overhead_bytes: Some(742),
                ipc_bytes: Some(392),
                other_bytes: Some(62),
            })
            .build();
        let test = async move {
            assert_matches!(
                facade.get_memory_stats().await,
                Ok(SerializableMemoryStats {
                    total_bytes: Some(2993),
                    free_bytes: Some(2311),
                    wired_bytes: Some(291),
                    total_heap_bytes: Some(49),
                    free_heap_bytes: Some(11),
                    vmo_bytes: Some(888),
                    mmu_overhead_bytes: Some(742),
                    ipc_bytes: Some(392),
                    other_bytes: Some(62),
                })
            );
        };

        join!(kernel, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_cpu_stats_ok() {
        let (facade, kernel) = MockKernelBuilder::new()
            .expect_get_cpu_stats(SerializableCpuStats {
                actual_num_cpus: 2,
                per_cpu_stats: Some(vec![
                    SerializablePerCpuStats {
                        cpu_number: Some(0),
                        flags: Some(0),
                        idle_time: Some(291),
                        reschedules: Some(314),
                        context_switches: Some(34),
                        irq_preempts: Some(421),
                        yields: Some(5),
                        ints: Some(618),
                        timer_ints: Some(991),
                        timers: Some(10),
                        page_faults: Some(94),
                        exceptions: Some(620),
                        syscalls: Some(555),
                        reschedule_ipis: Some(87),
                        generic_ipis: Some(7),
                    },
                    SerializablePerCpuStats {
                        cpu_number: Some(1),
                        flags: Some(0),
                        idle_time: Some(10),
                        reschedules: Some(532),
                        context_switches: Some(829),
                        irq_preempts: Some(7),
                        yields: Some(768),
                        ints: Some(63),
                        timer_ints: Some(4),
                        timers: Some(183),
                        page_faults: Some(853),
                        exceptions: Some(75),
                        syscalls: Some(422),
                        reschedule_ipis: Some(74),
                        generic_ipis: Some(89),
                    },
                ]),
            })
            .build();
        let test = async move {
            assert_matches!(
                facade.get_cpu_stats().await,
                Ok(stats) if stats == SerializableCpuStats{
                    actual_num_cpus: 2,
                    per_cpu_stats: Some(vec![
                        SerializablePerCpuStats {
                            cpu_number: Some(0),
                            flags: Some(0),
                            idle_time: Some(291),
                            reschedules: Some(314),
                            context_switches: Some(34),
                            irq_preempts: Some(421),
                            yields: Some(5),
                            ints: Some(618),
                            timer_ints: Some(991),
                            timers: Some(10),
                            page_faults: Some(94),
                            exceptions: Some(620),
                            syscalls: Some(555),
                            reschedule_ipis: Some(87),
                            generic_ipis: Some(7),
                        },
                        SerializablePerCpuStats {
                            cpu_number: Some(1),
                            flags: Some(0),
                            idle_time: Some(10),
                            reschedules: Some(532),
                            context_switches: Some(829),
                            irq_preempts: Some(7),
                            yields: Some(768),
                            ints: Some(63),
                            timer_ints: Some(4),
                            timers: Some(183),
                            page_faults: Some(853),
                            exceptions: Some(75),
                            syscalls: Some(422),
                            reschedule_ipis: Some(74),
                            generic_ipis: Some(89),
                        }
                    ])
                }
            );
        };

        join!(kernel, test);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_all_stats_ok() {
        let (facade, kernel) = MockKernelBuilder::new()
            .expect_get_all_stats(
                SerializableMemoryStats {
                    total_bytes: Some(2993),
                    free_bytes: Some(2311),
                    wired_bytes: Some(291),
                    total_heap_bytes: Some(49),
                    free_heap_bytes: Some(11),
                    vmo_bytes: Some(888),
                    mmu_overhead_bytes: Some(742),
                    ipc_bytes: Some(392),
                    other_bytes: Some(62),
                },
                SerializableCpuStats {
                    actual_num_cpus: 2,
                    per_cpu_stats: Some(vec![
                        SerializablePerCpuStats {
                            cpu_number: Some(0),
                            flags: Some(0),
                            idle_time: Some(291),
                            reschedules: Some(314),
                            context_switches: Some(34),
                            irq_preempts: Some(421),
                            yields: Some(5),
                            ints: Some(618),
                            timer_ints: Some(991),
                            timers: Some(10),
                            page_faults: Some(94),
                            exceptions: Some(620),
                            syscalls: Some(555),
                            reschedule_ipis: Some(87),
                            generic_ipis: Some(7),
                        },
                        SerializablePerCpuStats {
                            cpu_number: Some(1),
                            flags: Some(0),
                            idle_time: Some(10),
                            reschedules: Some(532),
                            context_switches: Some(829),
                            irq_preempts: Some(7),
                            yields: Some(768),
                            ints: Some(63),
                            timer_ints: Some(4),
                            timers: Some(183),
                            page_faults: Some(853),
                            exceptions: Some(75),
                            syscalls: Some(422),
                            reschedule_ipis: Some(74),
                            generic_ipis: Some(89),
                        },
                    ]),
                },
            )
            .build();
        let test = async move {
            assert_matches!(
                facade.get_all_stats().await,
                Ok(stats) if stats == (
                    SerializableMemoryStats {
                        total_bytes: Some(2993),
                        free_bytes: Some(2311),
                        wired_bytes: Some(291),
                        total_heap_bytes: Some(49),
                        free_heap_bytes: Some(11),
                        vmo_bytes: Some(888),
                        mmu_overhead_bytes: Some(742),
                        ipc_bytes: Some(392),
                        other_bytes: Some(62),
                    },
                    SerializableCpuStats{
                        actual_num_cpus: 2,
                        per_cpu_stats: Some(vec![
                            SerializablePerCpuStats {
                                cpu_number: Some(0),
                                flags: Some(0),
                                idle_time: Some(291),
                                reschedules: Some(314),
                                context_switches: Some(34),
                                irq_preempts: Some(421),
                                yields: Some(5),
                                ints: Some(618),
                                timer_ints: Some(991),
                                timers: Some(10),
                                page_faults: Some(94),
                                exceptions: Some(620),
                                syscalls: Some(555),
                                reschedule_ipis: Some(87),
                                generic_ipis: Some(7),
                            },
                            SerializablePerCpuStats {
                                cpu_number: Some(1),
                                flags: Some(0),
                                idle_time: Some(10),
                                reschedules: Some(532),
                                context_switches: Some(829),
                                irq_preempts: Some(7),
                                yields: Some(768),
                                ints: Some(63),
                                timer_ints: Some(4),
                                timers: Some(183),
                                page_faults: Some(853),
                                exceptions: Some(75),
                                syscalls: Some(422),
                                reschedule_ipis: Some(74),
                                generic_ipis: Some(89),
                            }
                        ])
                    }
                )
            );
        };

        join!(kernel, test);
    }
}
