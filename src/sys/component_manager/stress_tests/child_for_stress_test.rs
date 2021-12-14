// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    cm_stress_tests_lib::{create_child, stop_child, Child},
    fidl::endpoints::RequestStream,
    fidl_test_componentmanager_stresstests as fstresstests, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::prelude::*,
    std::sync::{Arc, Mutex},
};

#[fuchsia::component(logging_tags = ["child_for_stress_test"])]
async fn main() -> Result<(), Error> {
    const URL: &str =
        "fuchsia-pkg://fuchsia.com/component-manager-stress-tests#meta/child-for-stress-test.cm";
    const COL: &str = "children";
    tracing::debug!("started");
    let mut fs = ServiceFs::new_local();
    let children_vec = Arc::new(Mutex::new(vec![]));
    fs.dir("svc").add_fidl_service(move |mut stream: fstresstests::ChildRealmRequestStream| {
        let children_vec = children_vec.clone();
        fasync::Task::local(async move {
            stream.control_handle().send_on_connected().unwrap();
            while let Some(event) = stream.try_next().await.expect("Cannot read request stream") {
                match event {
                    fstresstests::ChildRealmRequest::Stop { .. } => {
                        std::process::exit(0);
                    }
                    fstresstests::ChildRealmRequest::CreateChildren {
                        direct_children,
                        tree_height,
                        responder,
                    } => {
                        let stream = stream::iter(0..direct_children);
                        let mut children: Vec<Child> = stream
                            .then(|_| async { create_child(COL, URL).await.unwrap() })
                            .collect()
                            .await;
                        stream::iter(&children)
                            .for_each_concurrent(None, |child| async move {
                                if tree_height > 1 {
                                    child
                                        .realm
                                        .create_children(direct_children, tree_height - 1)
                                        .await
                                        .unwrap();
                                }
                                match child
                                    .realm
                                    .take_event_stream()
                                    .try_next()
                                    .await
                                    .unwrap()
                                    .unwrap()
                                {
                                    fstresstests::ChildRealmEvent::OnConnected {} => {}
                                }
                            })
                            .await;
                        children_vec.lock().unwrap().append(&mut children);
                        responder.send().unwrap();
                    }
                    fstresstests::ChildRealmRequest::StopChildren { responder } => {
                        // TODO: this variable triggered the `must_not_suspend` lint and may be held across an await
                        // If this is the case, it is an error. See fxbug.dev/87757 for more details
                        let mut children_vec = children_vec.lock().unwrap();
                        let mut children = vec![];
                        children.append(&mut children_vec);

                        stream::iter(children)
                            .for_each_concurrent(None, |child| async {
                                child
                                    .realm
                                    .stop_children()
                                    .await
                                    .expect("Error calling stop_children");
                                stop_child(child).await.unwrap();
                            })
                            .await;
                        responder.send().unwrap();
                    }
                }
            }
        })
    });
    fs.take_and_serve_directory_handle()?;
    fs.for_each_concurrent(None, |t| async {
        t.await;
    })
    .await;
    Ok(())
}
