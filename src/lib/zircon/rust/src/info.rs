// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon object information.

use fuchsia_zircon_sys as sys;
use std::ops::Deref;

#[derive(Copy, Clone, Ord, PartialOrd, Eq, PartialEq, Hash)]
#[repr(transparent)]
pub struct Topic(sys::zx_object_info_topic_t);

impl Deref for Topic {
    type Target = sys::zx_object_info_topic_t;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

#[allow(clippy::missing_safety_doc)] // TODO(fxbug.dev/99066)
/// A query to get info about a zircon object.
///
/// Safety: `TOPIC` must correspond to a valid `zx_object_get_info` topic,
/// and `InfoTy` must be a type that can be safely replaced with the byte
/// representation of the associated `zx_object_get_info` buffer type.
pub unsafe trait ObjectQuery {
    /// A `Topic` identifying this query.
    const TOPIC: Topic;
    /// The datatype returned by this query.
    type InfoTy;
}

assoc_values!(Topic, [
    NONE = sys::ZX_INFO_NONE;
    HANDLE_VALID = sys::ZX_INFO_HANDLE_VALID;
    HANDLE_BASIC = sys::ZX_INFO_HANDLE_BASIC;
    PROCESS = sys::ZX_INFO_PROCESS;
    PROCESS_THREADS = sys::ZX_INFO_PROCESS_THREADS;
    VMAR = sys::ZX_INFO_VMAR;
    JOB_CHILDREN = sys::ZX_INFO_JOB_CHILDREN;
    JOB_PROCESSES = sys::ZX_INFO_JOB_PROCESSES;
    THREAD = sys::ZX_INFO_THREAD;
    THREAD_EXCEPTION_REPORT = sys::ZX_INFO_THREAD_EXCEPTION_REPORT;
    TASK_STATS = sys::ZX_INFO_TASK_STATS;
    TASK_RUNTIME = sys::ZX_INFO_TASK_RUNTIME;
    PROCESS_MAPS = sys::ZX_INFO_PROCESS_MAPS;
    PROCESS_VMOS = sys::ZX_INFO_PROCESS_VMOS;
    THREAD_STATS = sys::ZX_INFO_THREAD_STATS;
    CPU_STATS = sys::ZX_INFO_CPU_STATS;
    KMEM_STATS = sys::ZX_INFO_KMEM_STATS;
    KMEM_STATS_EXTENDED = sys::ZX_INFO_KMEM_STATS_EXTENDED;
    RESOURCE = sys::ZX_INFO_RESOURCE;
    HANDLE_COUNT = sys::ZX_INFO_HANDLE_COUNT;
    BTI = sys::ZX_INFO_BTI;
    PROCESS_HANDLE_STATS = sys::ZX_INFO_PROCESS_HANDLE_STATS;
    SOCKET = sys::ZX_INFO_SOCKET;
    VMO = sys::ZX_INFO_VMO;
    JOB = sys::ZX_INFO_JOB;
]);
