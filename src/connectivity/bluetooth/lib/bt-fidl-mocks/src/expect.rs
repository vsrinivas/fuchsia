// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{err_msg, Error},
    fidl::endpoints::RequestStream,
    fuchsia_async::{DurationExt, TimeoutExt},
    fuchsia_zircon::Duration,
    futures::{TryStream, TryStreamExt},
};

/// Represents the status of an expectation over a FIDL request stream.
pub(crate) enum Status<T> {
    Pending,
    Satisfied(T),
}

/// Asynchronously applies handler `H` to each FIDL request of type `R` received from stream `S`
/// until the handler is satisfied or the provided timeout expires.
pub(crate) async fn expect_call<H, T, R, S>(
    stream: &mut S,
    timeout: Duration,
    mut handler: H,
) -> Result<T, Error>
where
    H: FnMut(R) -> Result<Status<T>, Error>,
    S: RequestStream + TryStream<Ok = R, Error = fidl::Error>,
{
    async {
        while let Some(msg) = stream.try_next().await? {
            match handler(msg)? {
                Status::Satisfied(t) => {
                    return Ok(t);
                }
                Status::Pending => (),
            }
        }
        Err(err_msg("Stream closed before expectation was satisifed"))
    }
    .on_timeout(timeout.after_now(), || Err(err_msg("timed out before expectation was satisfied")))
    .await
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_bluetooth::DeviceClass,
        fidl_fuchsia_bluetooth_sys::{AccessMarker, AccessRequest, AccessRequestStream},
        fuchsia_zircon::DurationNum,
    };

    // This is a mock handler that does the following for the purposes of the unit tests below:
    // - Return success if Access.SetLocalName is called with the given `expected_name`;
    // - Return an error if Access.SetLocalName is called with a name that is different from
    //   `expected_name`;
    // - Continue expecting a message if any other FIDL message is received.
    async fn expect_set_local_name(
        mut stream: AccessRequestStream,
        expected_name: String,
    ) -> Result<(), Error> {
        expect_call(&mut stream, 500.millis(), move |req| match req {
            AccessRequest::SetLocalName { name, control_handle: _ } => {
                if name == expected_name {
                    Ok(Status::Satisfied(()))
                } else {
                    Err(err_msg("received incorrect name"))
                }
            }
            _ => Ok(Status::Pending),
        })
        .await
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_satisfied() {
        let (proxy, stream) =
            create_proxy_and_stream::<AccessMarker>().expect("failed to create proxy");
        let name = "TEST".to_string();

        let _ = proxy.set_local_name(&name);
        let mock_result = expect_set_local_name(stream, name).await;
        assert!(mock_result.is_ok());
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_unsatisfied_due_to_mismatch() {
        let (proxy, stream) =
            create_proxy_and_stream::<AccessMarker>().expect("failed to create proxy");
        let expected_name = "TEST".to_string();
        let wrong_name = "💩".to_string();

        let _ = proxy.set_local_name(&wrong_name);
        let mock_result = expect_set_local_name(stream, expected_name).await;
        assert!(mock_result.is_err());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_timeout_without_any_message() {
        let (_proxy, stream) =
            create_proxy_and_stream::<AccessMarker>().expect("failed to create proxy");
        let expected_name = "TEST".to_string();
        let result = expect_set_local_name(stream, expected_name).await;
        assert!(result.is_err());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_timeout_after_unexpected_message() {
        let (proxy, stream) =
            create_proxy_and_stream::<AccessMarker>().expect("failed to create proxy");
        let expected_name = "TEST".to_string();

        let _ = proxy.set_device_class(&mut DeviceClass { value: 0 });
        let mock_result = expect_set_local_name(stream, expected_name).await;
        assert!(mock_result.is_err());
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_error_after_handle_closure() {
        let (proxy, stream) =
            create_proxy_and_stream::<AccessMarker>().expect("failed to create proxy");
        let expected_name = "TEST".to_string();

        drop(proxy);
        let result = expect_set_local_name(stream, expected_name).await;
        assert!(result.is_err());
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_satisfied_with_expected_message_after_unexpected_message() {
        let (proxy, stream) =
            create_proxy_and_stream::<AccessMarker>().expect("failed to create proxy");
        let expected_name = "TEST".to_string();

        let _ = proxy.set_device_class(&mut DeviceClass { value: 0 });
        let _ = proxy.set_local_name(&expected_name);
        let mock_result = expect_set_local_name(stream, expected_name.clone()).await;
        assert!(mock_result.is_ok());
    }
}
