// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A server to handle calculator requests.
//!
//! This component (and the accompying parent realm) is a realistic example of
//! how to create & route client/server components in Fuchsia. It aims to be
//! fully fleshed out and showcase best practices such as:
//!
//! 1. Testing
//! 2. Exposing capabilities
//! 3. Well commented code
//! 4. FIDL interaction
//! 5. Error handling

use anyhow::{self, Context};
use fidl_fuchsia_examples_calculator::{CalculatorRequest, CalculatorRequestStream};
use fuchsia_component::server::ServiceFs;
use fuchsia_inspect::{component, health::Reporter};
use futures::prelude::*;
use tracing;

/// Wraps all hosted protocols into a single type that can be matched against
/// and dispatched.
enum IncomingRequest {
    Calculator(CalculatorRequestStream),
}

/// Calculator server entry point.
#[fuchsia::main]
async fn main() -> Result<(), anyhow::Error> {
    let mut service_fs = ServiceFs::new_local();

    // Initialize inspect.
    inspect_runtime::serve(component::inspector(), &mut service_fs)?;
    component::health().set_starting_up();

    service_fs.dir("svc").add_fidl_service(IncomingRequest::Calculator);

    service_fs.take_and_serve_directory_handle().context("failed to serve outgoing namespace")?;

    component::health().set_ok();
    tracing::debug!("Initialized.");

    service_fs
        .for_each_concurrent(None, |request: IncomingRequest| async move {
            match request {
                IncomingRequest::Calculator(stream) => handle_calculator_request(stream)
                    .await
                    .expect("Could not handle calculator request."),
            }
        })
        .await;

    Ok(())
}

/// Handler for incoming service requests.
async fn handle_calculator_request(stream: CalculatorRequestStream) -> anyhow::Result<()> {
    stream
        .try_for_each(|request| async {
            match request {
                CalculatorRequest::Add { a, b, responder } => responder.send(a + b),
                CalculatorRequest::Subtract { a, b, responder } => responder.send(a - b),
                CalculatorRequest::Multiply { a, b, responder } => responder.send(a * b),
                CalculatorRequest::Divide { dividend, divisor, responder } => {
                    responder.send(dividend / divisor)
                }
                CalculatorRequest::Pow { base, exponent, responder } => {
                    responder.send(base.powf(exponent))
                }
            }
        })
        .await
        .expect("failed to serve calculator service.");
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::handle_calculator_request;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_examples_calculator::CalculatorMarker;
    use futures::FutureExt;

    #[fuchsia::test]
    async fn test_add() {
        let (proxy, stream) = create_proxy_and_stream::<CalculatorMarker>()
            .expect("Couldn't create test proxy and stream.");
        let handle_calculator_request_task = handle_calculator_request(stream).fuse();
        let proxy_task = proxy.add(4.5, 3.2).fuse();
        futures::pin_mut!(handle_calculator_request_task, proxy_task);
        futures::select! {
            actual = proxy_task => {
                let actual = actual.expect("Add proxy didn't return value.");
                assert_eq!(actual, 7.7);
            },
            _ = handle_calculator_request_task => {
                panic!("handle_calculator_request should never complete.")
            }
        }
    }

    #[fuchsia::test]
    async fn test_subtract() {
        let (proxy, stream) = create_proxy_and_stream::<CalculatorMarker>()
            .expect("Couldn't create test proxy and stream.");
        let handle_calculator_request_task = handle_calculator_request(stream).fuse();
        let proxy_task = proxy.subtract(7.7, 3.2).fuse();
        futures::pin_mut!(handle_calculator_request_task, proxy_task);
        futures::select! {
            actual = proxy_task => {
                let actual = actual.expect("Subtract proxy didn't return value.");
                assert_eq!(actual, 4.5);
            },
            _ = handle_calculator_request_task => {
                panic!("handle_calculator_request should never complete.")
            }
        }
    }

    #[fuchsia::test]
    async fn test_multiply() {
        let (proxy, stream) = create_proxy_and_stream::<CalculatorMarker>()
            .expect("Couldn't create test proxy and stream.");
        let handle_calculator_request_task = handle_calculator_request(stream).fuse();
        let proxy_task = proxy.multiply(1.5, 2.0).fuse();
        futures::pin_mut!(handle_calculator_request_task, proxy_task);
        futures::select! {
            actual = proxy_task => {
                let actual = actual.expect("Multiply proxy didn't return value.");
                assert_eq!(actual, 3.0);
            },
            _ = handle_calculator_request_task => {
                panic!("handle_calculator_request should never complete.")
            }
        }
    }

    #[fuchsia::test]
    async fn test_divide() {
        let (proxy, stream) = create_proxy_and_stream::<CalculatorMarker>()
            .expect("Couldn't create test proxy and stream.");
        let handle_calculator_request_task = handle_calculator_request(stream).fuse();
        let proxy_task = proxy.divide(2.0, 4.0).fuse();
        futures::pin_mut!(handle_calculator_request_task, proxy_task);
        futures::select! {
            actual = proxy_task => {
                let actual = actual.expect("Divide proxy didn't return value.");
                assert_eq!(actual, 0.5);
            },
            _ = handle_calculator_request_task => {
                panic!("handle_calculator_request should never complete.")
            }
        }
    }

    #[fuchsia::test]
    async fn test_pow() {
        let (proxy, stream) = create_proxy_and_stream::<CalculatorMarker>()
            .expect("Couldn't create test proxy and stream.");
        let handle_calculator_request_task = handle_calculator_request(stream).fuse();
        let proxy_task = proxy.pow(3.0, 4.0).fuse();
        futures::pin_mut!(handle_calculator_request_task, proxy_task);
        futures::select! {
            actual = proxy_task => {
                let actual = actual.expect("Pow proxy didn't return value.");
                assert_eq!(actual, 81.0);
            },
            _ = handle_calculator_request_task => {
                panic!("handle_calculator_request should never complete.")
            }
        }
    }
}
