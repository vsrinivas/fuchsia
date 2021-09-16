// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error::PowerManagerError;
use crate::message::{Message, MessageResult, MessageReturn};
use crate::node::Node;
use async_trait::async_trait;
use fuchsia_zircon_sys as sys;

/// Node: SyscallHandler
///
/// Summary: Executes syscalls so that these calls can be easily mocked by tests for dependent
///          nodes.
///
/// Handles Messages:
///     - GetNumCpus
///     - GetSetCpuPerformanceInfo
///
/// FIDL dependencies: None

/// Struct for handling syscalls.
struct SyscallHandler {}

#[async_trait(?Send)]
impl Node for SyscallHandler {
    fn name(&self) -> String {
        "SyscallHandler".to_string()
    }

    async fn handle_message(&self, msg: &Message) -> MessageResult {
        match msg {
            &Message::GetNumCpus => {
                let num_cpus = unsafe { sys::zx_system_get_num_cpus() };
                Ok(MessageReturn::GetNumCpus(num_cpus))
            }
            &Message::GetSetCpuPerformanceInfo(_) => {
                // This is a placeholder until the corresponding syscall lands. Given the signature
                //   zx_status_t zx_system_get_set_performance_info(
                //     zx_handle_t resource,
                //     uint32_t topic,
                //     const void* new_info,
                //     void* prev_info,
                //     size_t info_count,
                //     size_t* output_count
                // );
                //
                // `resource` will be lazily initialized
                // `topic` is a fixed value
                // `info_count` is `new_info.len()
                // `prev_info` will be allocated here and returned to the caller
                // `output_count` must be equal to `new_info.len()`...we'll check that here and
                //  return an error if not.
                //
                // NOTE(fxbug.dev/84730) The syscall provides a get/set operation, so we have the
                // information necessary to revert changes to the kernel if a P-state update fails.
                // But if a P-state update fails, we won't necessarily know the current frequency of
                // the CPU, and the caller can make a guess as to what it is using more information
                // than just what the previous kernel performance scales were. Hence, we ignore the
                // syscall's return value.
                Ok(MessageReturn::GetSetCpuPerformanceInfo)
            }
            _ => Err(PowerManagerError::Unsupported),
        }
    }
}
