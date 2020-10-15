// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_kernel::{CpuStats, MemoryStats, PerCpuStats};
use serde::{Deserialize, Serialize};
use std::convert::TryFrom;

#[derive(Deserialize, Debug, PartialEq, Serialize)]
pub struct SerializableMemoryStats {
    pub total_bytes: Option<u64>,
    pub free_bytes: Option<u64>,
    pub wired_bytes: Option<u64>,
    pub total_heap_bytes: Option<u64>,
    pub free_heap_bytes: Option<u64>,
    pub vmo_bytes: Option<u64>,
    pub mmu_overhead_bytes: Option<u64>,
    pub ipc_bytes: Option<u64>,
    pub other_bytes: Option<u64>,
}

impl From<MemoryStats> for SerializableMemoryStats {
    fn from(memory_stats: MemoryStats) -> Self {
        SerializableMemoryStats {
            total_bytes: memory_stats.total_bytes,
            free_bytes: memory_stats.free_bytes,
            wired_bytes: memory_stats.wired_bytes,
            total_heap_bytes: memory_stats.total_heap_bytes,
            free_heap_bytes: memory_stats.free_heap_bytes,
            vmo_bytes: memory_stats.vmo_bytes,
            mmu_overhead_bytes: memory_stats.mmu_overhead_bytes,
            ipc_bytes: memory_stats.ipc_bytes,
            other_bytes: memory_stats.other_bytes,
        }
    }
}

impl From<SerializableMemoryStats> for MemoryStats {
    fn from(memory_stats: SerializableMemoryStats) -> Self {
        MemoryStats {
            total_bytes: memory_stats.total_bytes,
            free_bytes: memory_stats.free_bytes,
            wired_bytes: memory_stats.wired_bytes,
            total_heap_bytes: memory_stats.total_heap_bytes,
            free_heap_bytes: memory_stats.free_heap_bytes,
            vmo_bytes: memory_stats.vmo_bytes,
            mmu_overhead_bytes: memory_stats.mmu_overhead_bytes,
            ipc_bytes: memory_stats.ipc_bytes,
            other_bytes: memory_stats.other_bytes,
        }
    }
}

#[derive(Deserialize, Debug, PartialEq, Serialize)]
pub struct SerializablePerCpuStats {
    pub cpu_number: Option<u32>,
    pub flags: Option<u32>,
    pub idle_time: Option<i64>,
    pub reschedules: Option<u64>,
    pub context_switches: Option<u64>,
    pub irq_preempts: Option<u64>,
    pub yields: Option<u64>,
    pub ints: Option<u64>,
    pub timer_ints: Option<u64>,
    pub timers: Option<u64>,
    pub page_faults: Option<u64>,
    pub exceptions: Option<u64>,
    pub syscalls: Option<u64>,
    pub reschedule_ipis: Option<u64>,
    pub generic_ipis: Option<u64>,
}

impl From<PerCpuStats> for SerializablePerCpuStats {
    fn from(per_cpu_stats: PerCpuStats) -> Self {
        SerializablePerCpuStats {
            cpu_number: per_cpu_stats.cpu_number,
            flags: per_cpu_stats.flags,
            idle_time: per_cpu_stats.idle_time,
            reschedules: per_cpu_stats.reschedules,
            context_switches: per_cpu_stats.context_switches,
            irq_preempts: per_cpu_stats.irq_preempts,
            yields: per_cpu_stats.yields,
            ints: per_cpu_stats.ints,
            timer_ints: per_cpu_stats.timer_ints,
            timers: per_cpu_stats.timers,
            page_faults: per_cpu_stats.page_faults,
            exceptions: per_cpu_stats.exceptions,
            syscalls: per_cpu_stats.syscalls,
            reschedule_ipis: per_cpu_stats.reschedule_ipis,
            generic_ipis: per_cpu_stats.generic_ipis,
        }
    }
}

impl From<SerializablePerCpuStats> for PerCpuStats {
    fn from(per_cpu_stats: SerializablePerCpuStats) -> Self {
        PerCpuStats {
            cpu_number: per_cpu_stats.cpu_number,
            flags: per_cpu_stats.flags,
            idle_time: per_cpu_stats.idle_time,
            reschedules: per_cpu_stats.reschedules,
            context_switches: per_cpu_stats.context_switches,
            irq_preempts: per_cpu_stats.irq_preempts,
            yields: per_cpu_stats.yields,
            ints: per_cpu_stats.ints,
            timer_ints: per_cpu_stats.timer_ints,
            timers: per_cpu_stats.timers,
            page_faults: per_cpu_stats.page_faults,
            exceptions: per_cpu_stats.exceptions,
            syscalls: per_cpu_stats.syscalls,
            reschedule_ipis: per_cpu_stats.reschedule_ipis,
            generic_ipis: per_cpu_stats.generic_ipis,
        }
    }
}
#[derive(Deserialize, Debug, PartialEq, Serialize)]
pub struct SerializableCpuStats {
    pub actual_num_cpus: u64,
    pub per_cpu_stats: Option<Vec<SerializablePerCpuStats>>,
}

impl From<CpuStats> for SerializableCpuStats {
    fn from(cpu_stats: CpuStats) -> Self {
        SerializableCpuStats {
            actual_num_cpus: cpu_stats.actual_num_cpus,
            per_cpu_stats: cpu_stats.per_cpu_stats.map(|x| x.into_iter().map(Into::into).collect()),
        }
    }
}

impl From<SerializableCpuStats> for CpuStats {
    fn from(cpu_stats: SerializableCpuStats) -> Self {
        CpuStats {
            actual_num_cpus: cpu_stats.actual_num_cpus,
            per_cpu_stats: cpu_stats.per_cpu_stats.map(|x| x.into_iter().map(Into::into).collect()),
        }
    }
}
pub enum KernelMethod {
    GetMemoryStats,
    GetCpuStats,
    GetAllStats, // Gets memory and CPU stats
    // TODO(rdzhuang): Other functions if we need it
    UndefinedFunc,
}

impl TryFrom<(&str, serde_json::value::Value)> for KernelMethod {
    type Error = Error;
    fn try_from(input: (&str, serde_json::value::Value)) -> Result<Self, Self::Error> {
        match input.0 {
            "GetMemoryStats" => Ok(KernelMethod::GetMemoryStats),
            "GetCpuStats" => Ok(KernelMethod::GetCpuStats),
            "GetAllStats" => Ok(KernelMethod::GetAllStats),
            _ => Ok(KernelMethod::UndefinedFunc),
        }
    }
}
