// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fidl_processor::processor::{BaseFidlProcessor, ProcessingUnit, RequestResultCreator};
use crate::message::base::MessengerType;
use crate::message::MessageHubUtil;
use crate::service;
use crate::ExitSender;
use anyhow::format_err;
use fidl::endpoints::{ProtocolMarker, Request};
use fidl_fuchsia_settings::{
    Error, PrivacyMarker, PrivacyProxy, PrivacyRequest, PrivacySetResult, PrivacySettings,
};
use fuchsia_async::{Task, TestExecutor};
use futures::task::Poll;
use futures::{pin_mut, FutureExt};
use matches::assert_matches;

type RequestCallback<P> = Box<dyn Fn(Request<P>, ExitSender) -> RequestResultCreator<'static, P>>;

/// A super simple processing unit that just calls its `RequestCallback` immediately when `process`
/// is called.
struct TestProcessingUnit<P = PrivacyMarker>
where
    P: ProtocolMarker,
{
    callback: RequestCallback<P>,
}

impl<P> TestProcessingUnit<P>
where
    P: ProtocolMarker,
{
    fn new(callback: RequestCallback<P>) -> Self {
        Self { callback }
    }
}

impl<P> ProcessingUnit<P> for TestProcessingUnit<P>
where
    P: ProtocolMarker,
{
    fn process(
        &self,
        _service_messenger: service::message::Messenger,
        request: Request<P>,
        exit_tx: ExitSender,
    ) -> RequestResultCreator<'static, P> {
        (self.callback)(request, exit_tx)
    }
}

/// Creates and starts a `BaseFidlProcessor` that includes the given processing units. Returns a
/// proxy to make FIDL calls on.
async fn create_processor(
    processing_units: Vec<Box<dyn ProcessingUnit<PrivacyMarker>>>,
) -> PrivacyProxy {
    let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<PrivacyMarker>().unwrap();

    let service_delegate = service::MessageHub::create_hub();
    let (service_messenger, _) =
        service_delegate.create(MessengerType::Unbound).await.expect("should create messenger");

    let fidl_processor = BaseFidlProcessor::<PrivacyMarker>::with_processing_units(
        stream,
        service_messenger,
        processing_units,
    );

    // Start processing.
    Task::local(async move {
        fidl_processor.process().await;
    })
    .detach();

    proxy
}

fn create_processing_unit<T, F>(callback: T) -> Box<TestProcessingUnit<PrivacyMarker>>
where
    T: Fn(PrivacyRequest, ExitSender) -> F + 'static,
    F: futures::Future<Output = Result<Option<PrivacyRequest>, anyhow::Error>> + 'static,
{
    Box::new(TestProcessingUnit::<PrivacyMarker>::new(Box::new(
        move |request: PrivacyRequest, exit_sender| -> RequestResultCreator<'_, PrivacyMarker> {
            callback(request, exit_sender).boxed_local()
        },
    )))
}

// Tests the happy path where the FIDL processing unit consumes the requests and responds to the
// client.
#[fuchsia_async::run_until_stalled(test)]
async fn test_processing_unit_consumes_request() {
    let mut expected_result: PrivacySetResult = Ok(());

    // Processing unit sends a response and returns a result indicating that it consumed the
    // request.
    let processing_unit = create_processing_unit(move |request: PrivacyRequest, _| async move {
        match request {
            PrivacyRequest::Set { responder, .. } => {
                responder.send(&mut expected_result).unwrap();
            }
            _ => panic!("unexpected request"),
        }
        Ok(None)
    });

    let proxy = create_processor(vec![processing_unit]).await;

    let result = proxy
        .set(PrivacySettings { user_data_sharing_consent: None, ..PrivacySettings::EMPTY })
        .await
        .expect("set request failed");
    assert_eq!(result, expected_result)
}

// Tests that when one FIDL processing unit consumes a request, the next one in line won't be
// invoked.
#[fuchsia_async::run_until_stalled(test)]
async fn test_multiple_processing_unit_consumes_request() {
    let mut expected_result: PrivacySetResult = Err(Error::Failed);

    // Sends a response and consumes the request.
    let processing_unit = create_processing_unit(move |request: PrivacyRequest, _| async move {
        match request {
            PrivacyRequest::Set { responder, .. } => {
                responder.send(&mut expected_result).unwrap();
            }
            _ => panic!("unexpected request"),
        }
        Ok(None)
    });

    let processing_unit_panics =
        create_processing_unit(
            move |_, _| async move { panic!("unexpected call to processing unit") },
        );

    let proxy = create_processor(vec![processing_unit, processing_unit_panics]).await;

    let result = proxy
        .set(PrivacySettings { user_data_sharing_consent: None, ..PrivacySettings::EMPTY })
        .await
        .expect("set request failed");
    assert_eq!(result, expected_result)
}

// Tests that when one FIDL processing unit doesn't process the request, that the next one in line
// will.
#[fuchsia_async::run_until_stalled(test)]
async fn test_processing_unit_passes_request() {
    let mut expected_result: PrivacySetResult = Err(Error::Failed);

    // Returns the request as-is to indicate that this processing unit didn't process it.
    let processing_unit_pass =
        create_processing_unit(move |request: PrivacyRequest, _| async move { Ok(Some(request)) });

    let processing_unit_consume =
        create_processing_unit(move |request: PrivacyRequest, _| async move {
            match request {
                PrivacyRequest::Set { responder, .. } => {
                    responder.send(&mut expected_result).unwrap();
                }
                _ => panic!("unexpected request"),
            }
            Ok(None)
        });

    let proxy = create_processor(vec![processing_unit_pass, processing_unit_consume]).await;

    let result = proxy
        .set(PrivacySettings { user_data_sharing_consent: None, ..PrivacySettings::EMPTY })
        .await
        .expect("set request failed");
    assert_eq!(result, expected_result)
}

// Tests that when one FIDL processing unit returns an error, the next one in line is not invoked.
#[fuchsia_async::run_until_stalled(test)]
async fn test_error_ends_processing() {
    let mut expected_result: PrivacySetResult = Err(Error::Failed);

    // Sends a response, but also returns an error.
    let processing_unit_expected =
        create_processing_unit(move |request: PrivacyRequest, _| async move {
            match request {
                PrivacyRequest::Set { responder, .. } => {
                    responder.send(&mut expected_result).unwrap();
                }
                _ => panic!("unexpected request"),
            }
            Err(format_err!("failed to process"))
        });

    let processing_unit_panics =
        create_processing_unit(
            move |_, _| async move { panic!("unexpected call to processing unit") },
        );

    let proxy = create_processor(vec![processing_unit_expected, processing_unit_panics]).await;

    let result = proxy
        .set(PrivacySettings { user_data_sharing_consent: None, ..PrivacySettings::EMPTY })
        .await
        .expect("set request failed");
    assert_eq!(result, expected_result)
}

// Tests that processing stops after one loop if a FIDL processing unit invokes the exit sender.
#[test]
fn test_exit_sender_ends_processing() {
    // We want to test that execution stalls since the fidl processor exited, so we use our own
    // executor.
    let mut executor = TestExecutor::new_with_fake_time().expect("Failed to create executor");

    let mut expected_result: PrivacySetResult = Err(Error::Failed);

    // Sends an exit, but returns the request as-is so that processing continues for the current
    // iteration.
    let processing_unit_exits =
        create_processing_unit(move |request: PrivacyRequest, exit_sender| async move {
            exit_sender.unbounded_send(()).expect("exit failed to send");
            Ok(Some(request))
        });

    // Sends a response and consumes the request.
    let processing_unit_responds =
        create_processing_unit(move |request: PrivacyRequest, _| async move {
            match request {
                PrivacyRequest::Set { responder, .. } => {
                    responder.send(&mut expected_result).unwrap();
                }
                _ => panic!("unexpected request"),
            }
            Ok(None)
        });

    let processor_future = create_processor(vec![processing_unit_exits, processing_unit_responds]);
    pin_mut!(processor_future);
    let proxy = match executor.run_until_stalled(&mut processor_future) {
        Poll::Ready(proxy) => proxy,
        _ => panic!("Failed to create processor"),
    };

    // First set finishes successfully.
    let set_future =
        proxy.set(PrivacySettings { user_data_sharing_consent: None, ..PrivacySettings::EMPTY });
    pin_mut!(set_future);
    assert_matches!(executor.run_until_stalled(&mut set_future), Poll::Ready(Result::Ok(_)));

    // Second set finishes with an error since the processing loop ended.
    let set_future =
        proxy.set(PrivacySettings { user_data_sharing_consent: None, ..PrivacySettings::EMPTY });
    pin_mut!(set_future);
    assert_matches!(
        executor.run_until_stalled(&mut set_future),
        Poll::Ready(Result::Err(fidl::Error::ClientChannelClosed { .. }))
    );
}
