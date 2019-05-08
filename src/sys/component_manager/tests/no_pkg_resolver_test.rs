// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_manager_lib::{
        model::{Resolver, ResolverError},
        startup,
    },
    fuchsia_async as fasync,
};


fn main() {
    run_test();
}

fn run_test() {
    let mut executor = fasync::Executor::new().expect("Failed to create executor");

    let resolver_registry = startup::available_resolvers().expect("Failed to get resolvers");
    let result = executor.run_singlethreaded(resolver_registry.resolve("fuchsia-pkg://fuchsia.com/anything#meta/anything.cmx"));
    match result {
        Err(ResolverError::SchemeNotRegistered) => {},
        _ => {
            panic!("Test failed, unexpected result: {:?}", result);
        }
    }
}
