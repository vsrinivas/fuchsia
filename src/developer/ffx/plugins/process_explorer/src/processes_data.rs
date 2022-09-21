// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Processes Explorer related functionality.

/// Types and utilities related to the raw data produced by
/// `process_explorer`.
pub mod raw {
    use fuchsia_zircon_types::{zx_koid_t, zx_obj_type_t};
    use serde::{Deserialize, Serialize};

    /// A simplified version of the zx_info_handle_extended_t
    /// type.
    ///
    /// Some notable points:
    ///   * The peer_owner_koid is currently unimplemented
    ///     (fxbug.dev/60170), so the value is calculated
    ///     by the process_explorer component.
    ///     See more details about its implementation at
    ///     src/developer/process_explorer/utils.h
    #[derive(Clone, Copy, Serialize, Deserialize, PartialEq, Debug)]
    pub struct KernelObject {
        /// The object type: channel, event, socket, etc.
        pub object_type: zx_obj_type_t,
        /// Kernel Object ID - the unique id assigned by kernel
        /// to the object.
        pub koid: zx_koid_t,
        /// If the object referenced by the handle is related
        /// to another (such as the other end of a channel, or
        /// the parent of a job) then |related_koid| is the koid
        /// of that object, otherwise it is zero. This relationship
        /// is immutable: an object's |related_koid| does not
        /// change even if the related object no longer exists.
        pub related_koid: zx_koid_t,
        /// If the object referenced by the handle has a peer,
        /// like the other end of a channel, then this is the koid
        /// of the process which currently owns it.
        pub peer_owner_koid: zx_koid_t,
    }

    /// A structure containing information about a process.
    #[derive(Serialize, Deserialize, PartialEq, Debug)]
    pub struct Process {
        /// Kernel Object ID - the unique id assigned by kernel
        /// to the process.
        pub koid: zx_koid_t,
        /// The name of the process.
        pub name: String,
        /// A vector containing all objects associated with the process.
        /// Slightly modified copy of the structure returned by the
        /// `zx_object_get_info` syscall when invoked with the
        /// `ZX_INFO_HANDLE_TABLE` topic. Refer to this syscall's
        /// documentation and to the KernelObject struct definition in
        /// this file for more details.
        pub objects: Vec<KernelObject>,
    }

    /// Processes Data exported by `ProcessExplorer`.
    /// This corresponds to the schema of the data that is transferred
    /// by `ProcessExplorerQuery::WriteJsonProcessesData`'s API.
    #[derive(Serialize, Deserialize, PartialEq, Debug, Default)]
    #[serde(rename_all = "PascalCase")]
    pub struct ProcessesData {
        /// Processes data
        pub processes: Vec<Process>,
    }
}

/// Types and utilities to produce and manipulate processed summaries
/// suitable for user-facing consumption.
pub mod processed {
    use crate::processes_data::raw;
    use fuchsia_zircon_types::{zx_koid_t, zx_obj_type_t};
    use serde::Serialize;
    use std::collections::HashMap;

    /// Similar to raw::KernelObject, except it does not contain the
    /// object type.
    #[derive(Serialize, Clone, Copy, Eq, Hash, PartialEq, Debug, Default)]
    pub struct KernelObject {
        pub koid: zx_koid_t,
        pub related_koid: zx_koid_t,
        pub peer_owner_koid: zx_koid_t,
    }

    /// A structure containing all objects of the same type
    /// associated with the same process.
    #[derive(Serialize, PartialEq, Debug, Default)]
    pub struct KernelObjectsByType {
        /// The type of the objects.
        pub objects_type: zx_obj_type_t,
        /// The number of objects of the specified type associated
        /// with a process.
        pub objects_count: usize,
        /// A vector containing all KernelObject type elements.
        pub objects: Vec<KernelObject>,
    }

    /// Similar to raw::KernelObject, except that objects are
    /// grouped by their type.
    #[derive(Serialize, PartialEq, Debug, Default)]
    pub struct Process {
        pub koid: zx_koid_t,
        pub name: String,
        /// The number of objects associated with the process.
        pub objects_count: usize,
        /// A vector of KernelObjectByType type elements.
        pub objects: Vec<KernelObjectsByType>,
    }

    /// Similar to raw::ProcessesData, except that it contains an
    /// additional field that stores the number of processes.
    #[derive(Serialize, PartialEq, Debug)]
    pub struct ProcessesData {
        /// Number of processes found.
        pub processes_count: usize,
        /// Processes data
        pub processes: Vec<Process>,
    }

    /// Conversion trait from raw processes information to a human-friendly
    /// processed information. Data is aggregated, normalized, sorted.
    impl From<raw::ProcessesData> for ProcessesData {
        fn from(raw: raw::ProcessesData) -> Self {
            let processes: Vec<Process> = {
                let mut processes = vec![];
                for raw_process in raw.processes {
                    let mut type_to_objects: HashMap<u32, Vec<KernelObject>> = HashMap::new();
                    for raw_object in &raw_process.objects {
                        let object = KernelObject {
                            koid: raw_object.koid,
                            related_koid: raw_object.related_koid,
                            peer_owner_koid: raw_object.peer_owner_koid,
                        };
                        type_to_objects.entry(raw_object.object_type).or_default().push(object);
                    }
                    let vector_objects_by_type: Vec<KernelObjectsByType> = {
                        let mut vector_objects_by_type: Vec<KernelObjectsByType> = Vec::new();
                        for i in 0..32 {
                            if type_to_objects.contains_key(&i) {
                                let objects = type_to_objects.get(&i).unwrap();
                                let kot = KernelObjectsByType {
                                    objects_type: i,
                                    objects_count: objects.len(),
                                    objects: objects.to_vec(),
                                };
                                vector_objects_by_type.push(kot);
                            }
                        }
                        vector_objects_by_type
                    };
                    let p = Process {
                        koid: raw_process.koid,
                        name: raw_process.name,
                        objects_count: raw_process.objects.len(),
                        objects: vector_objects_by_type,
                    };
                    processes.push(p);
                }
                processes
            };
            ProcessesData { processes_count: processes.len(), processes }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashMap;

    #[test]
    fn raw_to_processed_test() {
        let raw = raw::ProcessesData {
            processes: vec![
                raw::Process {
                    koid: 1,
                    name: "process1".to_string(),
                    objects: vec![
                        raw::KernelObject {
                            object_type: 4,
                            koid: 78,
                            related_koid: 79,
                            peer_owner_koid: 2,
                        },
                        raw::KernelObject {
                            object_type: 4,
                            koid: 52,
                            related_koid: 53,
                            peer_owner_koid: 0,
                        },
                        raw::KernelObject {
                            object_type: 17,
                            koid: 36,
                            related_koid: 0,
                            peer_owner_koid: 0,
                        },
                    ],
                },
                raw::Process {
                    koid: 2,
                    name: "process2".to_string(),
                    objects: vec![
                        raw::KernelObject {
                            object_type: 19,
                            koid: 28,
                            related_koid: 0,
                            peer_owner_koid: 0,
                        },
                        raw::KernelObject {
                            object_type: 14,
                            koid: 95,
                            related_koid: 96,
                            peer_owner_koid: 0,
                        },
                        raw::KernelObject {
                            object_type: 4,
                            koid: 79,
                            related_koid: 78,
                            peer_owner_koid: 1,
                        },
                    ],
                },
            ],
        };
        let expected_processes_per_koid = HashMap::from([
            (
                1,
                processed::Process {
                    koid: 1,
                    name: "process1".to_string(),
                    objects_count: 3,
                    objects: vec![
                        processed::KernelObjectsByType {
                            objects_type: 4,
                            objects_count: 2,
                            objects: vec![
                                processed::KernelObject {
                                    koid: 78,
                                    related_koid: 79,
                                    peer_owner_koid: 2,
                                },
                                processed::KernelObject {
                                    koid: 52,
                                    related_koid: 53,
                                    peer_owner_koid: 0,
                                },
                            ],
                        },
                        processed::KernelObjectsByType {
                            objects_type: 17,
                            objects_count: 1,
                            objects: vec![processed::KernelObject {
                                koid: 36,
                                related_koid: 0,
                                peer_owner_koid: 0,
                            }],
                        },
                    ],
                },
            ),
            (
                2,
                processed::Process {
                    koid: 2,
                    name: "process2".to_string(),
                    objects_count: 3,
                    objects: vec![
                        processed::KernelObjectsByType {
                            objects_type: 4,
                            objects_count: 1,
                            objects: vec![processed::KernelObject {
                                koid: 79,
                                related_koid: 78,
                                peer_owner_koid: 1,
                            }],
                        },
                        processed::KernelObjectsByType {
                            objects_type: 14,
                            objects_count: 1,
                            objects: vec![processed::KernelObject {
                                koid: 95,
                                related_koid: 96,
                                peer_owner_koid: 0,
                            }],
                        },
                        processed::KernelObjectsByType {
                            objects_type: 19,
                            objects_count: 1,
                            objects: vec![processed::KernelObject {
                                koid: 28,
                                related_koid: 0,
                                peer_owner_koid: 0,
                            }],
                        },
                    ],
                },
            ),
        ]);
        let processed = processed::ProcessesData::from(raw);
        for process in processed.processes {
            let expected = expected_processes_per_koid
                .get(&process.koid)
                .expect(&format!("Data contains unexpected process {:?}", process));
            pretty_assertions::assert_eq!(process, *expected);
        }
    }
}
