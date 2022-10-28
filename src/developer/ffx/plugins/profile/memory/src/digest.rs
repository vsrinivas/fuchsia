// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Capture and Digest related functionality.

/// Types and utilities related to the raw data produced by
/// `MemoryMonitor`.
pub mod raw {
    use serde::{Deserialize, Serialize};

    /// Slightly modified copy of the structure returned by the
    /// `zx_object_get_info` syscall when invoked with the
    /// `ZX_INFO_KMEM_STATS_EXTENDED` topic. Refer to this syscall's
    /// documentation (and implementation) for more details.
    ///
    /// Some notable points:
    ///   * Some memory is not covered (because it belongs to an
    ///   uncovered category).
    ///   * Some memory is counted twice in different fields
    ///   (`total_heap` counts memory in `free_heap`, for instance)
    ///   * Data collection is racy and best effort, which can lead to
    ///   some inaccuracies (a page can be counted once in allocated
    ///   and free memory, for instance, if it was allocated or
    ///   deallocated during the collection).
    ///   * The report is from a kernel centric point of view, and
    ///   distinguishes data reserved to the kernel from data reserved
    ///   to userspace.
    ///   * The report assumes expert knowledge in memory
    ///   management. This type includes every single field of the
    ///   report for completion and to facilitate deserialization, but
    ///   it is likely that only a small subset of its fields will
    ///   ever get used in this crate.
    #[derive(Serialize, Deserialize, PartialEq, Debug, Default, Clone)]
    pub struct Kernel {
        /// Total physical memory available to the system, in bytes.
        pub total: u64,
        /// Unallocated memory, in bytes.
        pub free: u64,
        /// Memory reserved by and mapped into the kernel for reasons
        /// not covered by other fields in this struct, in
        /// bytes. Typically for readonly data like the ram disk and
        /// kernel image, and for early-boot dynamic memory.
        pub wired: u64,
        /// Memory allocated to the kernel heap, in bytes.
        pub total_heap: u64,
        /// Portion of `total_heap` that is not in use, in bytes.
        pub free_heap: u64,
        /// Memory committed to (reserved for, but not necessarily
        /// used by) VMOs, both kernel and user, in bytes. A superset
        /// of all userspace memory. Does not include certain VMOs
        /// that fall under `wired`.
        pub vmo: u64,
        /// Memory committed to pager-backed VMOs, in bytes.
        pub vmo_pager_total: u64,
        /// Memory committed to pager-backed VMOs, in bytes, that has
        /// been most recently accessed, and would not be eligible for
        /// eviction by the kernel under memory pressure.
        pub vmo_pager_newest: u64,
        /// Memory committed to pager-backed VMOs, in bytes, that has
        /// been least recently accessed, and would be the first to be
        /// evicted by the kernel under memory pressure.
        pub vmo_pager_oldest: u64,
        /// Memory committed to discardable VMOs, in bytes, that is
        /// currently locked, or unreclaimable by the kernel under
        /// memory pressure.
        pub vmo_discardable_locked: u64,
        /// Memory committed to discardable VMOs, in bytes, that is
        /// currently unlocked, or reclaimable by the kernel under
        /// memory pressure.
        pub vmo_discardable_unlocked: u64,
        /// Memory used for architecture-specific MMU (Memory
        /// Management Unit) metadata like page tables, in bytes.
        pub mmu: u64,
        /// Memory in use by IPC, in bytes.
        pub ipc: u64,
        /// Non-free memory that isn't accounted for in any other
        /// field, in bytes.
        pub other: u64,
    }

    /// Placeholder to validate the JSON schema. None of those fields
    /// are ever used, but they are documented here as a reference.
    #[derive(Serialize, Deserialize, PartialEq, Debug, Clone)]
    pub struct ProcessHeaders {
        pub koid: String,
        pub name: String,
        pub vmos: String,
    }

    impl Default for ProcessHeaders {
        fn default() -> ProcessHeaders {
            ProcessHeaders {
                koid: "koid".to_string(),
                name: "name".to_string(),
                vmos: "vmos".to_string(),
            }
        }
    }

    #[derive(Serialize, Deserialize, PartialEq, Debug, Clone)]
    pub struct ProcessData {
        /// Kernel Object ID. See related Fuchsia Kernel concept.
        pub koid: u64,
        pub name: String,
        pub vmos: Vec<u64>,
    }

    #[derive(Serialize, Deserialize, PartialEq, Debug, Clone)]
    #[serde(untagged)]
    pub enum Process {
        /// Headers describing the meaning of the data.
        Headers(ProcessHeaders),
        /// The actual data.
        Data(ProcessData),
    }

    /// Placeholder to validate the JSON schema. None of those fields
    /// are ever used, but they are documented here as a reference.
    #[derive(Serialize, Deserialize, PartialEq, Debug, Clone)]
    pub struct VmoHeaders {
        pub koid: String,
        pub name: String,
        pub parent_koid: String,
        pub committed_bytes: String,
        pub allocated_bytes: String,
    }

    impl Default for VmoHeaders {
        fn default() -> Self {
            VmoHeaders {
                koid: "koid".to_string(),
                name: "name".to_string(),
                parent_koid: "parent_koid".to_string(),
                committed_bytes: "committed_bytes".to_string(),
                allocated_bytes: "allocated_bytes".to_string(),
            }
        }
    }

    #[derive(Serialize, Deserialize, PartialEq, Debug, Clone)]
    pub struct VmoData {
        /// Kernel Object ID. See related Fuchsia Kernel concept.
        pub koid: u64,
        pub name: u64,
        pub parent_koid: u64,
        pub committed_bytes: u64,
        pub allocated_bytes: u64,
    }

    #[derive(Serialize, Deserialize, PartialEq, Debug, Clone)]
    #[serde(untagged)]
    pub enum Vmo {
        /// Headers describing the meaning of the data.
        Headers(VmoHeaders),
        /// The actual data.
        Data(VmoData),
    }

    /// Capture exported by `MemoryMonitor`.
    /// This part of the schema that contains information on the
    /// processes and VMOs.
    #[derive(Serialize, Deserialize, PartialEq, Debug, Clone)]
    #[serde(rename_all = "PascalCase")]
    pub struct Capture {
        /// A monotonic time (in ns).
        pub time: u64,
        pub kernel: Kernel,
        pub processes: Vec<Process>,
        /// Names of the VMOs mentioned in the `Capture`.
        pub vmo_names: Vec<String>,
        pub vmos: Vec<Vmo>,
    }

    /// Defines a memory bucket.
    #[derive(Serialize, Deserialize, PartialEq, Debug)]
    pub struct BucketDefinition {
        /// The Cobalt event code associated with this bucket.
        pub event_code: u64,
        /// The human-readable name of the bucket.
        pub name: String,
        /// String saying which process to match. Will be interpreted as a regex.
        /// If the string is empty, will be interpreted as ".*".
        pub process: String,
        /// Regex saying which VMOs to match. Will be interpreted as a regex.
        /// If the string is empty, will be interpreted as ".*".
        pub vmo: String,
    }

    /// Digests exported by `MemoryMonitor`.
    /// This corresponds to the schema of the data that is transferred
    /// by `MemoryMonitor::WriteJsonCaptureAndBuckets`'s API.
    #[derive(Serialize, Deserialize, PartialEq, Debug)]
    pub struct MemoryMonitorOutput {
        #[serde(rename(serialize = "Buckets", deserialize = "Buckets"))]
        pub buckets_definitions: Vec<BucketDefinition>,
        #[serde(rename(serialize = "Capture", deserialize = "Capture"))]
        pub capture: Capture,
    }
}

/// Types and utilities to produce and manipulate processed summaries
/// suitable for user-facing consumption.
pub mod processed {
    use crate::bucket::compute_buckets;
    use crate::bucket::Bucket;
    use crate::digest::processed;
    use crate::digest::raw;
    use serde::Serialize;

    use std::collections::{HashMap, HashSet};
    use std::iter::FromIterator;

    /// Per process memory attribution.
    #[derive(Serialize, PartialEq, Debug)]
    pub struct RetainedMemory {
        /// Total size, in bytes, of VMOs exclusively retained
        /// (directly, or indirectly via children VMOs) by the
        /// process.
        pub private: u64,
        /// Total size, in bytes, of VMOs retained (directly, or
        /// indirectly via children VMOs) by several processes. The
        /// cost of each VMO is shared evenly among all its retaining
        /// processes.
        pub scaled: u64,
        /// Total size, in bytes, of VMOs retained (exclusively or
        /// not, directly, or indirectly via children VMOs) by this
        /// process.
        pub total: u64,
    }

    /// Summary of memory-related data for a given process.
    #[derive(Serialize, PartialEq, Debug)]
    pub struct Process {
        /// Kernel Object ID. See related Fuchsia Kernel concept.
        pub koid: u64,
        pub name: String,
        pub memory: RetainedMemory,
        /// Mapping between the names of the VMOs related to this
        /// process, and their retained memory.
        pub name_to_memory: HashMap<String, RetainedMemory>,
        /// Set of vmo koids related to this process.
        pub vmos: HashSet<u64>,
    }

    /// Holds all the data relevant to a Vmo.
    pub struct Vmo {
        pub koid: u64,
        pub name: String,
        pub parent_koid: u64,
        pub committed_bytes: u64,
        pub allocated_bytes: u64,
    }

    pub type Kernel = raw::Kernel;

    /// Aggregated, processed digest of memory use in a system.
    #[derive(Serialize, PartialEq, Debug)]
    pub struct Digest {
        /// A monotonic time (in ns).
        pub time: u64,
        /// The sum of all the committed bytes in all VMOs.
        pub total_committed_bytes_in_vmos: u64,
        /// Kernel data.
        pub kernel: Kernel,
        /// Process data.
        pub processes: Vec<Process>,
        /// Buckets
        pub buckets: Vec<Bucket>,
    }

    /// Perform ad-hoc VMO name canonicalization. Computes equivalence
    /// classes for certain VMO names, for aggregation purposes.
    fn rename(name: &str) -> &str {
        lazy_static::lazy_static! {
        /// Default, global regex match.
        static ref RULES: [(regex::Regex, &'static str); 9] = [
            (regex::Regex::new("^blob-[0-9a-f]+$").unwrap(), "[blobs]"),
            (regex::Regex::new("^mrkl-[0-9a-f]+$").unwrap(), "[blob-merkles]"),
            (regex::Regex::new("^inactive-blob-[0-9a-f]+$").unwrap(), "[inactive blobs]"),
            (regex::Regex::new("^thrd_t:0x.*|initial-thread|pthread_t:0x.*$").unwrap(), "[stacks]"),
            (regex::Regex::new("^data[0-9]*:.*$").unwrap(), "[data]"),
            (regex::Regex::new("^relro:.*$").unwrap(), "[relro]"),
            (regex::Regex::new("^$").unwrap(), "[unnamed]"),
            (regex::Regex::new("^scudo:.*$").unwrap(), "[scudo]"),
            (regex::Regex::new("^.*\\.so.*$").unwrap(), "[libraries]"),
        ];
        }
        RULES.iter().find(|(regex, _)| regex.is_match(name)).map_or(name, |rule| rule.1)
    }

    /// Conversion trait from a raw digest to a human-friendly
    /// processed digest. Data is aggregated, normalized, sorted.
    pub fn digest_from_memory_monitor_output(
        memory_monitor_output: raw::MemoryMonitorOutput,
        bucketize: bool,
    ) -> Digest {
        let capture = memory_monitor_output.capture;
        // Index processes by koid.
        let koid_to_process = {
            let mut koid_to_process = HashMap::new();
            for process in capture.processes {
                if let raw::Process::Data(raw::ProcessData { koid, .. }) = process {
                    koid_to_process.insert(koid, process);
                }
            }
            koid_to_process
        };
        // Index VMOs by koid.
        let koid_to_vmo = {
            let mut koid_to_vmo = HashMap::new();
            for vmo in capture.vmos {
                if let raw::Vmo::Data(raw::VmoData {
                    koid,
                    parent_koid,
                    name,
                    committed_bytes,
                    allocated_bytes,
                }) = vmo
                {
                    let vmo_name_index = name as usize;
                    let vmo_name_string = capture.vmo_names[vmo_name_index].clone();
                    koid_to_vmo.insert(
                        koid,
                        processed::Vmo {
                            koid,
                            parent_koid,
                            name: vmo_name_string,
                            committed_bytes,
                            allocated_bytes,
                        },
                    );
                }
            }
            koid_to_vmo
        };
        let mut processes: Vec<Process> = {
            let mut processes = vec![];
            for (koid, process) in koid_to_process {
                if let raw::Process::Data(raw::ProcessData { name, vmos, .. }) = process {
                    let p = Process {
                        koid,
                        name: name.to_string(),
                        memory: RetainedMemory { private: 0, scaled: 0, total: 0 },
                        name_to_memory: HashMap::new(),
                        vmos: HashSet::from_iter(vmos),
                    };
                    if !p.vmos.is_empty() {
                        processes.push(p);
                    }
                }
            }
            processes
        };
        // Mapping from each VMO koid to the set of every processes that refer
        // this VMO, either directly, or indirectly via related VMOs.
        // Also maps process to VMOs either directly, or indirectly.
        let (vmo_to_charged_processes, process_to_charged_vmos) = {
            let mut vmo_to_charged_processes: HashMap<u64, HashSet<u64>> = HashMap::new();
            let mut process_to_charged_vmos: HashMap<u64, HashSet<u64>> = HashMap::new();
            for process in processes.iter() {
                for mut vmo_koid in process.vmos.iter() {
                    // In case of related VMOs, follow parents until reaching
                    // the root VMO.
                    while *vmo_koid != 0 {
                        vmo_to_charged_processes.entry(*vmo_koid).or_default().insert(process.koid);
                        process_to_charged_vmos.entry(process.koid).or_default().insert(*vmo_koid);
                        if let Some(processed::Vmo { parent_koid, .. }) = koid_to_vmo.get(&vmo_koid)
                        {
                            vmo_koid = parent_koid;
                        } else {
                            // If we reach this branch, it means that the report
                            // mentions a process that holds a handle to a VMO,
                            // and that either this VMO or one of its ascendants
                            // is absent from the VMO list. This might be a bug.
                            eprintln!(
                                "Process {:?} refers (directly or indirectly) to unknown VMO {}",
                                process, vmo_koid
                            );
                            eprintln!(
                                "Please consider reporting this issue to the plugin's authors."
                            );
                            break;
                        }
                    }
                }
            }
            (vmo_to_charged_processes, process_to_charged_vmos)
        };

        // Compute per-process, aggregated sizes.
        for mut process in processes.iter_mut() {
            if let Some(vmo_koids) = process_to_charged_vmos.get(&process.koid) {
                for vmo_koid in vmo_koids.iter() {
                    if let Some(processed::Vmo { name, committed_bytes, .. }) =
                        koid_to_vmo.get(&vmo_koid)
                    {
                        let share_count = match vmo_to_charged_processes.get(&vmo_koid) {
                            Some(v) => v.len() as u64,
                            None => unreachable!(),
                        };
                        let name = rename(name).to_string();
                        let mut name_sizes = process
                            .name_to_memory
                            .entry(name)
                            .or_insert(RetainedMemory { private: 0, scaled: 0, total: 0 });
                        name_sizes.total += committed_bytes;
                        process.memory.total += committed_bytes;
                        name_sizes.scaled += committed_bytes / share_count;
                        process.memory.scaled += committed_bytes / share_count;
                        if share_count == 1 {
                            name_sizes.private += committed_bytes;
                            process.memory.private += committed_bytes;
                        }
                    }
                }
            }
        }

        processes.sort_unstable_by(|a, b| b.memory.private.cmp(&a.memory.private));
        let total_committed_vmo = {
            let mut total = 0;
            for (_, vmo) in &koid_to_vmo {
                total += vmo.committed_bytes;
            }
            total
        };
        let kernel_vmo = capture.kernel.vmo.saturating_sub(total_committed_vmo);

        let buckets = if bucketize {
            compute_buckets(&memory_monitor_output.buckets_definitions, &processes, &koid_to_vmo)
        } else {
            vec![]
        };

        Digest {
            time: capture.time,
            total_committed_bytes_in_vmos: total_committed_vmo,
            kernel: raw::Kernel { vmo: kernel_vmo, ..capture.kernel },
            processes,
            buckets,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::{HashMap, HashSet};

    #[test]
    fn raw_to_processed_test() {
        let raw = raw::Capture {
            time: 1234567,
            kernel: raw::Kernel::default(),
            processes: vec![
                raw::Process::Headers(raw::ProcessHeaders::default()),
                // Process with one shared, root VMO
                raw::Process::Data(raw::ProcessData {
                    koid: 2,
                    name: "process2".to_string(),
                    vmos: vec![1],
                }),
                // Process with two VMOs
                raw::Process::Data(raw::ProcessData {
                    koid: 3,
                    name: "process3".to_string(),
                    vmos: vec![1, 2],
                }),
                // Process with one private, root VMO
                raw::Process::Data(raw::ProcessData {
                    koid: 4,
                    name: "process4".to_string(),
                    vmos: vec![3],
                }),
                // Process with one child VMO
                raw::Process::Data(raw::ProcessData {
                    koid: 5,
                    name: "process5".to_string(),
                    vmos: vec![2],
                }),
            ],
            vmo_names: vec!["vmo1".to_string(), "vmo2".to_string(), "vmo3".to_string()],
            vmos: vec![
                raw::Vmo::Headers(raw::VmoHeaders::default()),
                raw::Vmo::Data(raw::VmoData {
                    koid: 1,
                    name: 0,
                    parent_koid: 0,
                    committed_bytes: 300,
                    allocated_bytes: 300,
                }),
                raw::Vmo::Data(raw::VmoData {
                    koid: 2,
                    name: 1,
                    parent_koid: 1,
                    committed_bytes: 100,
                    allocated_bytes: 100,
                }),
                raw::Vmo::Data(raw::VmoData {
                    koid: 3,
                    name: 2,
                    parent_koid: 0,
                    committed_bytes: 100,
                    allocated_bytes: 100,
                }),
            ],
        };
        let expected_processes_per_koid = HashMap::from([
            (
                2,
                processed::Process {
                    koid: 2,
                    name: "process2".to_string(),
                    memory: processed::RetainedMemory { private: 0, scaled: 100, total: 300 },
                    name_to_memory: {
                        let mut result = HashMap::new();
                        result.insert(
                            "vmo1".to_string(),
                            processed::RetainedMemory { private: 0, scaled: 100, total: 300 },
                        );
                        result
                    },
                    vmos: {
                        let mut result = HashSet::new();
                        result.insert(1);
                        result
                    },
                },
            ),
            (
                3,
                processed::Process {
                    koid: 3,
                    name: "process3".to_string(),
                    memory: processed::RetainedMemory { private: 0, scaled: 150, total: 400 },
                    name_to_memory: {
                        let mut result = HashMap::new();
                        result.insert(
                            "vmo1".to_string(),
                            processed::RetainedMemory { private: 0, scaled: 100, total: 300 },
                        );
                        result.insert(
                            "vmo2".to_string(),
                            processed::RetainedMemory { private: 0, scaled: 50, total: 100 },
                        );
                        result
                    },
                    vmos: {
                        let mut result = HashSet::new();
                        result.insert(1);
                        result.insert(2);
                        result
                    },
                },
            ),
            (
                5,
                processed::Process {
                    koid: 5,
                    name: "process5".to_string(),
                    memory: processed::RetainedMemory { private: 0, scaled: 150, total: 400 },
                    name_to_memory: {
                        let mut result = HashMap::new();
                        result.insert(
                            "vmo2".to_string(),
                            processed::RetainedMemory { private: 0, scaled: 50, total: 100 },
                        );
                        result.insert(
                            "vmo1".to_string(),
                            processed::RetainedMemory { private: 0, scaled: 100, total: 300 },
                        );
                        result
                    },
                    vmos: {
                        let mut result = HashSet::new();
                        result.insert(2);
                        result
                    },
                },
            ),
            (
                4,
                processed::Process {
                    koid: 4,
                    name: "process4".to_string(),
                    memory: processed::RetainedMemory { private: 100, scaled: 100, total: 100 },
                    name_to_memory: {
                        let mut result = HashMap::new();
                        result.insert(
                            "vmo3".to_string(),
                            processed::RetainedMemory { private: 100, scaled: 100, total: 100 },
                        );
                        result
                    },
                    vmos: {
                        let mut result = HashSet::new();
                        result.insert(3);
                        result
                    },
                },
            ),
        ]);
        let processed = processed::digest_from_memory_monitor_output(
            raw::MemoryMonitorOutput { capture: raw, buckets_definitions: Vec::new() },
            false,
        );
        // Check that the process list is sorted
        {
            let mut pairs = processed.processes.windows(2);
            while let Some([p1, p2]) = pairs.next() {
                assert!(
                    p1.memory.private >= p2.memory.private,
                    "Processes are not presented in sorted order: {:?} < {:?}",
                    p1.memory,
                    p2.memory
                );
            }
        }
        for process in processed.processes {
            let expected = expected_processes_per_koid
                .get(&process.koid)
                .expect(&format!("Digest contains unexpected process {:?}", process));
            pretty_assertions::assert_eq!(process, *expected);
        }
    }

    // Reproduce a case similar to how blobfs shares the VMOs containing the file content.
    // `blobfs.cm` shares an unmodified child VMO with `app.cmx`.
    // The children VMO has 0 committed pages.
    // The test verifies that the shared memory charged to `app.cmx` is 0 despite the fact
    // that it owns a VMO that has a parent with committed memory.
    #[test]
    fn code_pages_received_from_blobfs_test() {
        let capture = raw::Capture {
            time: 1234567,
            kernel: raw::Kernel::default(),
            processes: vec![
                raw::Process::Headers(raw::ProcessHeaders::default()),
                raw::Process::Data(raw::ProcessData {
                    koid: 2,
                    name: "blobfs.cm".to_string(),
                    vmos: vec![1],
                }),
                raw::Process::Data(raw::ProcessData {
                    koid: 3,
                    name: "app.cmx".to_string(),
                    vmos: vec![2],
                }),
            ],
            vmo_names: vec!["blob-xxx".to_string(), "app.cmx".to_string()],
            vmos: vec![
                raw::Vmo::Headers(raw::VmoHeaders::default()),
                raw::Vmo::Data(raw::VmoData {
                    koid: 1,
                    name: 0,
                    parent_koid: 0,
                    committed_bytes: 500,
                    allocated_bytes: 1000,
                }),
                raw::Vmo::Data(raw::VmoData {
                    koid: 2,
                    name: 1,
                    parent_koid: 1,
                    committed_bytes: 0,
                    allocated_bytes: 1000,
                }),
            ],
        };
        let expected_processes_per_koid = HashMap::from([
            (
                2,
                processed::Process {
                    koid: 2,
                    name: "blobfs.cm".to_string(),
                    memory: processed::RetainedMemory { private: 0, scaled: 250, total: 500 },
                    name_to_memory: {
                        let mut result = HashMap::new();
                        result.insert(
                            "blob-xxx".to_string(),
                            processed::RetainedMemory { private: 0, scaled: 250, total: 500 },
                        );
                        result
                    },
                    vmos: {
                        let mut result = HashSet::new();
                        result.insert(1);
                        result
                    },
                },
            ),
            (
                3,
                processed::Process {
                    koid: 3,
                    name: "app.cmx".to_string(),
                    memory: processed::RetainedMemory { private: 0, scaled: 250, total: 500 },
                    name_to_memory: {
                        let mut result = HashMap::new();
                        result.insert(
                            "app.cmx".to_string(),
                            processed::RetainedMemory { private: 0, scaled: 0, total: 0 },
                        );
                        result.insert(
                            "blob-xxx".to_string(),
                            processed::RetainedMemory { private: 0, scaled: 250, total: 500 },
                        );
                        result
                    },
                    vmos: {
                        let mut result = HashSet::new();
                        result.insert(2);
                        result
                    },
                },
            ),
        ]);
        let processed = processed::digest_from_memory_monitor_output(
            raw::MemoryMonitorOutput { capture, buckets_definitions: vec![] },
            false,
        );
        // processed::Digest::from(raw);
        // Check that the process list is sorted
        {
            let mut pairs = processed.processes.windows(2);
            while let Some([p1, p2]) = pairs.next() {
                assert!(
                    p1.memory.private >= p2.memory.private,
                    "Processes are not presented in sorted order: {:?} < {:?}",
                    p1.memory,
                    p2.memory
                );
            }
        }
        for process in processed.processes {
            let expected = expected_processes_per_koid
                .get(&process.koid)
                .expect(&format!("Digest contains unexpected process {:?}", process));
            pretty_assertions::assert_eq!(process, *expected);
        }
    }
}
