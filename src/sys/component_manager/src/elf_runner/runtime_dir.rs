// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{DirectoryMarker, NodeMarker, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    std::sync::Arc,
    vfs::{
        directory::entry::DirectoryEntry, directory::immutable::simple as pfs,
        execution_scope::ExecutionScope, file::vmo::asynchronous::read_only_static,
        path::Path as fvfsPath, tree_builder::TreeBuilder,
    },
};

// Simple directory type which is used to implement `ComponentStartInfo.runtime_directory`.
pub type RuntimeDirectory = Arc<pfs::Simple>;

pub struct RuntimeDirBuilder {
    args: Vec<String>,
    job_id: Option<u64>,
    process_id: Option<u64>,
    process_start_time: Option<i64>,
    process_start_time_utc_estimate: Option<String>,
    server_end: ServerEnd<NodeMarker>,
}

impl RuntimeDirBuilder {
    pub fn new(server_end: ServerEnd<DirectoryMarker>) -> Self {
        // Transform the server end to speak Node protocol only
        let server_end = ServerEnd::<NodeMarker>::new(server_end.into_channel());
        Self {
            args: vec![],
            job_id: None,
            process_id: None,
            process_start_time: None,
            process_start_time_utc_estimate: None,
            server_end,
        }
    }

    pub fn args(mut self, args: Vec<String>) -> Self {
        self.args = args;
        self
    }

    pub fn job_id(mut self, job_id: u64) -> Self {
        self.job_id = Some(job_id);
        self
    }

    pub fn process_id(mut self, process_id: u64) -> Self {
        self.process_id = Some(process_id);
        self
    }

    pub fn process_start_time(mut self, process_start_time: i64) -> Self {
        self.process_start_time = Some(process_start_time);
        self
    }

    pub fn process_start_time_utc_estimate(
        mut self,
        process_start_time_utc_estimate: Option<String>,
    ) -> Self {
        self.process_start_time_utc_estimate = process_start_time_utc_estimate;
        self
    }

    pub fn serve(mut self) -> RuntimeDirectory {
        // Create the runtime tree structure
        //
        // runtime
        // |- args
        // |  |- 0
        // |  |- 1
        // |  \- ...
        // \- elf
        //    |- job_id
        //    \- process_id
        let mut runtime_tree_builder = TreeBuilder::empty_dir();
        let mut count: u32 = 0;
        for arg in self.args.drain(..) {
            runtime_tree_builder
                .add_entry(["args", &count.to_string()], read_only_static(arg))
                .expect("Failed to add arg to runtime directory");
            count += 1;
        }

        if let Some(job_id) = self.job_id {
            runtime_tree_builder
                .add_entry(["elf", "job_id"], read_only_static(job_id.to_string()))
                .expect("Failed to add job_id to runtime/elf directory");
        }

        if let Some(process_id) = self.process_id {
            runtime_tree_builder
                .add_entry(["elf", "process_id"], read_only_static(process_id.to_string()))
                .expect("Failed to add process_id to runtime/elf directory");
        }

        if let Some(process_start_time) = self.process_start_time {
            runtime_tree_builder
                .add_entry(
                    ["elf", "process_start_time"],
                    read_only_static(process_start_time.to_string()),
                )
                .expect("Failed to add process_start_time to runtime/elf directory");
        }

        if let Some(process_start_time_utc_estimate) = self.process_start_time_utc_estimate {
            runtime_tree_builder
                .add_entry(
                    ["elf", "process_start_time_utc_estimate"],
                    read_only_static(process_start_time_utc_estimate),
                )
                .expect("Failed to add process_start_time_utc_estimate to runtime/elf directory");
        }

        let runtime_directory = runtime_tree_builder.build();

        // Serve the runtime directory
        runtime_directory.clone().open(
            ExecutionScope::new(),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            0,
            fvfsPath::empty(),
            self.server_end,
        );

        runtime_directory
    }
}
