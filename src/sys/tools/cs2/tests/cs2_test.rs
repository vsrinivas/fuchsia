use breakpoint_system_client::*;
use cs2::Component;
use failure::Error;
use std::path::PathBuf;
use test_utils::*;

fn launch_cs2(hub_v2_path: PathBuf) -> Vec<String> {
    // Do exactly what cs2 does. Point to HubV2 and get output.
    Component::new_root_component(hub_v2_path).generate_output()
}

#[fuchsia_async::run_singlethreaded(test)]
async fn empty_component() -> Result<(), Error> {
    let test = BlackBoxTest::default("fuchsia-pkg://fuchsia.com/cs2_test#meta/empty.cm").await?;
    let receiver = test.breakpoint_system.set_breakpoints(vec![StartInstance::TYPE]).await?;

    test.breakpoint_system.start_component_manager().await?;

    // Root must be created first
    let invocation = receiver.expect_exact::<StartInstance>("/").await?;
    invocation.resume().await?;

    let output = launch_cs2(test.hub_v2_path);
    let output: Vec<&str> = output.iter().map(|line| line.as_str()).collect();
    assert_eq!(
        output,
        vec![
            "<root>",
            "",
            "<root>:0",
            "- URL: fuchsia-pkg://fuchsia.com/cs2_test#meta/empty.cm",
            "- Component Type: static"
        ]
    );
    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn tree() -> Result<(), Error> {
    let test = BlackBoxTest::default("fuchsia-pkg://fuchsia.com/cs2_test#meta/root.cm").await?;
    let receiver = test.breakpoint_system.set_breakpoints(vec![StartInstance::TYPE]).await?;

    test.breakpoint_system.start_component_manager().await?;

    // Root must be created first
    let invocation = receiver.expect_exact::<StartInstance>("/").await?;
    invocation.resume().await?;

    // 5 children are created eagerly. Order is irrelevant.
    for _ in 1..=5 {
        let invocation = receiver.expect_type::<StartInstance>().await?;
        invocation.resume().await?;
    }

    let output = launch_cs2(test.hub_v2_path);
    let output: Vec<&str> = output.iter().map(|line| line.as_str()).collect();
    assert_eq!(
        output,
        vec![
            "<root>",
            "  bar",
            "    baz",
            "  foo",
            "    bar",
            "      baz",
            "    baz",
            "",
            "<root>:0",
            "- URL: fuchsia-pkg://fuchsia.com/cs2_test#meta/root.cm",
            "- Component Type: static",
            "",
            "<root>:0/bar:0",
            "- URL: fuchsia-pkg://fuchsia.com/cs2_test#meta/bar.cm",
            "- Component Type: static",
            "",
            "<root>:0/bar:0/baz:0",
            "- URL: fuchsia-pkg://fuchsia.com/cs2_test#meta/baz.cm",
            "- Component Type: static",
            "",
            "<root>:0/foo:0",
            "- URL: fuchsia-pkg://fuchsia.com/cs2_test#meta/foo.cm",
            "- Component Type: static",
            "",
            "<root>:0/foo:0/bar:0",
            "- URL: fuchsia-pkg://fuchsia.com/cs2_test#meta/bar.cm",
            "- Component Type: static",
            "",
            "<root>:0/foo:0/bar:0/baz:0",
            "- URL: fuchsia-pkg://fuchsia.com/cs2_test#meta/baz.cm",
            "- Component Type: static",
            "",
            "<root>:0/foo:0/baz:0",
            "- URL: fuchsia-pkg://fuchsia.com/cs2_test#meta/baz.cm",
            "- Component Type: static"
        ]
    );
    Ok(())
}
