use anyhow::Error;
use breakpoint_system_client::*;
use cs2::Component;
use std::path::PathBuf;
use test_utils::*;

fn launch_cs2(hub_v2_path: PathBuf) -> Vec<String> {
    // Do exactly what cs2 does. Point to HubV2 and get output.
    Component::new_root_component(hub_v2_path).generate_output()
}

fn compare_output(actual: Vec<String>, expected: Vec<&str>) {
    // Print out the outputs in a readable format
    let print_actual = actual.join("\n");
    let print_actual = format!("------------ ACTUAL OUTPUT ----------------\n{}\n-------------------------------------------", print_actual);
    let print_expected = expected.join("\n");
    let print_expected = format!("------------ EXPECT OUTPUT ----------------\n{}\n-------------------------------------------", print_expected);
    println!("{}\n{}", print_actual, print_expected);

    // Now compare
    let actual: Vec<&str> = actual.iter().map(|line| line.as_str()).collect();
    assert_eq!(actual, expected);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn empty_component() -> Result<(), Error> {
    let test = BlackBoxTest::default("fuchsia-pkg://fuchsia.com/cs2_test#meta/empty.cm").await?;
    let breakpoint_system =
        test.connect_to_breakpoint_system().await.expect("Failed to connect to breakpoint system");
    let receiver = breakpoint_system.set_breakpoints(vec![BeforeStartInstance::TYPE]).await?;

    breakpoint_system.start_component_tree().await?;

    // Root must be created first
    let invocation = receiver.expect_exact::<BeforeStartInstance>(".").await?;
    invocation.resume().await?;

    let actual = launch_cs2(test.get_hub_v2_path());
    compare_output(
        actual,
        vec![
            "<root>",
            "",
            "<root>:0",
            "- URL: fuchsia-pkg://fuchsia.com/cs2_test#meta/empty.cm",
            "- Component Type: static",
            "- Exposed Services (0)",
            "- Incoming Services (0)",
            "- Outgoing Services (0)",
            "- Used Services (0)",
        ],
    );
    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn tree() -> Result<(), Error> {
    let test = BlackBoxTest::default("fuchsia-pkg://fuchsia.com/cs2_test#meta/root.cm").await?;
    let breakpoint_system =
        test.connect_to_breakpoint_system().await.expect("Failed to connect to breakpoint system");
    let receiver = breakpoint_system.set_breakpoints(vec![BeforeStartInstance::TYPE]).await?;

    breakpoint_system.start_component_tree().await?;

    // Root must be created first
    let invocation = receiver.expect_exact::<BeforeStartInstance>(".").await?;
    invocation.resume().await?;

    // 6 descendants are created eagerly. Order is irrelevant.
    for _ in 1..=6 {
        let invocation = receiver.expect_type::<BeforeStartInstance>().await?;
        invocation.resume().await?;
    }

    let actual = launch_cs2(test.get_hub_v2_path());
    compare_output(
        actual,
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
            "- Exposed Services (0)",
            "- Incoming Services (0)",
            "- Outgoing Services (0)",
            "- Used Services (0)",
            "",
            "<root>:0/bar:0",
            "- URL: fuchsia-pkg://fuchsia.com/cs2_test#meta/bar.cm",
            "- Component Type: static",
            "- Exposed Services (0)",
            "- Incoming Services (0)",
            "- Outgoing Services (0)",
            "- Used Services (0)",
            "",
            "<root>:0/bar:0/baz:0",
            "- URL: fuchsia-pkg://fuchsia.com/cs2_test#meta/baz.cm",
            "- Component Type: static",
            "- Exposed Services (0)",
            "- Incoming Services (0)",
            "- Outgoing Services (0)",
            "- Used Services (0)",
            "",
            "<root>:0/foo:0",
            "- URL: fuchsia-pkg://fuchsia.com/cs2_test#meta/foo.cm",
            "- Component Type: static",
            "- Exposed Services (0)",
            "- Incoming Services (0)",
            "- Outgoing Services (0)",
            "- Used Services (0)",
            "",
            "<root>:0/foo:0/bar:0",
            "- URL: fuchsia-pkg://fuchsia.com/cs2_test#meta/bar.cm",
            "- Component Type: static",
            "- Exposed Services (0)",
            "- Incoming Services (0)",
            "- Outgoing Services (0)",
            "- Used Services (0)",
            "",
            "<root>:0/foo:0/bar:0/baz:0",
            "- URL: fuchsia-pkg://fuchsia.com/cs2_test#meta/baz.cm",
            "- Component Type: static",
            "- Exposed Services (0)",
            "- Incoming Services (0)",
            "- Outgoing Services (0)",
            "- Used Services (0)",
            "",
            "<root>:0/foo:0/baz:0",
            "- URL: fuchsia-pkg://fuchsia.com/cs2_test#meta/baz.cm",
            "- Component Type: static",
            "- Exposed Services (0)",
            "- Incoming Services (0)",
            "- Outgoing Services (0)",
            "- Used Services (0)",
        ],
    );
    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn echo_realm() -> Result<(), Error> {
    let test =
        BlackBoxTest::default("fuchsia-pkg://fuchsia.com/cs2_test#meta/echo_realm.cm").await?;
    let breakpoint_system =
        test.connect_to_breakpoint_system().await.expect("Failed to connect to breakpoint system");

    let receiver = breakpoint_system.set_breakpoints(vec![BeforeStartInstance::TYPE]).await?;
    breakpoint_system.start_component_tree().await?;

    // 3 components are started. Order is irrelevant.
    // root and echo_client are started eagerly.
    // echo_server is started after echo_client connects to the Echo service.
    for _ in 1..=3 {
        let invocation = receiver.expect_type::<BeforeStartInstance>().await?;
        invocation.resume().await?;
    }

    let actual = launch_cs2(test.get_hub_v2_path());
    compare_output(
        actual,
        vec![
            "<root>",
            "  echo_client",
            "  echo_server",
            "",
            "<root>:0",
            "- URL: fuchsia-pkg://fuchsia.com/cs2_test#meta/echo_realm.cm",
            "- Component Type: static",
            "- Exposed Services (0)",
            "- Incoming Services (0)",
            "- Outgoing Services (0)",
            "- Used Services (0)",
            "",
            "<root>:0/echo_client:0",
            "- URL: fuchsia-pkg://fuchsia.com/cs2_test#meta/echo_client.cm",
            "- Component Type: static",
            "- Exposed Services (0)",
            "- Incoming Services (1)",
            "  - fidl.examples.routing.echo.Echo",
            "- Outgoing Services (0)",
            "- Used Services (1)",
            "  - fidl.examples.routing.echo.Echo",
            "",
            "<root>:0/echo_server:0",
            "- URL: fuchsia-pkg://fuchsia.com/cs2_test#meta/echo_server.cm",
            "- Component Type: static",
            "- Exposed Services (0)",
            "- Incoming Services (0)",
            "- Outgoing Services (1)",
            "  - fidl.examples.routing.echo.Echo",
            "- Used Services (0)",
        ],
    );

    Ok(())
}
