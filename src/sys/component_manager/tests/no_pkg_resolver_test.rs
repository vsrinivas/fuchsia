// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use {
    component_manager_lib::{
        model::{Resolver, ResolverError},
        startup,
    },
    fuchsia_async as fasync,
};

#[fasync::run_singlethreaded(test)]
async fn fuchsia_pkg_resolve_fails() {
    let resolver_registry = startup::available_resolvers().expect("Failed to get resolvers");
    let result = resolver_registry.resolve("fuchsia-pkg://fuchsia.com/foo#meta/bar.cmx").await;
    match result {
        Err(ResolverError::SchemeNotRegistered) => {}
        _ => {
            panic!("Test failed, unexpected result: {:?}", result);
        }
    }
}
