// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A simple client to test functionality of examples/fidl/calculator/server.
//!
//! This component connects to the Calculator FIDL service defined in
//! [calculator.fidl](../fidl/calculator.fidl)
//!
//! You should be using this component when you want to test that the
//! calculator-server is:
//!
//! 1. Running
//! 2. Functioning as expected

mod parse;

use anyhow;
use fidl;
use fidl_fuchsia_examples_calculator::{CalculatorMarker, CalculatorProxy};
use fuchsia_component::client::connect_to_protocol;
use std::fs;
use tracing;

/// Entry-point into the client.
#[fuchsia::main]
async fn main() -> Result<(), anyhow::Error> {
    // `CalculatorMarker` is generated code. The build rule `fidl("calculator")`
    // in <../../../fidl/BUILD.gn> generates the necessary targets so
    // <../BUILD.gn> can rely on
    // `"//examples/fidl/calculator/fidl:calculator_rust"` to make this
    // available.
    let calculator =
        connect_to_protocol::<CalculatorMarker>().expect("Error connecting to Calculator Service.");

    // Note the path starts with /pkg/ even though the build rule
    // `resource("input")` uses `data/input.txt`. At runtime, components are
    // able to read the contents of their own package by accessing the path
    // /pkg/ in their namespace. See
    // https://fuchsia.dev/fuchsia-src/development/components/data#including_resources_with_a_component
    // for more details.
    let input = fs::read_to_string("/pkg/data/input.txt")?;

    for line in input.lines() {
        let result = calculator_line(line, &calculator).await;
        match result {
            Ok(result) => tracing::info!("{} = {}", &line, result),
            Err(msg) => tracing::info!("Error with expression '{}': {}", &line, &msg),
        }
    }

    Ok(())
}

async fn calculator_line(line: &str, calculator: &CalculatorProxy) -> Result<f64, fidl::Error> {
    let parse::Expression::Leaf(left, op, right) = parse::parse(line);
    match op {
        parse::Operator::Add => calculator.add(left, right),
        parse::Operator::Subtract => calculator.subtract(left, right),
        parse::Operator::Multiply => calculator.multiply(left, right),
        parse::Operator::Divide => calculator.divide(left, right),
        parse::Operator::Pow => calculator.pow(left, right),
    }
    .await
}

#[cfg(test)]
mod tests {
    use super::calculator_line;
    use anyhow::Context;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_examples_calculator::{
        CalculatorMarker, CalculatorRequest, CalculatorRequestStream,
    };
    use futures::FutureExt;
    use futures::StreamExt;
    use futures::TryStreamExt;

    // A fake of the calculator service.
    async fn calculator_fake(stream: CalculatorRequestStream) -> anyhow::Result<()> {
        stream
            .map(|result| result.context("failed request"))
            .try_for_each(|request| async {
                (match request {
                    CalculatorRequest::Add { a, b, responder } => responder.send(a + b),
                    CalculatorRequest::Subtract { a, b, responder } => responder.send(a - b),
                    CalculatorRequest::Multiply { a, b, responder } => responder.send(a * b),
                    CalculatorRequest::Divide { dividend, divisor, responder } => {
                        responder.send(dividend / divisor)
                    }
                    CalculatorRequest::Pow { base, exponent, responder } => {
                        responder.send(base.powf(exponent))
                    }
                })
                .context("Error sending response")?;
                Ok(())
            })
            .await
            .expect("Error running calculator fake");
        Ok(())
    }

    #[fuchsia::test]
    async fn add_test() {
        let (proxy, stream) = create_proxy_and_stream::<CalculatorMarker>()
            .expect("Failed to create proxy and stream.");

        // Run two tasks: The calculator_fake & the calculator_line method we're interested
        // in testing.
        let fake_task = calculator_fake(stream).fuse();
        let calculator_line_task = calculator_line("1 + 2", &proxy).fuse();
        futures::pin_mut!(fake_task, calculator_line_task);
        futures::select! {
           actual = calculator_line_task => {
                let actual = actual.expect("Calculator didn't return value");
                assert_eq!(actual, 3.0);
           },
           _ = fake_task => {
               panic!("Fake should never complete.")
           }
        };
    }

    #[fuchsia::test]
    async fn subtract_test() {
        let (proxy, stream) = create_proxy_and_stream::<CalculatorMarker>()
            .expect("Failed to create proxy and stream.");

        // Run two tasks: The calculator_fake & the calculator_line method we're interested
        // in testing.
        let fake_task = calculator_fake(stream).fuse();
        let calculator_line_task = calculator_line("1.234 - -2.456", &proxy).fuse();
        futures::pin_mut!(fake_task, calculator_line_task);
        futures::select! {
           actual = calculator_line_task => {
                let actual = actual.expect("Calculator didn't return value");
                assert_eq!(actual, 3.69);
           },
           _ = fake_task => {
               panic!("Fake should never complete.")
           }
        };
    }

    #[fuchsia::test]
    async fn multiply_test() {
        let (proxy, stream) = create_proxy_and_stream::<CalculatorMarker>()
            .expect("Failed to create proxy and stream.");

        // Run two tasks: The calculator_fake & the calculator_line method we're interested
        // in testing.
        let fake_task = calculator_fake(stream).fuse();
        let calculator_line_task = calculator_line("1.5 * 2.0", &proxy).fuse();
        futures::pin_mut!(fake_task, calculator_line_task);
        futures::select! {
           actual = calculator_line_task => {
                let actual = actual.expect("Calculator didn't return value");
                assert_eq!(actual, 3.0);
           },
           _ = fake_task => {
               panic!("Fake should never complete.")
           }
        };
    }

    #[fuchsia::test]
    async fn divide_test() {
        let (proxy, stream) = create_proxy_and_stream::<CalculatorMarker>()
            .expect("Failed to create proxy and stream.");

        // Run two tasks: The calculator_fake & the calculator_line method we're interested
        // in testing.
        let fake_task = calculator_fake(stream).fuse();
        let calculator_line_task = calculator_line("1.5 / 3.0", &proxy).fuse();
        futures::pin_mut!(fake_task, calculator_line_task);
        futures::select! {
           actual = calculator_line_task => {
                let actual = actual.expect("Calculator didn't return value");
                assert_eq!(actual, 0.5);
           },
           _ = fake_task => {
               panic!("Fake should never complete.")
           }
        };
    }

    #[fuchsia::test]
    async fn pow_test() {
        let (proxy, stream) = create_proxy_and_stream::<CalculatorMarker>()
            .expect("Failed to create proxy and stream.");

        // Run two tasks: The calculator_fake & the calculator_line method we're interested
        // in testing.
        let fake_task = calculator_fake(stream).fuse();
        let calculator_line_task = calculator_line("3.0 ^ 4.0", &proxy).fuse();
        futures::pin_mut!(fake_task, calculator_line_task);
        futures::select! {
           actual = calculator_line_task => {
                let actual = actual.expect("Calculator didn't return value");
                assert_eq!(actual, 81.0);
           },
           _ = fake_task => {
               panic!("Fake should never complete.")
           }
        };
    }
}
