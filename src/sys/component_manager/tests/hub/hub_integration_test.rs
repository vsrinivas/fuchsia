// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fidl_examples_routing_echo as fecho,
    fidl_fuchsia_io::{OPEN_FLAG_DIRECTORY, OPEN_FLAG_POSIX},
    fidl_fuchsia_test_hub as fhub, fuchsia_async as fasync,
    futures::{channel::mpsc, StreamExt},
    hub_report_capability::*,
    io_util::*,
    std::{path::PathBuf, sync::Arc},
    test_utils_lib::{events::*, injectors::*, matcher::EventMatcher, opaque_test::*},
};

pub struct TestRunner {
    pub test: OpaqueTest,
    external_hub_v2_path: PathBuf,
    hub_report_capability: Arc<HubReportCapability>,
    channel_close_rx: mpsc::Receiver<()>,
}

impl TestRunner {
    async fn start(
        root_component_url: &str,
        num_eager_static_components: i32,
        event_names: Vec<&str>,
    ) -> (TestRunner, EventStream) {
        assert!(
            num_eager_static_components >= 1,
            "There must always be at least one eager static component (the root)"
        );

        let test = OpaqueTest::default(root_component_url).await.unwrap();

        let (hub_report_capability, channel_close_rx) = HubReportCapability::new();

        let event_stream = {
            // Subscribe to temporary event streams for Started and CapabilityRouted.
            // These events are subscribed to separately because the events do not happen
            // in predictable orders. There is a possibility for the CapabilityRouted event
            // to be interleaved between the Started events.
            let event_source = test.connect_to_event_source().await.unwrap();
            let mut start_event_stream = event_source.subscribe(vec![Started::NAME]).await.unwrap();

            // Subscribe to events which are required by this test runner.
            // TODO(xbhatnag): There may be problems here if event_names contains
            // Started or CapabilityRouted
            let event_stream = event_source.subscribe(event_names).await.unwrap();

            // Inject HubReportCapability wherever it's requested.
            hub_report_capability.inject(&event_source, EventMatcher::ok()).await;

            // Unblock component manager
            event_source.start_component_tree().await;

            // Wait for the root component to start up
            EventMatcher::ok()
                .moniker(".")
                .expect_match::<Started>(&mut start_event_stream)
                .await
                .resume()
                .await
                .unwrap();

            // Wait for all child components to start up
            for _ in 1..=(num_eager_static_components - 1) {
                EventMatcher::ok()
                    .expect_match::<Started>(&mut start_event_stream)
                    .await
                    .resume()
                    .await
                    .unwrap();
            }

            // Return the event_stream to be used later
            event_stream
        };

        let external_hub_v2_path = test.get_component_manager_path().join("out/hub");

        let runner = Self { test, external_hub_v2_path, hub_report_capability, channel_close_rx };

        (runner, event_stream)
    }

    pub async fn connect_to_echo_service(&self, echo_service_path: String) -> Result<(), Error> {
        let full_path = self.external_hub_v2_path.join(echo_service_path);
        let full_path = full_path.to_str().expect("invalid chars");
        let node_proxy = open_node_in_namespace(full_path, OPEN_RIGHT_READABLE)?;
        let echo_proxy = fecho::EchoProxy::new(
            node_proxy.into_channel().expect("could not get channel from proxy"),
        );
        let res = echo_proxy.echo_string(Some("hippos")).await?;
        assert_eq!(res, Some("hippos".to_string()));
        Ok(())
    }

    pub async fn verify_directory_listing_externally(
        &self,
        relative_path: &str,
        expected_listing: Vec<&str>,
    ) {
        let full_path = self.external_hub_v2_path.join(relative_path);
        let full_path = full_path.to_str().expect("invalid chars");
        let dir_proxy = open_directory_in_namespace(
            full_path,
            OPEN_FLAG_DIRECTORY | OPEN_RIGHT_READABLE | OPEN_FLAG_POSIX,
        )
        .expect("Could not open directory externally");

        let actual_listing = list_directory(&dir_proxy).await.expect(&format!(
            "failed to verify that {} contains {:?}",
            relative_path, expected_listing
        ));
        assert_eq!(expected_listing, actual_listing);
    }

    pub async fn verify_directory_listing_locally(
        &self,
        path: &str,
        expected_listing: Vec<&str>,
    ) -> fhub::HubReportListDirectoryResponder {
        let event = self.hub_report_capability.observe(path).await;
        let expected_listing: Vec<String> =
            expected_listing.iter().map(|s| s.to_string()).collect();
        match event {
            HubReportEvent::DirectoryListing { listing, responder } => {
                assert_eq!(expected_listing, listing);
                responder
            }
            _ => {
                panic!("Unexpected event type!");
            }
        }
    }

    pub async fn verify_directory_listing(
        &self,
        hub_relative_path: &str,
        expected_listing: Vec<&str>,
    ) {
        let local_path = format!("/hub/{}", hub_relative_path);
        let responder = self
            .verify_directory_listing_locally(local_path.as_str(), expected_listing.clone())
            .await;
        self.verify_directory_listing_externally(hub_relative_path, expected_listing).await;
        responder.send().expect("Unable to respond");
    }

    pub async fn verify_file_content_locally(
        &self,
        path: &str,
        expected_content: &str,
    ) -> fhub::HubReportReportFileContentResponder {
        let event = self.hub_report_capability.observe(path).await;
        match event {
            HubReportEvent::FileContent { content, responder } => {
                let expected_content = expected_content.to_string();
                assert_eq!(expected_content, content);
                responder
            }
            _ => {
                panic!("Unexpected event type!");
            }
        }
    }

    pub async fn verify_file_content_externally(
        &self,
        relative_path: &str,
        expected_content: &str,
    ) {
        let full_path = self.external_hub_v2_path.join(relative_path);
        let full_path = full_path.to_str().expect("invalid chars");
        let file_proxy =
            open_file_in_namespace(full_path, OPEN_RIGHT_READABLE).expect("Failed to open file.");
        let actual_content = read_file(&file_proxy).await.expect("unable to read file");
        assert_eq!(expected_content, actual_content);
    }

    pub async fn wait_for_component_stop(mut self) {
        self.channel_close_rx.next().await.expect("component stop channel has been closed");
    }
}

#[fasync::run_singlethreaded(test)]
async fn advanced_routing() {
    let echo_service_name = "fidl.examples.routing.echo.Echo";
    let hub_report_service_name = "fuchsia.test.hub.HubReport";

    let (test_runner, _) = TestRunner::start(
        "fuchsia-pkg://fuchsia.com/hub_integration_test#meta/advanced_routing_echo_realm.cm",
        3,
        vec![],
    )
    .await;

    // Verify that echo_realm has two children.
    test_runner
        .verify_directory_listing_externally("children", vec!["echo_server", "reporter"])
        .await;

    // Verify the args from reporter.cml
    test_runner
        .verify_file_content_externally("children/reporter/exec/runtime/args/0", "Hippos")
        .await;
    test_runner
        .verify_file_content_externally("children/reporter/exec/runtime/args/1", "rule!")
        .await;

    let expose_dir = "children/echo_server/exec/expose";

    // Verify that the Echo service is exposed by echo_server
    test_runner
        .verify_directory_listing_externally(expose_dir, vec![echo_service_name, "hub"])
        .await;

    let out_dir = "children/echo_server/exec/out";
    let out_svc_dir = "children/echo_server/exec/out/svc";

    // Verify that the Echo service is available in the out dir.
    test_runner.verify_directory_listing_externally(out_dir, vec!["svc"]).await;
    test_runner.verify_directory_listing_externally(out_svc_dir, vec![echo_service_name]).await;

    // Verify that reporter is given the HubReport and Echo services
    let in_dir = "children/reporter/exec/in";
    let svc_dir = format!("{}/{}", in_dir, "svc");
    test_runner
        .verify_directory_listing_externally(
            svc_dir.as_str(),
            vec![echo_service_name, hub_report_service_name],
        )
        .await;

    // Verify that the 'pkg' directory is available.
    let pkg_dir = format!("{}/{}", in_dir, "pkg");
    test_runner
        .verify_directory_listing_externally(pkg_dir.as_str(), vec!["bin", "lib", "meta", "test"])
        .await;

    // Verify that we can connect to the echo service from the in/svc directory.
    let in_echo_service_path = format!("{}/{}", svc_dir, echo_service_name);
    test_runner.connect_to_echo_service(in_echo_service_path).await.unwrap();

    // Verify that we can connect to the echo service from the expose/svc directory.
    let expose_echo_service_path = format!("{}/{}", expose_dir, echo_service_name);
    test_runner.connect_to_echo_service(expose_echo_service_path).await.unwrap();

    // Verify that the 'hub' directory is available. The 'hub' mapped to 'reporter''s
    // namespace is actually mapped to the 'exec' directory of 'reporter'.
    let scoped_hub_dir = format!("{}/{}", in_dir, "hub");
    test_runner
        .verify_directory_listing_externally(
            scoped_hub_dir.as_str(),
            vec!["expose", "in", "out", "resolved_url", "runtime"],
        )
        .await;

    test_runner
        .verify_directory_listing_locally(
            "/hub",
            vec!["expose", "in", "out", "resolved_url", "runtime"],
        )
        .await
        .send()
        .unwrap();

    // Verify that reporter's view is able to correctly read the names of the
    // children of the parent echo_realm.
    test_runner
        .verify_directory_listing_locally("/parent_hub/children", vec!["echo_server", "reporter"])
        .await
        .send()
        .unwrap();

    // Verify that reporter is able to see its sibling's hub correctly.
    test_runner
        .verify_file_content_locally(
            "/sibling_hub/exec/resolved_url",
            "fuchsia-pkg://fuchsia.com/hub_integration_test#meta/echo_server.cm",
        )
        .await
        .send()
        .unwrap();

    test_runner.wait_for_component_stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn dynamic_child_test() {
    let (test_runner, mut event_stream) = TestRunner::start(
        "fuchsia-pkg://fuchsia.com/hub_integration_test#meta/dynamic_child_reporter.cm",
        1,
        vec![MarkedForDestruction::NAME, Stopped::NAME, Destroyed::NAME],
    )
    .await;

    // Verify that the dynamic child exists in the parent's hub
    test_runner.verify_directory_listing("children", vec!["coll:simple_instance"]).await;

    // Before binding, verify that the dynamic child's hub has the directories we expect
    // i.e. "children" and "url" but no "exec" because the child has not been bound.
    test_runner
        .verify_directory_listing(
            "children/coll:simple_instance",
            vec!["children", "component_type", "deleting", "id", "url"],
        )
        .await;

    // Verify that the dynamic child has the correct instance id.
    test_runner
        .verify_file_content_locally("/hub/children/coll:simple_instance/id", "1")
        .await
        .send()
        .unwrap();

    // Before binding, verify that the dynamic child's static children are invisible
    test_runner.verify_directory_listing("children/coll:simple_instance/children", vec![]).await;

    // After binding, verify that the dynamic child's hub has the directories we expect
    test_runner
        .verify_directory_listing(
            "children/coll:simple_instance",
            vec!["children", "component_type", "deleting", "exec", "id", "url"],
        )
        .await;

    // After binding, verify that the dynamic child's static child is visible
    test_runner
        .verify_directory_listing("children/coll:simple_instance/children", vec!["child"])
        .await;

    // Verify that the dynamic child's static child has the correct instance id.
    test_runner
        .verify_file_content_locally("/hub/children/coll:simple_instance/children/child/id", "0")
        .await
        .send()
        .unwrap();

    // Wait for the dynamic child to begin deletion
    let event = EventMatcher::ok()
        .moniker("./coll:simple_instance:1")
        .expect_match::<MarkedForDestruction>(&mut event_stream)
        .await;

    // When deletion begins, the dynamic child should be moved to the deleting directory
    test_runner.verify_directory_listing("children", vec![]).await;
    test_runner.verify_directory_listing("deleting", vec!["coll:simple_instance:1"]).await;
    test_runner
        .verify_directory_listing(
            "deleting/coll:simple_instance:1",
            vec!["children", "component_type", "deleting", "exec", "id", "url"],
        )
        .await;

    // Unblock the ComponentManager
    event.resume().await.unwrap();

    // Wait for the dynamic child to stop
    let event = EventMatcher::ok()
        .moniker("./coll:simple_instance:1")
        .expect_match::<Stopped>(&mut event_stream)
        .await;

    // After stopping, the dynamic child should not have an exec directory
    test_runner
        .verify_directory_listing(
            "deleting/coll:simple_instance:1",
            vec!["children", "component_type", "deleting", "id", "url"],
        )
        .await;

    // Unblock the Component Manager
    event.resume().await.unwrap();

    // Wait for the dynamic child's static child to begin deletion
    let event = EventMatcher::ok()
        .moniker("./coll:simple_instance:1/child:0")
        .expect_match::<MarkedForDestruction>(&mut event_stream)
        .await;

    // When deletion begins, the dynamic child's static child should be moved to the deleting directory
    test_runner.verify_directory_listing("deleting/coll:simple_instance:1/children", vec![]).await;
    test_runner
        .verify_directory_listing("deleting/coll:simple_instance:1/deleting", vec!["child:0"])
        .await;
    test_runner
        .verify_directory_listing(
            "deleting/coll:simple_instance:1/deleting/child:0",
            vec!["children", "component_type", "deleting", "id", "url"],
        )
        .await;

    // Unblock the Component Manager
    event.resume().await.unwrap();

    // Wait for the dynamic child's static child to be destroyed
    let event = EventMatcher::ok()
        .moniker("./coll:simple_instance:1/child:0")
        .expect_match::<Destroyed>(&mut event_stream)
        .await;

    // The dynamic child's static child should not be visible in the hub anymore
    test_runner.verify_directory_listing("deleting/coll:simple_instance:1/deleting", vec![]).await;

    // Unblock the Component Manager
    event.resume().await.unwrap();

    // Wait for the dynamic child to be destroyed
    let event = EventMatcher::ok()
        .moniker("./coll:simple_instance:1")
        .expect_match::<Destroyed>(&mut event_stream)
        .await;

    // After deletion, verify that parent can no longer see the dynamic child in the Hub
    test_runner.verify_directory_listing("deleting", vec![]).await;

    // Unblock the Component Manager
    event.resume().await.unwrap();

    // Wait for the component to stop
    test_runner.wait_for_component_stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn visibility_test() {
    let (test_runner, _) = TestRunner::start(
        "fuchsia-pkg://fuchsia.com/hub_integration_test#meta/visibility_reporter.cm",
        1,
        vec![],
    )
    .await;

    // Verify that the child exists in the parent's hub
    test_runner.verify_directory_listing("children", vec!["child"]).await;

    // Verify that the child's hub has the directories we expect
    // i.e. no "exec" because the child has not been bound.
    test_runner
        .verify_directory_listing(
            "children/child",
            vec!["children", "component_type", "deleting", "id", "url"],
        )
        .await;

    // Verify that the grandchild is not shown because the child is lazy
    test_runner.verify_directory_listing("children/child/children", vec![]).await;

    // Wait for the component to stop
    test_runner.wait_for_component_stop().await;
}
