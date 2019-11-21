// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_manager_lib::{
        model::{
            self, hooks::*, testing::breakpoints::*, testing::breakpoints_capability::*,
            testing::test_helpers, AbsoluteMoniker, BuiltinEnvironment, ComponentManagerConfig,
            Model,
        },
        startup,
    },
    failure::{self, Error},
    fidl_fidl_examples_routing_echo as fecho,
    fidl_fuchsia_io::{
        DirectoryEvent, DirectoryProxy, MODE_TYPE_SERVICE, OPEN_FLAG_DESCRIBE, OPEN_RIGHT_READABLE,
    },
    fidl_fuchsia_test_hub as fhub,
    futures::TryStreamExt,
    hub_test_hook::*,
    std::{
        path::PathBuf,
        sync::{Arc, Weak},
        vec::Vec,
    },
};

struct TestRunner {
    pub model: Arc<Model>,
    pub builtin_environment: BuiltinEnvironment,
    hub_test_hook: Arc<HubTestHook>,
    _breakpoint_system: BreakpointSystem,
    _breakpoint_capability_hook: BreakpointCapabilityHook,
    breakpoint_receiver: BreakpointInvocationReceiver,
    hub_proxy: DirectoryProxy,
}

async fn install_hub_test_hook(model: &Model) -> Arc<HubTestHook> {
    let hub_test_hook = Arc::new(HubTestHook::new());
    model
        .root_realm
        .hooks
        .install(vec![HooksRegistration {
            events: vec![EventType::RouteFrameworkCapability],
            callback: Arc::downgrade(&hub_test_hook) as Weak<dyn Hook>,
        }])
        .await;
    hub_test_hook
}

async fn register_breakpoints(
    model: &Model,
    event_types: Vec<EventType>,
) -> (BreakpointSystem, BreakpointCapabilityHook, BreakpointInvocationReceiver) {
    let breakpoint_system = BreakpointSystem::new();
    let breakpoint_capability_hook = breakpoint_system.create_capability_hook();
    let breakpoint_receiver = breakpoint_system.register(event_types).await;
    model.root_realm.hooks.install(breakpoint_system.hooks()).await;
    model.root_realm.hooks.install(breakpoint_capability_hook.hooks()).await;
    (breakpoint_system, breakpoint_capability_hook, breakpoint_receiver)
}

impl TestRunner {
    async fn new(root_component_url: &str) -> Result<Self, Error> {
        TestRunner::new_with_breakpoints(root_component_url, vec![]).await
    }

    async fn new_with_breakpoints(
        root_component_url: &str,
        event_types: Vec<EventType>,
    ) -> Result<Self, Error> {
        let args = startup::Arguments {
            use_builtin_process_launcher: false,
            use_builtin_vmex: false,
            root_component_url: root_component_url.to_string(),
            debug: false,
        };
        let model = startup::model_setup(&args).await?;
        let hub_test_hook = install_hub_test_hook(&model).await;
        let builtin_environment =
            startup::builtin_environment_setup(&args, &model, ComponentManagerConfig::default())
                .await?;

        // Setup ServiceFs
        let hub_proxy = builtin_environment.bind_service_fs_for_hub(&model).await?;

        // Setup the breakpoints system for the test and the components in the tree.
        let (breakpoint_system, breakpoint_capability_hook, breakpoint_receiver) =
            register_breakpoints(&model, event_types).await;

        // Bind the model, starting the root component
        let root_moniker = model::AbsoluteMoniker::root();
        let res = model.bind(&root_moniker).await;
        let expected_res: Result<(), model::ModelError> = Ok(());
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));

        Ok(Self {
            model,
            builtin_environment,
            hub_proxy,
            hub_test_hook,
            _breakpoint_system: breakpoint_system,
            _breakpoint_capability_hook: breakpoint_capability_hook,
            breakpoint_receiver,
        })
    }

    async fn expect_invocation(
        &self,
        expected_event: EventType,
        components: Vec<&str>,
    ) -> BreakpointInvocation {
        let breakpoint = self.breakpoint_receiver.receive().await;
        let expected_moniker = AbsoluteMoniker::from(components);
        assert_eq!(breakpoint.event.type_(), expected_event);
        let moniker = breakpoint.event.target_realm().abs_moniker.clone();
        assert_eq!(moniker, expected_moniker);
        breakpoint
    }

    async fn connect_to_echo_service(&self, echo_service_path: String) -> Result<(), Error> {
        let node_proxy = io_util::open_node(
            &self.hub_proxy,
            &PathBuf::from(echo_service_path),
            io_util::OPEN_RIGHT_READABLE,
            MODE_TYPE_SERVICE,
        )?;
        let echo_proxy = fecho::EchoProxy::new(node_proxy.into_channel().unwrap());
        let res = echo_proxy.echo_string(Some("hippos")).await?;
        assert_eq!(res, Some("hippos".to_string()));
        Ok(())
    }

    async fn verify_global_directory_listing(
        &self,
        relative_path: &str,
        expected_listing: Vec<&str>,
    ) {
        let dir_proxy = io_util::open_directory(
            &self.hub_proxy,
            &PathBuf::from(relative_path),
            OPEN_RIGHT_READABLE | OPEN_FLAG_DESCRIBE,
        )
        .expect("Could not open directory from global view");

        // Expect an OnOpen event to be sent over the event stream.
        let mut event_stream = dir_proxy.take_event_stream();
        let event = event_stream.try_next().await.unwrap();
        let DirectoryEvent::OnOpen_ { s: _, info: _ } =
            event.expect("failed to receive OnOpen directory event");

        assert_eq!(expected_listing, test_helpers::list_directory(&dir_proxy).await);
    }

    async fn verify_local_directory_listing(
        &self,
        path: &str,
        expected_listing: Vec<&str>,
    ) -> fhub::HubReportListDirectoryResponder {
        let event = self.hub_test_hook.observe(path).await;
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

    async fn verify_directory_listing(&self, hub_relative_path: &str, expected_listing: Vec<&str>) {
        let local_path = format!("/hub/{}", hub_relative_path);
        let responder = self
            .verify_local_directory_listing(local_path.as_str(), expected_listing.clone())
            .await;
        self.verify_global_directory_listing(hub_relative_path, expected_listing).await;
        responder.send().expect("Unable to respond");
    }

    async fn verify_local_file_content(&self, path: &str, expected_content: &str) {
        let event = self.hub_test_hook.observe(path).await;
        match event {
            HubReportEvent::FileContent { content, responder } => {
                let expected_content = expected_content.to_string();
                assert_eq!(expected_content, content);
                responder.send().expect("failed to respond");
            }
            _ => {
                panic!("Unexpected event type!");
            }
        };
    }

    async fn verify_global_file_content(&self, relative_path: &str, expected_content: &str) {
        assert_eq!(expected_content, test_helpers::read_file(&self.hub_proxy, relative_path).await);
    }

    async fn wait_for_component_stop(&self) {
        self.hub_test_hook.wait_for_component_stop().await;
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn advanced_routing_test() -> Result<(), Error> {
    let root_component_url = "fuchsia-pkg://fuchsia.com/hub_integration_test#meta/echo_realm.cm";
    let test_runner =
        TestRunner::new_with_breakpoints(root_component_url, vec![EventType::UseCapability])
            .await?;

    // Wait for Breakpoints and HubReport capabilities to be used by reporter.
    test_runner.expect_invocation(EventType::UseCapability, vec!["reporter:0"]).await.resume();
    test_runner.expect_invocation(EventType::UseCapability, vec!["reporter:0"]).await.resume();

    // Verify that echo_realm has two children.
    test_runner.verify_global_directory_listing("children", vec!["echo_server", "reporter"]).await;

    // Verify reporter's instance id.
    test_runner.verify_global_file_content("children/reporter/id", "0").await;

    // Verify echo_server's instance id.
    test_runner.verify_global_file_content("children/echo_server/id", "0").await;

    // Verify the args from reporter.cml.
    test_runner.verify_global_file_content("children/reporter/exec/runtime/args/0", "Hippos").await;
    test_runner.verify_global_file_content("children/reporter/exec/runtime/args/1", "rule!").await;

    let echo_service_name = "fidl.examples.routing.echo.Echo";
    let hub_report_service_name = "fuchsia.test.hub.HubReport";
    let breakpoints_service_name = "fuchsia.test.breakpoints.Breakpoints";
    let expose_svc_dir = "children/echo_server/exec/expose/svc";

    // Verify that the Echo service is exposed by echo_server
    test_runner.verify_global_directory_listing(expose_svc_dir, vec![echo_service_name]).await;

    // Verify that reporter is using Breakpoint, HubReport and Echo services
    let in_dir = "children/reporter/exec/in";
    let svc_dir = format!("{}/{}", in_dir, "svc");
    test_runner
        .verify_global_directory_listing(
            svc_dir.as_str(),
            vec![echo_service_name, breakpoints_service_name, hub_report_service_name],
        )
        .await;

    // Verify that the 'pkg' directory is available.
    let pkg_dir = format!("{}/{}", in_dir, "pkg");
    test_runner
        .verify_global_directory_listing(pkg_dir.as_str(), vec!["bin", "lib", "meta", "test"])
        .await;

    // Verify that we can connect to the echo service from the in/svc directory.
    let in_echo_service_path = format!("{}/{}", svc_dir, echo_service_name);
    test_runner.connect_to_echo_service(in_echo_service_path).await?;

    // Verify that we can connect to the echo service from the expose/svc directory.
    let expose_echo_service_path = format!("{}/{}", expose_svc_dir, echo_service_name);
    test_runner.connect_to_echo_service(expose_echo_service_path).await?;

    // Verify that the 'hub' directory is available. The 'hub' mapped to 'reporter''s
    // namespace is actually mapped to the 'exec' directory of 'reporter'.
    let scoped_hub_dir = format!("{}/{}", in_dir, "hub");
    test_runner
        .verify_global_directory_listing(
            scoped_hub_dir.as_str(),
            vec!["expose", "in", "out", "resolved_url", "runtime", "used"],
        )
        .await;
    let responder = test_runner
        .verify_local_directory_listing(
            "/hub",
            vec!["expose", "in", "out", "resolved_url", "runtime", "used"],
        )
        .await;
    responder.send().expect("Could not respond");

    // Verify that reporter's view is able to correctly read the names of the
    // children of the parent echo_realm.
    let responder = test_runner
        .verify_local_directory_listing("/parent_hub/children", vec!["echo_server", "reporter"])
        .await;
    responder.send().expect("Could not respond");

    // Verify that reporter is able to see its sibling's hub correctly.
    test_runner
        .verify_local_file_content(
            "/sibling_hub/exec/resolved_url",
            "fuchsia-pkg://fuchsia.com/hub_integration_test#meta/echo_server.cm",
        )
        .await;

    // Verify that reporter has only connected to the HubReport and Breakpoint capabilities
    let responder = test_runner
        .verify_local_directory_listing(
            "/hub/used/svc",
            vec![breakpoints_service_name, hub_report_service_name],
        )
        .await;
    responder.send().expect("Could not respond");

    // Wait for the reporter to connect to the Echo capability
    test_runner.expect_invocation(EventType::UseCapability, vec!["reporter:0"]).await.resume();

    // Verify that the hub now shows the Echo capability as in use
    let responder = test_runner
        .verify_local_directory_listing(
            "/hub/used/svc",
            vec![echo_service_name, breakpoints_service_name, hub_report_service_name],
        )
        .await;
    responder.send().expect("Could not respond");

    // Wait for the reporter to connect to the Echo capability again
    test_runner.expect_invocation(EventType::UseCapability, vec!["reporter:0"]).await.resume();

    // Verify that the hub does not change
    let responder = test_runner
        .verify_local_directory_listing(
            "/hub/used/svc",
            vec![echo_service_name, breakpoints_service_name, hub_report_service_name],
        )
        .await;
    responder.send().expect("Could not respond");

    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn dynamic_child_test() -> Result<(), Error> {
    let root_component_url =
        "fuchsia-pkg://fuchsia.com/hub_integration_test#meta/dynamic_child_reporter.cm";
    let test_runner = TestRunner::new_with_breakpoints(
        root_component_url,
        vec![
            EventType::PreDestroyInstance,
            EventType::StopInstance,
            EventType::PostDestroyInstance,
        ],
    )
    .await?;

    // Verify that the dynamic child exists in the parent's hub
    test_runner.verify_directory_listing("children", vec!["coll:simple_instance"]).await;

    // Before binding, verify that the dynamic child's hub has the directories we expect
    // i.e. "children" and "url" but no "exec" because the child has not been bound.
    test_runner
        .verify_directory_listing(
            "children/coll:simple_instance",
            vec!["children", "deleting", "id", "url"],
        )
        .await;

    // Verify that the dynamic child has the correct instance id.
    test_runner.verify_local_file_content("/hub/children/coll:simple_instance/id", "1").await;

    // Before binding, verify that the dynamic child's static children are invisible
    test_runner.verify_directory_listing("children/coll:simple_instance/children", vec![]).await;

    // After binding, verify that the dynamic child's hub has the directories we expect
    test_runner
        .verify_directory_listing(
            "children/coll:simple_instance",
            vec!["children", "deleting", "exec", "id", "url"],
        )
        .await;

    // After binding, verify that the dynamic child's static child is visible
    test_runner
        .verify_directory_listing("children/coll:simple_instance/children", vec!["child"])
        .await;

    // Verify that the dynamic child's static child has the correct instance id.
    test_runner
        .verify_local_file_content("/hub/children/coll:simple_instance/children/child/id", "0")
        .await;

    // Wait for the dynamic child to begin deletion
    let breakpoint = test_runner
        .expect_invocation(EventType::PreDestroyInstance, vec!["coll:simple_instance:1"])
        .await;

    // When deletion begins, the dynamic child should be moved to the deleting directory
    test_runner.verify_directory_listing("children", vec![]).await;
    test_runner.verify_directory_listing("deleting", vec!["coll:simple_instance:1"]).await;
    test_runner
        .verify_directory_listing(
            "deleting/coll:simple_instance:1",
            vec!["children", "deleting", "exec", "id", "url"],
        )
        .await;

    // Unblock the ComponentManager
    breakpoint.resume();

    // Wait for the dynamic child to stop
    let breakpoint = test_runner
        .expect_invocation(EventType::StopInstance, vec!["coll:simple_instance:1"])
        .await;

    // After stopping, the dynamic child should not have an exec directory
    test_runner
        .verify_directory_listing(
            "deleting/coll:simple_instance:1",
            vec!["children", "deleting", "id", "url"],
        )
        .await;

    // Unblock the Component Manager
    breakpoint.resume();

    // Wait for the dynamic child's static child to begin deletion
    let breakpoint = test_runner
        .expect_invocation(EventType::PreDestroyInstance, vec!["coll:simple_instance:1", "child:0"])
        .await;

    // When deletion begins, the dynamic child's static child should be moved to the deleting directory
    test_runner.verify_directory_listing("deleting/coll:simple_instance:1/children", vec![]).await;
    test_runner
        .verify_directory_listing("deleting/coll:simple_instance:1/deleting", vec!["child:0"])
        .await;
    test_runner
        .verify_directory_listing(
            "deleting/coll:simple_instance:1/deleting/child:0",
            vec!["children", "deleting", "id", "url"],
        )
        .await;

    // Unblock the Component Manager
    breakpoint.resume();

    // Wait for the dynamic child's static child to be destroyed
    let breakpoint = test_runner
        .expect_invocation(
            EventType::PostDestroyInstance,
            vec!["coll:simple_instance:1", "child:0"],
        )
        .await;

    // The dynamic child's static child should not be visible in the hub anymore
    test_runner.verify_directory_listing("deleting/coll:simple_instance:1/deleting", vec![]).await;

    // Unblock the Component Manager
    breakpoint.resume();

    // Wait for the dynamic child to be destroyed
    let breakpoint = test_runner
        .expect_invocation(EventType::PostDestroyInstance, vec!["coll:simple_instance:1"])
        .await;

    // After deletion, verify that parent can no longer see the dynamic child in the Hub
    test_runner.verify_directory_listing("deleting", vec![]).await;

    // Unblock the Component Manager
    breakpoint.resume();

    // Wait for the component to stop
    test_runner.wait_for_component_stop().await;

    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn visibility_test() -> Result<(), Error> {
    let root_component_url =
        "fuchsia-pkg://fuchsia.com/hub_integration_test#meta/visibility_reporter.cm";
    let test_runner = TestRunner::new(root_component_url).await?;

    // Verify that the child exists in the parent's hub
    test_runner.verify_directory_listing("children", vec!["child"]).await;

    // Verify that the child's hub has the directories we expect
    // i.e. no "exec" because the child has not been bound.
    test_runner
        .verify_directory_listing("children/child", vec!["children", "deleting", "id", "url"])
        .await;

    // Verify that the grandchild is not shown because the child is lazy
    test_runner.verify_directory_listing("children/child/children", vec![]).await;
    Ok(())
}
