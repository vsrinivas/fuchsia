// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error,
    cobalt_sw_delivery_registry::{
        CreateTufClientMetricDimensionResult, UpdateTufClientMetricDimensionResult,
    },
};

pub fn tuf_error_as_update_tuf_client_event_code(
    e: &error::TufOrDeadline,
) -> UpdateTufClientMetricDimensionResult {
    use {
        error::TufOrDeadline::*, tuf::error::Error::*,
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
        Tuf(Opaque(_)) => EventCodes::Opaque,
        Tuf(Programming(_)) => EventCodes::Programming,
        Tuf(TargetUnavailable) => EventCodes::TargetUnavailable,
        Tuf(UnknownKeyType(_)) => EventCodes::UnknownKeyType,
        Tuf(VerificationFailure(_)) => EventCodes::VerificationFailure,
        Tuf(Http(_)) => EventCodes::Http,
        Tuf(Hyper(_)) => EventCodes::Hyper,
        DeadlineExceeded => EventCodes::DeadlineExceeded,
        _ => EventCodes::UnexpectedTufErrorVariant,
    }
}

pub fn tuf_error_as_create_tuf_client_event_code(
    e: &error::TufOrDeadline,
) -> CreateTufClientMetricDimensionResult {
    use {
        error::TufOrDeadline::*, tuf::error::Error::*,
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
        Tuf(Opaque(_)) => EventCodes::Opaque,
        Tuf(Programming(_)) => EventCodes::Programming,
        Tuf(TargetUnavailable) => EventCodes::TargetUnavailable,
        Tuf(UnknownKeyType(_)) => EventCodes::UnknownKeyType,
        Tuf(VerificationFailure(_)) => EventCodes::VerificationFailure,
        Tuf(Http(_)) => EventCodes::Http,
        Tuf(Hyper(_)) => EventCodes::Hyper,
        DeadlineExceeded => EventCodes::DeadlineExceeded,
        _ => EventCodes::UnexpectedTufErrorVariant,
    }
}
