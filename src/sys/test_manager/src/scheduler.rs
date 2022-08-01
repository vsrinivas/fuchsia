// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{self_diagnostics, test_suite};
use async_trait::async_trait;
use fidl_fuchsia_test_internal as ftest_internal;
use futures::channel::oneshot;

#[async_trait]
pub(crate) trait Scheduler {
    // This function schedules and executes the provided collection
    // of test suites. This allows objects that implement the
    // Scheduler trait to define their own test suite scheduling
    // algorithm. Inputs:
    //     - &self
    //     - suites: a collection of suites to schedule and execute
    //     - inspect_node_ref: a reference to an Inspect node, which contains diagnostics
    //                         for a single test run.
    //     - stop_recv: Receiving end of a channel that receives messages to attempt to stop the
    //                  test run. Scheduler::execute should check for stop messages over
    //                  this channel and try to terminate the test run gracefully.
    //     - run_id: an id that identifies the test run.
    //     -debug_controller: used to control debug data component and get it to process debug
    //                        data vmos
    async fn execute(
        &self,
        suites: Vec<test_suite::Suite>,
        inspect_node_ref: &self_diagnostics::RunInspectNode,
        stop_recv: &mut oneshot::Receiver<()>,
        run_id: u32,
        debug_controller: &ftest_internal::DebugDataSetControllerProxy,
    );
}

pub struct SerialScheduler {}

#[async_trait]
impl Scheduler for SerialScheduler {
    async fn execute(
        &self,
        suites: Vec<test_suite::Suite>,
        inspect_node_ref: &self_diagnostics::RunInspectNode,
        stop_recv: &mut oneshot::Receiver<()>,
        run_id: u32,
        debug_controller: &ftest_internal::DebugDataSetControllerProxy,
    ) {
        // run test suites serially for now
        for (suite_idx, suite) in suites.into_iter().enumerate() {
            // only check before running the test. We should complete the test run for
            // running tests, if stop is called.
            if let Ok(Some(())) = stop_recv.try_recv() {
                break;
            }
            let instance_name = format!("{:?}-{:?}", run_id, suite_idx);
            let suite_inspect = inspect_node_ref.new_suite(&instance_name, &suite.test_url);
            test_suite::run_single_suite(suite, debug_controller, &instance_name, suite_inspect)
                .await;
        }
    }
}
