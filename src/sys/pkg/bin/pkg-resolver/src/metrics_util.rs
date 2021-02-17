// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error,
    cobalt_sw_delivery_registry::{
        CreateTufClientMetricDimensionResult, UpdateTufClientMetricDimensionResult,
    },
    hyper::StatusCode,
};

pub fn tuf_error_as_update_tuf_client_event_code(
    e: &error::TufOrTimeout,
) -> UpdateTufClientMetricDimensionResult {
    use {
        error::TufOrTimeout::*, tuf::error::Error::*,
        UpdateTufClientMetricDimensionResult as EventCodes,
    };
    match e {
        Tuf(BadSignature) => EventCodes::BadSignature,
        Tuf(Encoding(_)) => EventCodes::Encoding,
        Tuf(ExpiredMetadata(_)) => EventCodes::ExpiredMetadata,
        Tuf(IllegalArgument(_)) => EventCodes::IllegalArgument,
        Tuf(MissingMetadata(_)) => EventCodes::MissingMetadata,
        Tuf(NoSupportedHashAlgorithm) => EventCodes::NoSupportedHashAlgorithm,
        Tuf(NotFound) => EventCodes::NotFound,
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
        Tuf(Programming(_)) => EventCodes::Programming,
        Tuf(TargetUnavailable) => EventCodes::TargetUnavailable,
        Tuf(UnknownKeyType(_)) => EventCodes::UnknownKeyType,
        Tuf(VerificationFailure(_)) => EventCodes::VerificationFailure,
        Tuf(Http(_)) => EventCodes::Http,
        Tuf(Hyper(_)) => EventCodes::Hyper,
        Timeout => EventCodes::DeadlineExceeded,
        _ => EventCodes::UnexpectedTufErrorVariant,
    }
}

pub fn tuf_error_as_create_tuf_client_event_code(
    e: &error::TufOrTimeout,
) -> CreateTufClientMetricDimensionResult {
    use {
        error::TufOrTimeout::*, tuf::error::Error::*,
        CreateTufClientMetricDimensionResult as EventCodes,
    };
    match e {
        Tuf(BadSignature) => EventCodes::BadSignature,
        Tuf(Encoding(_)) => EventCodes::Encoding,
        Tuf(ExpiredMetadata(_)) => EventCodes::ExpiredMetadata,
        Tuf(IllegalArgument(_)) => EventCodes::IllegalArgument,
        Tuf(MissingMetadata(_)) => EventCodes::MissingMetadata,
        Tuf(NoSupportedHashAlgorithm) => EventCodes::NoSupportedHashAlgorithm,
        Tuf(NotFound) => EventCodes::NotFound,
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
        Tuf(Programming(_)) => EventCodes::Programming,
        Tuf(TargetUnavailable) => EventCodes::TargetUnavailable,
        Tuf(UnknownKeyType(_)) => EventCodes::UnknownKeyType,
        Tuf(VerificationFailure(_)) => EventCodes::VerificationFailure,
        Tuf(Http(_)) => EventCodes::Http,
        Tuf(Hyper(_)) => EventCodes::Hyper,
        Timeout => EventCodes::DeadlineExceeded,
        _ => EventCodes::UnexpectedTufErrorVariant,
    }
}
