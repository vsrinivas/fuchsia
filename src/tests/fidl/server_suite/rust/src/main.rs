// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl::endpoints::{ControlHandle, ServerEnd, UnknownMethodType},
    fidl::{AsHandleRef, Event, Status},
    fidl_fidl_serversuite::{
        AjarTargetMarker, AjarTargetRequest, AnyTarget, ClosedTargetMarker, ClosedTargetRequest,
        ClosedTargetTwoWayResultRequest, ClosedTargetTwoWayTablePayloadResponse,
        ClosedTargetTwoWayUnionPayloadRequest, ClosedTargetTwoWayUnionPayloadResponse, Elements,
        Empty, EventType, LargeMessageTargetMarker, LargeMessageTargetRequest,
        OpenTargetFlexibleTwoWayErrRequest, OpenTargetFlexibleTwoWayFieldsErrRequest,
        OpenTargetMarker, OpenTargetRequest, OpenTargetStrictTwoWayErrRequest,
        OpenTargetStrictTwoWayFieldsErrRequest, ReporterProxy, RunnerRequest, RunnerRequestStream,
        Test, UnknownMethodType as DynsuiteUnknownMethodType,
    },
    fuchsia_component::server::ServiceFs,
    futures::prelude::*,
};

const EXPECT_STRICT_ONE_WAY: &'static str = "failed to report strict one way request";
const EXPECT_REPLY_FAILED: &'static str = "failed to send reply";

async fn run_closed_target_server(
    server_end: ServerEnd<ClosedTargetMarker>,
    reporter_proxy: &ReporterProxy,
) -> Result<(), Error> {
    server_end
        .into_stream()?
        .map(|result| result.context("failed request"))
        .try_for_each(|request| async move {
            match request {
                ClosedTargetRequest::OneWayNoPayload { control_handle: _ } => {
                    println!("OneWayNoPayload");
                    reporter_proxy
                        .received_one_way_no_payload()
                        .expect("calling received_one_way_no_payload failed");
                }
                ClosedTargetRequest::TwoWayNoPayload { responder } => {
                    println!("TwoWayNoPayload");
                    responder.send().expect("failed to send two way payload response");
                }
                ClosedTargetRequest::TwoWayStructPayload { v, responder } => {
                    println!("TwoWayStructPayload");
                    responder.send(v).expect("failed to send two way payload response");
                }
                ClosedTargetRequest::TwoWayTablePayload { payload, responder } => {
                    println!("TwoWayTablePayload");
                    responder
                        .send(ClosedTargetTwoWayTablePayloadResponse {
                            v: payload.v,
                            ..ClosedTargetTwoWayTablePayloadResponse::EMPTY
                        })
                        .expect("failed to send two way payload response");
                }
                ClosedTargetRequest::TwoWayUnionPayload { payload, responder } => {
                    println!("TwoWayUnionPayload");
                    let v = match payload {
                        ClosedTargetTwoWayUnionPayloadRequest::V(v) => v,
                        _ => {
                            panic!("unexpected union value");
                        }
                    };
                    responder
                        .send(&mut ClosedTargetTwoWayUnionPayloadResponse::V(v))
                        .expect("failed to send two way payload response");
                }
                ClosedTargetRequest::TwoWayResult { payload, responder } => {
                    println!("TwoWayResult");
                    match payload {
                        ClosedTargetTwoWayResultRequest::Payload(value) => {
                            responder.send(&mut Ok(value))
                        }
                        ClosedTargetTwoWayResultRequest::Error(value) => {
                            responder.send(&mut Err(value))
                        }
                    }
                    .expect("failed to send two way payload response");
                }
                ClosedTargetRequest::GetHandleRights { handle, responder } => {
                    let basic_info = handle
                        .as_handle_ref()
                        .basic_info()
                        .expect("failed to get basic handle info");
                    let rights = fidl_zx::Rights::from_bits(basic_info.rights.bits())
                        .expect("bits should be valid");
                    responder.send(rights).expect("failed to send response");
                }
                ClosedTargetRequest::GetSignalableEventRights { handle, responder } => {
                    let basic_info = handle
                        .as_handle_ref()
                        .basic_info()
                        .expect("failed to get basic handle info");
                    let rights = fidl_zx::Rights::from_bits(basic_info.rights.bits())
                        .expect("bits should be valid");
                    responder.send(rights).expect("failed to send response");
                }
                ClosedTargetRequest::EchoAsTransferableSignalableEvent { handle, responder } => {
                    responder.send(fidl::Event::from(handle)).expect("failed to send response");
                }
                ClosedTargetRequest::CloseWithEpitaph { epitaph_status, control_handle } => {
                    control_handle.shutdown_with_epitaph(Status::from_raw(epitaph_status));
                }
                ClosedTargetRequest::ByteVectorSize { vec, responder } => {
                    responder.send(vec.len().try_into().unwrap()).expect("failed to send response");
                }
                ClosedTargetRequest::HandleVectorSize { vec, responder } => {
                    responder.send(vec.len().try_into().unwrap()).expect("failed to send response");
                }
                ClosedTargetRequest::CreateNByteVector { n, responder } => {
                    let bytes: Vec<u8> = vec![0; n.try_into().unwrap()];
                    responder.send(&bytes).expect("failed to send response");
                }
                ClosedTargetRequest::CreateNHandleVector { n, responder } => {
                    let mut handles: Vec<fidl::Event> = Vec::new();
                    for _ in 0..n {
                        handles.push(Event::create().unwrap());
                    }
                    responder.send(&mut handles.into_iter()).expect("failed to send response");
                }
            }
            Ok(())
        })
        .await
}

async fn run_ajar_target_server(
    server_end: ServerEnd<AjarTargetMarker>,
    reporter_proxy: &ReporterProxy,
) -> Result<(), Error> {
    server_end
        .into_stream()?
        .map(|result| result.context("failed request"))
        .try_for_each(|request| async move {
            match request {
                AjarTargetRequest::_UnknownMethod { ordinal, control_handle: _ } => {
                    reporter_proxy
                        .received_unknown_method(ordinal, DynsuiteUnknownMethodType::OneWay)
                        .expect("failed to report unknown method call");
                }
            }
            Ok(())
        })
        .await
}

async fn run_open_target_server(
    server_end: ServerEnd<OpenTargetMarker>,
    reporter_proxy: &ReporterProxy,
) -> Result<(), Error> {
    server_end
        .into_stream()?
        .map(|result| result.context("failed request"))
        .try_for_each(|request| async move {
            match request {
                OpenTargetRequest::SendEvent { event_type, control_handle } => match event_type {
                    EventType::Strict => {
                        control_handle.send_strict_event().expect("failed to send event")
                    }
                    EventType::Flexible => {
                        control_handle.send_flexible_event().expect("failed to send event")
                    }
                },
                OpenTargetRequest::StrictOneWay { .. } => {
                    reporter_proxy.received_strict_one_way().expect(EXPECT_STRICT_ONE_WAY);
                }
                OpenTargetRequest::FlexibleOneWay { .. } => {
                    reporter_proxy
                        .received_flexible_one_way()
                        .expect("failed to report flexible one way request");
                }
                OpenTargetRequest::StrictTwoWay { responder } => {
                    responder.send().expect(EXPECT_REPLY_FAILED);
                }
                OpenTargetRequest::StrictTwoWayFields { reply_with, responder } => {
                    responder.send(reply_with).expect(EXPECT_REPLY_FAILED);
                }
                OpenTargetRequest::StrictTwoWayErr { payload, responder } => match payload {
                    OpenTargetStrictTwoWayErrRequest::ReplySuccess(Empty) => {
                        responder.send(&mut Ok(())).expect(EXPECT_REPLY_FAILED);
                    }
                    OpenTargetStrictTwoWayErrRequest::ReplyError(reply_error) => {
                        responder.send(&mut Err(reply_error)).expect(EXPECT_REPLY_FAILED);
                    }
                },
                OpenTargetRequest::StrictTwoWayFieldsErr { payload, responder } => match payload {
                    OpenTargetStrictTwoWayFieldsErrRequest::ReplySuccess(reply_success) => {
                        responder.send(&mut Ok(reply_success)).expect(EXPECT_REPLY_FAILED);
                    }
                    OpenTargetStrictTwoWayFieldsErrRequest::ReplyError(reply_error) => {
                        responder.send(&mut Err(reply_error)).expect(EXPECT_REPLY_FAILED);
                    }
                },
                OpenTargetRequest::FlexibleTwoWay { responder } => {
                    responder.send().expect(EXPECT_REPLY_FAILED);
                }
                OpenTargetRequest::FlexibleTwoWayFields { reply_with, responder } => {
                    responder.send(reply_with).expect(EXPECT_REPLY_FAILED);
                }
                OpenTargetRequest::FlexibleTwoWayErr { payload, responder } => match payload {
                    OpenTargetFlexibleTwoWayErrRequest::ReplySuccess(Empty) => {
                        responder.send(&mut Ok(())).expect(EXPECT_REPLY_FAILED);
                    }
                    OpenTargetFlexibleTwoWayErrRequest::ReplyError(reply_error) => {
                        responder.send(&mut Err(reply_error)).expect(EXPECT_REPLY_FAILED);
                    }
                },
                OpenTargetRequest::FlexibleTwoWayFieldsErr { payload, responder } => {
                    match payload {
                        OpenTargetFlexibleTwoWayFieldsErrRequest::ReplySuccess(reply_success) => {
                            responder.send(&mut Ok(reply_success)).expect(EXPECT_REPLY_FAILED);
                        }
                        OpenTargetFlexibleTwoWayFieldsErrRequest::ReplyError(reply_error) => {
                            responder.send(&mut Err(reply_error)).expect(EXPECT_REPLY_FAILED);
                        }
                    }
                }
                OpenTargetRequest::_UnknownMethod {
                    ordinal,
                    unknown_method_type,
                    control_handle: _,
                } => {
                    let unknown_method_type = match unknown_method_type {
                        UnknownMethodType::OneWay => DynsuiteUnknownMethodType::OneWay,
                        UnknownMethodType::TwoWay => DynsuiteUnknownMethodType::TwoWay,
                    };
                    reporter_proxy
                        .received_unknown_method(ordinal, unknown_method_type)
                        .expect("failed to report unknown method call");
                }
            }
            Ok(())
        })
        .await
}

async fn run_large_message_target_server(
    server_end: ServerEnd<LargeMessageTargetMarker>,
    reporter_proxy: &ReporterProxy,
) -> Result<(), Error> {
    server_end
        .into_stream()?
        .map(|result| result.context("failed request"))
        .try_for_each(|request| async move {
            match request {
                LargeMessageTargetRequest::DecodeBoundedKnownToBeSmall {
                    bytes: _,
                    control_handle: _,
                } => {
                    reporter_proxy.received_strict_one_way().expect(EXPECT_STRICT_ONE_WAY);
                }
                LargeMessageTargetRequest::DecodeBoundedMaybeLarge {
                    bytes: _,
                    control_handle: _,
                } => {
                    reporter_proxy.received_strict_one_way().expect(EXPECT_STRICT_ONE_WAY);
                }
                LargeMessageTargetRequest::DecodeSemiBoundedBelievedToBeSmall {
                    payload: _,
                    control_handle: _,
                } => {
                    reporter_proxy.received_strict_one_way().expect(EXPECT_STRICT_ONE_WAY);
                }
                LargeMessageTargetRequest::DecodeSemiBoundedMaybeLarge {
                    payload: _,
                    control_handle: _,
                } => {
                    reporter_proxy.received_strict_one_way().expect(EXPECT_STRICT_ONE_WAY);
                }
                LargeMessageTargetRequest::DecodeUnboundedMaybeLargeValue {
                    bytes: _,
                    control_handle: _,
                } => {
                    reporter_proxy.received_strict_one_way().expect(EXPECT_STRICT_ONE_WAY);
                }
                LargeMessageTargetRequest::DecodeUnboundedMaybeLargeResource {
                    elements: _,
                    control_handle: _,
                } => {
                    reporter_proxy.received_strict_one_way().expect(EXPECT_STRICT_ONE_WAY);
                }
                LargeMessageTargetRequest::EncodeBoundedKnownToBeSmall { bytes, responder } => {
                    responder.send(&bytes).expect(EXPECT_REPLY_FAILED);
                }
                LargeMessageTargetRequest::EncodeBoundedMaybeLarge { bytes, responder } => {
                    responder.send(&bytes).expect(EXPECT_REPLY_FAILED);
                }
                LargeMessageTargetRequest::EncodeSemiBoundedBelievedToBeSmall {
                    mut payload,
                    responder,
                } => {
                    responder.send(&mut payload).expect(EXPECT_REPLY_FAILED);
                }
                LargeMessageTargetRequest::EncodeSemiBoundedMaybeLarge {
                    mut payload,
                    responder,
                } => {
                    responder.send(&mut payload).expect(EXPECT_REPLY_FAILED);
                }
                LargeMessageTargetRequest::EncodeUnboundedMaybeLargeValue { bytes, responder } => {
                    responder.send(&bytes).expect(EXPECT_REPLY_FAILED);
                }
                LargeMessageTargetRequest::EncodeUnboundedMaybeLargeResource {
                    populate_unset_handles: _,
                    mut data,
                    responder,
                } => {
                    let mut elements = TryInto::<[&mut Elements; 64]>::try_into(
                        data.elements.iter_mut().collect::<Vec<&mut Elements>>(),
                    )
                    .unwrap();
                    responder.send(&mut elements).expect(EXPECT_REPLY_FAILED);
                }
                LargeMessageTargetRequest::_UnknownMethod {
                    ordinal,
                    unknown_method_type,
                    control_handle: _,
                } => {
                    let unknown_method_type = match unknown_method_type {
                        UnknownMethodType::OneWay => DynsuiteUnknownMethodType::OneWay,
                        UnknownMethodType::TwoWay => DynsuiteUnknownMethodType::TwoWay,
                    };
                    reporter_proxy
                        .received_unknown_method(ordinal, unknown_method_type)
                        .expect("failed to report unknown method call");
                }
            }
            Ok(())
        })
        .await
}

async fn run_runner_server(stream: RunnerRequestStream) -> Result<(), Error> {
    stream
        .map(|result| result.context("failed request"))
        .try_for_each(|request| async move {
            match request {
                RunnerRequest::IsTestEnabled { test, responder } => {
                    let enabled = match test {
                        Test::IgnoreDisabled => {
                            // This case will forever be false, as it is intended to validate the
                            // "test disabling" functionality of the runner itself.
                            false
                        }
                        Test::OneWayWithNonZeroTxid
                        | Test::TwoWayNoPayloadWithZeroTxid
                        | Test::ServerSendsTooFewRights
                        | Test::ResponseExceedsByteLimit
                        | Test::ResponseExceedsHandleLimit => false,
                        Test::V1TwoWayNoPayload | Test::V1TwoWayStructPayload => {
                            // TODO(fxbug.dev/99738): Rust bindings should reject V1 wire format.
                            false
                        }
                        Test::GoodDecodeBoundedMaybeLargeMessage
                        | Test::GoodDecodeSemiBoundedUnknowableLargeMessage
                        | Test::GoodDecodeSemiBoundedMaybeLargeMessage
                        | Test::GoodDecodeUnboundedLargeMessage
                        | Test::GoodDecode63HandleLargeMessage
                        | Test::GoodDecodeUnknownLargeMessage
                        | Test::BadDecodeByteOverflowFlagSetOnSmallMessage
                        | Test::BadDecodeByteOverflowFlagUnsetOnLargeMessage
                        | Test::BadDecodeLargeMessageInfoOmitted
                        | Test::BadDecodeLargeMessageInfoTooSmall
                        | Test::BadDecodeLargeMessageInfoTooLarge
                        | Test::BadDecodeLargeMessageInfoTopHalfUnzeroed
                        | Test::BadDecodeLargeMessageInfoByteCountIsZero
                        | Test::BadDecodeLargeMessageInfoByteCountTooSmall
                        | Test::BadDecodeLargeMessageInfoByteCountNotEqualToBound
                        | Test::BadDecodeNoHandles
                        | Test::BadDecodeTooFewHandles
                        | Test::BadDecode64HandleLargeMessage
                        | Test::BadDecodeLastHandleNotVmo
                        | Test::BadDecodeLastHandleInsufficientRights
                        | Test::BadDecodeVmoTooSmall
                        | Test::BadDecodeVmoTooLarge
                        | Test::GoodEncodeBoundedMaybeLargeMessage
                        | Test::GoodEncodeSemiBoundedMaybeLargeMessage
                        | Test::GoodEncodeUnboundedLargeMessage
                        | Test::GoodEncode63HandleLargeMessage
                        | Test::BadEncode64HandleLargeMessage => {
                            // TODO(fxbug.dev/114259): Implement large messages for Rust.
                            false
                        }
                        _ => true,
                    };
                    responder.send(enabled)?;
                }
                RunnerRequest::Start { reporter, target, responder } => {
                    println!("Runner.Start() called");
                    let reporter_proxy: &ReporterProxy = &reporter.into_proxy()?;
                    match target {
                        AnyTarget::ClosedTarget(server_end) => {
                            responder.send().expect("sending response failed");
                            run_closed_target_server(server_end, reporter_proxy)
                                .await
                                .unwrap_or_else(|e| {
                                    println!("closed target server failed {:?}", e)
                                });
                        }
                        AnyTarget::AjarTarget(server_end) => {
                            responder.send().expect("sending response failed");
                            run_ajar_target_server(server_end, reporter_proxy)
                                .await
                                .unwrap_or_else(|e| println!("ajar target server failed {:?}", e));
                        }
                        AnyTarget::OpenTarget(server_end) => {
                            responder.send().expect("sending response failed");
                            run_open_target_server(server_end, reporter_proxy)
                                .await
                                .unwrap_or_else(|e| println!("open target server failed {:?}", e));
                        }
                        AnyTarget::LargeMessageTarget(server_end) => {
                            responder.send().expect("sending response failed");
                            run_large_message_target_server(server_end, reporter_proxy)
                                .await
                                .unwrap_or_else(|e| {
                                    println!("large message target server failed {:?}", e)
                                });
                        }
                    }
                }
                RunnerRequest::CheckAlive { responder } => {
                    responder.send().expect("sending response failed");
                }
            }
            Ok(())
        })
        .await
}

enum IncomingService {
    Runner(RunnerRequestStream),
}

#[fuchsia::main]
async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(IncomingService::Runner);
    fs.take_and_serve_directory_handle().expect("serving directory failed");

    println!("Listening for incoming connections...");
    const MAX_CONCURRENT: usize = 10_000;
    fs.for_each_concurrent(MAX_CONCURRENT, |IncomingService::Runner(stream)| {
        run_runner_server(stream).unwrap_or_else(|e| panic!("runner server failed {:?}", e))
    })
    .await;

    Ok(())
}
