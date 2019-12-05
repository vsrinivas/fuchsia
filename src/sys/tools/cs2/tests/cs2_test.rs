use breakpoint_system_client::*;
use cs2::Component;
use failure::Error;
use std::path::PathBuf;
use test_utils::*;

fn launch_cs2(hub_v2_path: PathBuf) -> String {
    // Do exactly what cs2 does. Point to HubV2 and get output.
    Component::new_root_component(hub_v2_path).generate_output()
}

#[fuchsia_async::run_singlethreaded(test)]
async fn empty_component() -> Result<(), Error> {
    let test = BlackBoxTest::default("fuchsia-pkg://fuchsia.com/cs2_test#meta/empty.cm").await?;
    let receiver = test.breakpoint_system.register(vec![StartInstance::TYPE]).await?;

    test.breakpoint_system.start_component_manager().await?;

    // Root must be created first
    let invocation = receiver.expect_exact::<StartInstance>("/").await?;
    invocation.resume().await?;

    let output = launch_cs2(test.hub_v2_path);
    assert_eq!(output, "<root>");
    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn tree() -> Result<(), Error> {
    let test = BlackBoxTest::default("fuchsia-pkg://fuchsia.com/cs2_test#meta/root.cm").await?;
    let receiver = test.breakpoint_system.register(vec![StartInstance::TYPE]).await?;

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
    assert_eq!(output, "<root>\n  bar\n    baz\n  foo\n    bar\n      baz\n    baz");
    Ok(())
}
