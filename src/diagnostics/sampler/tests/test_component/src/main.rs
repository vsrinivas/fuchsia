// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context;
use fidl_fuchsia_samplertestcontroller::*;
use fuchsia_async::{
    self as fasync,
    futures::{FutureExt, StreamExt, TryStreamExt},
};
use fuchsia_component::server::ServiceFs;
use fuchsia_inspect::component;
use std::{
    collections::HashMap,
    sync::{Arc, Mutex},
};

fn main() {
    let mut executor = fasync::Executor::new().context("Error creating executor").unwrap();

    let mut fs = ServiceFs::new();
    let inspector = component::inspector();
    inspector.serve(&mut fs).unwrap();

    fs.dir("svc").add_fidl_service(move |stream| serve_sampler_test_controller(stream));

    fs.take_and_serve_directory_handle().unwrap();

    executor.run_singlethreaded(fs.collect::<()>());
}

struct TestState {
    integer_property_map: HashMap<u16, (String, i64)>,
    sample_count_callback_opt: Option<SamplerTestControllerWaitForSampleResponder>,
}

impl TestState {
    pub fn new() -> Arc<Mutex<TestState>> {
        let mut integer_property_map: HashMap<u16, (String, i64)> = HashMap::new();
        integer_property_map.insert(1, ("counter".to_string(), 0));
        Arc::new(Mutex::new(TestState { integer_property_map, sample_count_callback_opt: None }))
    }
}

fn add_lazy_sampled_node(
    parent: &fuchsia_inspect::Node,
    state: Arc<Mutex<TestState>>,
) -> fuchsia_inspect::LazyNode {
    parent.create_lazy_child("samples", move || {
        let state = state.clone();
        async move {
            let mut unwrapped = state.lock().unwrap();

            if let Some(responder) = unwrapped.sample_count_callback_opt.take() {
                responder.send(&mut Ok(())).unwrap();
            }

            let samples = fuchsia_inspect::Inspector::new();
            for (_, (name, int_val)) in unwrapped.integer_property_map.clone() {
                let () = samples.root().record_int(name, int_val);
            }
            Ok(samples)
        }
        .boxed()
    })
}

fn serve_sampler_test_controller(mut stream: SamplerTestControllerRequestStream) {
    fasync::Task::spawn(async move {
        let state = TestState::new();
        let inspector = component::inspector();
        let _lazy_state_node = add_lazy_sampled_node(inspector.root(), state.clone());
        while let Some(req) = stream.try_next().await.unwrap() {
            match req {
                SamplerTestControllerRequest::IncrementInt { property_id, control_handle: _ } => {
                    let mut unwrapped = state.lock().unwrap();
                    unwrapped.integer_property_map.get_mut(&property_id).unwrap().1 += 1
                }
                SamplerTestControllerRequest::WaitForSample { responder } => {
                    let mut unwrapped = state.lock().unwrap();

                    if unwrapped.sample_count_callback_opt.is_some() {
                        responder
                            .send(&mut Err(SamplingError::MultipleSampleCallbacksError))
                            .unwrap();
                        continue;
                    }

                    unwrapped.sample_count_callback_opt = Some(responder);
                }
            }
        }
    })
    .detach();
}
