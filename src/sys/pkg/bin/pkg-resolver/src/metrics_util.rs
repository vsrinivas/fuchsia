// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error,
    anyhow::{format_err, Context as _, Error},
    cobalt_sw_delivery_registry::{
        self as metrics, CreateTufClientMigratedMetricDimensionResult,
        UpdateTufClientMigratedMetricDimensionResult,
    },
    fidl_contrib::protocol_connector::ConnectedProtocol,
    fidl_fuchsia_metrics::{
        MetricEvent, MetricEventLoggerFactoryMarker, MetricEventLoggerProxy, ProjectSpec,
    },
    fuchsia_component::client::connect_to_protocol,
    futures::{future, FutureExt as _},
    hyper::StatusCode,
};

pub fn tuf_error_as_update_tuf_client_event_code(
    e: &error::TufOrTimeout,
) -> UpdateTufClientMigratedMetricDimensionResult {
    use {
        error::TufOrTimeout::*, tuf::error::Error::*,
        UpdateTufClientMigratedMetricDimensionResult as EventCodes,
    };
    match e {
        Tuf(BadSignature(_)) => EventCodes::BadSignature,
        Tuf(Encoding(_)) => EventCodes::Encoding,
        Tuf(ExpiredMetadata(_)) => EventCodes::ExpiredMetadata,
        Tuf(IllegalArgument(_)) => EventCodes::IllegalArgument,
        Tuf(NoSupportedHashAlgorithm) => EventCodes::NoSupportedHashAlgorithm,
        Tuf(MetadataNotFound { .. }) => EventCodes::MissingMetadata,
        Tuf(BadHttpStatus { code, .. }) => match *code {
            StatusCode::BAD_REQUEST => EventCodes::HttpBadRequest,
            StatusCode::UNAUTHORIZED => EventCodes::HttpUnauthorized,
            StatusCode::FORBIDDEN => EventCodes::HttpForbidden,
            StatusCode::NOT_FOUND => EventCodes::HttpNotFound,
            StatusCode::METHOD_NOT_ALLOWED => EventCodes::HttpMethodNotAllowed,
            StatusCode::REQUEST_TIMEOUT => EventCodes::HttpRequestTimeout,
            StatusCode::PRECONDITION_FAILED => EventCodes::HttpPreconditionFailed,
            StatusCode::RANGE_NOT_SATISFIABLE => EventCodes::HttpRangeNotSatisfiable,
            StatusCode::TOO_MANY_REQUESTS => EventCodes::HttpTooManyRequests,
            StatusCode::INTERNAL_SERVER_ERROR => EventCodes::HttpInternalServerError,
            StatusCode::BAD_GATEWAY => EventCodes::HttpBadGateway,
            StatusCode::SERVICE_UNAVAILABLE => EventCodes::HttpServiceUnavailable,
            StatusCode::GATEWAY_TIMEOUT => EventCodes::HttpGatewayTimeout,
            _ => match code.as_u16() {
                100..=199 => EventCodes::Http1xx,
                200..=299 => EventCodes::Http2xx,
                300..=399 => EventCodes::Http3xx,
                400..=499 => EventCodes::Http4xx,
                500..=599 => EventCodes::Http5xx,
                _ => EventCodes::Opaque,
            },
        },
        Tuf(TargetNotFound { .. }) => EventCodes::TargetUnavailable,
        Tuf(UnknownKeyType(_)) => EventCodes::UnknownKeyType,
        Tuf(Http { .. }) => EventCodes::Http,
        Tuf(Hyper { .. }) => EventCodes::Hyper,
        Timeout => EventCodes::DeadlineExceeded,
        _ => EventCodes::UnexpectedTufErrorVariant,
    }
}

pub fn tuf_error_as_create_tuf_client_event_code(
    e: &error::TufOrTimeout,
) -> CreateTufClientMigratedMetricDimensionResult {
    use {
        error::TufOrTimeout::*, tuf::error::Error::*,
        CreateTufClientMigratedMetricDimensionResult as EventCodes,
    };
    match e {
        Tuf(BadSignature(_)) => EventCodes::BadSignature,
        Tuf(Encoding(_)) => EventCodes::Encoding,
        Tuf(ExpiredMetadata(_)) => EventCodes::ExpiredMetadata,
        Tuf(IllegalArgument(_)) => EventCodes::IllegalArgument,
        Tuf(MetadataNotFound { .. }) => EventCodes::MissingMetadata,
        Tuf(NoSupportedHashAlgorithm) => EventCodes::NoSupportedHashAlgorithm,
        Tuf(BadHttpStatus { code, .. }) => match *code {
            StatusCode::BAD_REQUEST => EventCodes::HttpBadRequest,
            StatusCode::UNAUTHORIZED => EventCodes::HttpUnauthorized,
            StatusCode::FORBIDDEN => EventCodes::HttpForbidden,
            StatusCode::NOT_FOUND => EventCodes::HttpNotFound,
            StatusCode::METHOD_NOT_ALLOWED => EventCodes::HttpMethodNotAllowed,
            StatusCode::REQUEST_TIMEOUT => EventCodes::HttpRequestTimeout,
            StatusCode::PRECONDITION_FAILED => EventCodes::HttpPreconditionFailed,
            StatusCode::RANGE_NOT_SATISFIABLE => EventCodes::HttpRangeNotSatisfiable,
            StatusCode::TOO_MANY_REQUESTS => EventCodes::HttpTooManyRequests,
            StatusCode::INTERNAL_SERVER_ERROR => EventCodes::HttpInternalServerError,
            StatusCode::BAD_GATEWAY => EventCodes::HttpBadGateway,
            StatusCode::SERVICE_UNAVAILABLE => EventCodes::HttpServiceUnavailable,
            StatusCode::GATEWAY_TIMEOUT => EventCodes::HttpGatewayTimeout,
            _ => match code.as_u16() {
                100..=199 => EventCodes::Http1xx,
                200..=299 => EventCodes::Http2xx,
                300..=399 => EventCodes::Http3xx,
                400..=499 => EventCodes::Http4xx,
                500..=599 => EventCodes::Http5xx,
                _ => EventCodes::Opaque,
            },
        },
        Tuf(TargetNotFound { .. }) => EventCodes::TargetUnavailable,
        Tuf(UnknownKeyType(_)) => EventCodes::UnknownKeyType,
        Tuf(Http { .. }) => EventCodes::Http,
        Tuf(Hyper { .. }) => EventCodes::Hyper,
        Timeout => EventCodes::DeadlineExceeded,
        _ => EventCodes::UnexpectedTufErrorVariant,
    }
}

pub struct CobaltConnectedService;
impl ConnectedProtocol for CobaltConnectedService {
    type Protocol = MetricEventLoggerProxy;
    type ConnectError = Error;
    type Message = MetricEvent;
    type SendError = Error;

    fn get_protocol(&mut self) -> future::BoxFuture<'_, Result<MetricEventLoggerProxy, Error>> {
        async {
            let (logger_proxy, server_end) =
                fidl::endpoints::create_proxy().context("failed to create proxy endpoints")?;
            let metric_event_logger_factory =
                connect_to_protocol::<MetricEventLoggerFactoryMarker>()
                    .context("Failed to connect to fuchsia::metrics::MetricEventLoggerFactory")?;

            metric_event_logger_factory
                .create_metric_event_logger(
                    ProjectSpec { project_id: Some(metrics::PROJECT_ID), ..ProjectSpec::EMPTY },
                    server_end,
                )
                .await?
                .map_err(|e| format_err!("Connection to MetricEventLogger refused {e:?}"))?;
            Ok(logger_proxy)
        }
        .boxed()
    }

    fn send_message<'a>(
        &'a mut self,
        protocol: &'a MetricEventLoggerProxy,
        mut msg: MetricEvent,
    ) -> future::BoxFuture<'a, Result<(), Error>> {
        async move {
            let fut = protocol.log_metric_events(&mut std::iter::once(&mut msg));
            fut.await?.map_err(|e| format_err!("Failed to log metric {e:?}"))?;
            Ok(())
        }
        .boxed()
    }
}
