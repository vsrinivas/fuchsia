// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{format_err, Context, Error},
    fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::client::{connect_to_childs_service, ScopedInstance},
    fuchsia_syslog as syslog,
    futures::future::join_all,
    log::*,
};

fn main() {
    let mut exec = fasync::Executor::new().expect("failed to make async executor");
    syslog::init_with_tags(&["components_v2_realm"]).expect("could not initialize logging");
    info!("Realm started");

    // Create 3 scoped instances
    let mut instances =
        exec.run_singlethreaded(create_instances()).expect("failed to create instances");
    info!("Created instances");

    // Grab the destroy waiters for each scoped instance, each of which will resolve once
    // destruction for its instance is complete
    let mut waiters: Vec<_> = instances.iter_mut().map(|i| i.take_destroy_waiter()).collect();

    // None of the waiters should be ready yet, since we haven't dropped any of the instances
    for waiter in waiters.iter_mut() {
        exec.wake_main_future();
        match exec.run_one_step(waiter) {
            None => panic!("waiter future has not been dispatched"),
            Some(core::task::Poll::Ready(_)) => panic!("waiter future should not be ready yet"),
            Some(core::task::Poll::Pending) => (),
        }
    }

    // Drop the ScopedInstances, which will cause the child components to be destroyed
    info!("Dropping scoped instances");
    drop(instances);
    // Wait for all of the instances to be destroyed, assert that there were no errors
    for destruction_error in exec.run_singlethreaded(join_all(waiters)) {
        assert!(destruction_error.is_none());
    }
}

async fn create_instances() -> Result<Vec<ScopedInstance>, Error> {
    let url = "fuchsia-pkg://fuchsia.com/fuchsia-component-tests#meta/echo_server.cm".to_string();
    // Create 4 scoped instances, and confirm that each is funcitoning correctly by using a FIDL
    // service from it
    let instances = vec![
        ScopedInstance::new("coll".to_string(), url.clone())
            .await
            .context("instance creation failed")?,
        ScopedInstance::new("coll".to_string(), url.clone())
            .await
            .context("instance creation failed")?,
        ScopedInstance::new("coll".to_string(), url.clone())
            .await
            .context("instance creation failed")?,
        ScopedInstance::new_with_name("static_name".to_string(), "coll".to_string(), url.clone())
            .await
            .context("instance creation failed")?,
    ];
    for scoped_instance in instances.iter() {
        {
            let echo_proxy = scoped_instance
                .connect_to_protocol_at_exposed_dir::<fecho::EchoMarker>()
                .context("failed to connect to echo in exposed dir")?;
            let out = echo_proxy
                .echo_string(Some("hippos"))
                .await
                .context("echo_string failed")?
                .ok_or(format_err!("empty result"))?;
            assert_eq!(out, "hippos");
        }
        {
            let echo_proxy = connect_to_childs_service::<fecho::EchoMarker>(
                scoped_instance.child_name(),
                Some("coll".to_string()),
            )
            .await
            .context("failed to connect to child's echo service")?;
            let out = echo_proxy
                .echo_string(Some("hippos"))
                .await
                .context("echo_string failed")?
                .ok_or(format_err!("empty result"))?;
            assert_eq!(out, "hippos");
        }
    }
    Ok(instances)
}
