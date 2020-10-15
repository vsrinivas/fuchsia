// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common_utils::common::{read_json_from_vmo, write_json_to_vmo};
use crate::server::constants::CONCURRENT_REQ_LIMIT;
use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use fidl_fuchsia_testing_sl4f::{
    FacadeIteratorRequest, FacadeProviderRequest, FacadeProviderRequestStream,
};
use fuchsia_syslog::macros::{fx_log_err, fx_log_info, fx_log_warn};
use futures::stream::{StreamExt, TryStreamExt};
use serde_json::Value;
use std::sync::Arc;

/// Trait for the implementation of the FacadeProvider protocol.
#[async_trait(?Send)]
pub trait FacadeProvider {
    /// Returns the object implementing Facade for the given facade name.
    /// # Arguments:
    /// * 'name' - A string representing the name of the facade.
    fn get_facade(&self, name: &str) -> Option<Arc<dyn Facade>>;

    /// Returns an iterator over all of the Facade implementations.
    fn get_facades(&self) -> Box<dyn Iterator<Item = &Arc<dyn Facade>> + '_>;

    /// Returns an iterator over all of the facade names.
    fn get_facade_names(&self) -> Box<dyn ExactSizeIterator<Item = &str> + '_>;

    /// Invoked on FacadeProvider.GetFacades(). Sends the list of hosted facades back to the client on
    /// subsequent calls to FacadeIterator.GetNext() over the channel given to GetFacades().
    /// # Arguments
    /// * 'request' - A request from the FacadeProvider client. Must be a GetFacades() request.
    async fn get_facades_impl(&self, request: FacadeProviderRequest) {
        let iterator = match request {
            FacadeProviderRequest::GetFacades { iterator, control_handle: _ } => iterator,
            _ => panic!(
                "get_facade_impl() must only be called with a FacadeProviderRequest::GetFacades."
            ),
        };

        // Wrap operation in an async block in order to capture any error.
        let get_facades_fut = async {
            let mut iterator = iterator.into_stream()?;
            if let Some(FacadeIteratorRequest::GetNext { responder }) = iterator.try_next().await? {
                // NOTE: if the list of facade names would exceed the channel buffer size,
                // they should be split over back-to-back responses to GetNext().
                responder.send(&mut self.get_facade_names())?;
                if let Some(FacadeIteratorRequest::GetNext { responder }) =
                    iterator.try_next().await?
                {
                    responder.send(&mut std::iter::empty())?; // Indicates completion.
                }
            }
            Ok::<(), Error>(())
        };
        if let Err(error) = get_facades_fut.await {
            fx_log_err!("Failed to handle GetFacades() with: {}", error);
        }
    }

    /// Invoked on FacadeProvider.Execute(). Executes the given command on a hosted facade.
    /// # Arguments
    /// * 'request' - A request from a FacadeProvider client. Must be an Execute() request.
    async fn execute_impl(&self, request: FacadeProviderRequest) {
        let (facade, command, params_blob, responder) = match request {
            FacadeProviderRequest::Execute { facade, command, params_blob, responder } => {
                (facade, command, params_blob, responder)
            }
            _ => {
                panic!("execute_impl() must only be called with a FacadeProviderRequest::Execute.")
            }
        };

        // Look-up the facade.
        let facade = if let Some(f) = self.get_facade(&facade) {
            f
        } else {
            let err_str = format!("Could not find facade: {}", facade);
            fx_log_err!("{}", err_str);
            if let Err(send_error) = responder.send(None, Some(&err_str)) {
                fx_log_err!("Failed to send response with: {}", send_error);
            }
            return;
        };

        // Construct a JSON Value out of the params_blob.
        let params = match read_json_from_vmo(&params_blob) {
            Ok(value) => value,
            Err(error) => {
                if let Err(send_error) =
                    responder.send(None, Some(&format!("Failed to extract params: {}", error)))
                {
                    fx_log_err!("Failed to send response with: {}", send_error);
                }
                return;
            }
        };

        // Execute the command on the facade. On error or empty result, send the response.
        let result = match facade.handle_request(command, params).await {
            Ok(Value::Null) => {
                if let Err(send_error) = responder.send(None, None) {
                    fx_log_err!("Failed to send response with: {}", send_error);
                }
                return;
            }
            Ok(result) => result,
            Err(error) => {
                if let Err(send_error) = responder.send(None, Some(&error.to_string())) {
                    fx_log_err!("Failed to send response with: {}", send_error);
                }
                return;
            }
        };

        // Write the result blob into a VMO and send the response.
        if let Err(send_error) = match write_json_to_vmo(&params_blob, &result) {
            Ok(()) => responder.send(Some(params_blob), None),
            Err(error) => responder.send(None, Some(&format!("Failed to write result: {}", error))),
        } {
            fx_log_err!("Failed to send response with: {}", send_error);
        }
    }

    /// Cleans up transient state on all hosted facades. Invoked on FacadeProvider.Cleanup().
    fn cleanup_impl(&self) {
        for facade in self.get_facades() {
            facade.cleanup();
        }
    }

    /// Prints state for all hosted facades. Invoked on FacadeProvider.Print().
    fn print_impl(&self) {
        for facade in self.get_facades() {
            facade.print();
        }
    }

    /// Invoked on each incoming request. Invokes the appropriate handler code.
    /// # Arguments
    /// * 'request' - Incoming request on the FacadeProvider connection.
    async fn handle_request(&self, request: FacadeProviderRequest) {
        match request {
            FacadeProviderRequest::GetFacades { iterator, control_handle } => {
                self.get_facades_impl(FacadeProviderRequest::GetFacades {
                    iterator,
                    control_handle,
                })
                .await;
            }
            FacadeProviderRequest::Execute { facade, command, params_blob, responder } => {
                fx_log_info!("Received command {}.{}", facade, command);
                self.execute_impl(FacadeProviderRequest::Execute {
                    facade,
                    command,
                    params_blob,
                    responder,
                })
                .await;
            }
            FacadeProviderRequest::Cleanup { responder } => {
                self.cleanup_impl();
                if let Err(error) = responder.send() {
                    fx_log_warn!("Failed to notify completion of Cleanup() with: {}", error);
                }
            }
            FacadeProviderRequest::Print { responder } => {
                self.print_impl();
                if let Err(error) = responder.send() {
                    fx_log_err!("Failed to notify completion of Print() with: {}", error);
                }
            }
        }
    }

    /// Invoked on an incoming FacadeProvider connection request. Requests arriving on the stream
    /// will be handled concurrently.
    /// NOTE: The main SL4F server doesn't appear to wait before any outstanding requests are
    /// completed before allowing a cleanup operation to move forward, and this code similarly
    /// makes no such effort. As such, it is up to the test harness to ensure that cleanup and
    /// command requests do not interleave in order to ensure that the facades and device remain in
    /// a consistent state.
    /// # Arguments
    /// * 'stream' - Incoming FacadeProvider request stream.
    async fn run_facade_provider(&self, stream: FacadeProviderRequestStream) {
        stream
            .for_each_concurrent(CONCURRENT_REQ_LIMIT, |request| {
                self.handle_request(request.unwrap())
            })
            .await;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use anyhow::format_err;
    use fidl_fuchsia_testing_sl4f::FacadeProviderMarker;
    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;
    use pin_utils::pin_mut;
    use serde_json::json;
    use std::cell::RefCell;
    use std::collections::HashMap;
    use std::task::Poll;

    /// TestFacade provides a trivial Facade implementation which supports commands to interact
    /// with the state.
    #[derive(Debug)]
    struct TestFacade {
        // Trivial state accessed through TestFacade's implementation of Facade.
        state: RefCell<bool>,
    }

    impl TestFacade {
        pub fn new() -> TestFacade {
            TestFacade { state: RefCell::new(false) }
        }
    }

    #[async_trait(?Send)]
    impl Facade for TestFacade {
        async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
            match method.as_str() {
                "set" => {
                    if let Value::Bool(new_state) = args {
                        *self.state.borrow_mut() = new_state;
                        return Ok(json!(null));
                    }
                    panic!("Received invalid args");
                }
                "get" => {
                    if let Value::Null = args {
                        return Ok(json!(*self.state.borrow()));
                    }
                    panic!("Received invalid args");
                }
                _ => return Err(format_err!("Invalid TestFacade method {}", method)),
            }
        }

        fn cleanup(&self) {
            *self.state.borrow_mut() = false;
        }

        fn print(&self) {}
    }

    /// TestFacadeProvider provides a simple implementation of the FacadeProvider trait and hosts
    /// the TestFacade.
    struct TestFacadeProvider {
        facades: HashMap<String, Arc<dyn Facade>>,
    }

    impl TestFacadeProvider {
        pub fn new() -> TestFacadeProvider {
            let mut facades: HashMap<String, Arc<dyn Facade>> = HashMap::new();
            facades
                .insert("test_facade".to_string(), Arc::new(TestFacade::new()) as Arc<dyn Facade>);
            TestFacadeProvider { facades }
        }
    }

    #[async_trait(?Send)]
    impl FacadeProvider for TestFacadeProvider {
        fn get_facade(&self, name: &str) -> Option<Arc<dyn Facade>> {
            self.facades.get(name).map(Arc::clone)
        }

        fn get_facades(&self) -> Box<dyn Iterator<Item = &Arc<dyn Facade>> + '_> {
            Box::new(self.facades.values())
        }

        fn get_facade_names(&self) -> Box<dyn ExactSizeIterator<Item = &str> + '_> {
            Box::new(self.facades.keys().map(|string| string.as_str()))
        }
    }

    /// This test exercises the default FacadeProvider server implementation provided in the
    /// FacadeProvider trait using TestFacadeProvider and TestFacade. It creates server and client
    /// endpoints to a channel, passing each respectively to async blocks for the server and client
    /// functionality. These async blocks are joined and passed to a single-threaded executor. The
    /// server simply waits on its endpoint. The client does the following:
    /// 1. Gets and verifies initial state.
    /// 2. Sets a new state.
    /// 3. Gets and verifies a new state.
    /// 4. Cleans up state.
    /// 5. Gets and verifies the clean state.
    /// 6. Releases the client end so that the server end can exit.
    #[test]
    fn test_facade_provider() -> Result<(), Error> {
        let mut executor = fasync::Executor::new().expect("Failed to create an executor!");

        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<FacadeProviderMarker>()?;
        let server_fut = async {
            // Run the FacadeProvider server.
            let sl4f = TestFacadeProvider::new();
            sl4f.run_facade_provider(stream).await;
        };
        let client_fut = async {
            // Get the initial state.
            let vmo = zx::Vmo::create_with_opts(zx::VmoOptions::RESIZABLE, 0)?;
            write_json_to_vmo(&vmo, &json!(null))?;
            if let (Some(vmo), None) = proxy.execute("test_facade", "get", vmo).await? {
                assert_eq!(false, read_json_from_vmo(&vmo)?.as_bool().unwrap());
            } else {
                panic!("Failed to get initial state.");
            }

            // Set the new state.
            let vmo = zx::Vmo::create_with_opts(zx::VmoOptions::RESIZABLE, 0)?;
            write_json_to_vmo(&vmo, &json!(true))?;
            if let (None, None) = proxy.execute("test_facade", "set", vmo).await? {
            } else {
                panic!("Failed to set new state.");
            }

            // Get the new state.
            let vmo = zx::Vmo::create_with_opts(zx::VmoOptions::RESIZABLE, 0)?;
            write_json_to_vmo(&vmo, &json!(null))?;
            if let (Some(vmo), None) = proxy.execute("test_facade", "get", vmo).await? {
                assert_eq!(true, read_json_from_vmo(&vmo)?.as_bool().unwrap());
            } else {
                panic!("Failed to get new state.");
            }

            // Clean up the state.
            proxy.cleanup().await?;

            // Get the final state.
            let vmo = zx::Vmo::create_with_opts(zx::VmoOptions::RESIZABLE, 0)?;
            write_json_to_vmo(&vmo, &json!(null))?;
            if let (Some(vmo), None) = proxy.execute("test_facade", "get", vmo).await? {
                assert_eq!(false, read_json_from_vmo(&vmo)?.as_bool().unwrap());
            } else {
                panic!("Failed to get initial state.");
            }

            // Close the proxy.
            drop(proxy);
            Ok::<(), Error>(())
        };
        let combined_fut = async {
            let (_, res) = futures::join!(server_fut, client_fut);
            res.unwrap();
        };
        pin_mut!(combined_fut);

        assert_eq!(Poll::Ready(()), executor.run_until_stalled(&mut combined_fut));

        Ok(())
    }
}
