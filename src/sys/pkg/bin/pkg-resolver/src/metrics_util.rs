// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {cobalt_sw_delivery_registry as metrics, tuf::error::Error as TufError};

pub fn tuf_error_as_update_tuf_client_event_code(
    e: &TufError,
) -> metrics::UpdateTufClientMetricDimensionResult {
    match e {
        TufError::BadSignature => metrics::UpdateTufClientMetricDimensionResult::BadSignature,
        TufError::Encoding(_) => metrics::UpdateTufClientMetricDimensionResult::Encoding,
        TufError::ExpiredMetadata(_) => {
            metrics::UpdateTufClientMetricDimensionResult::ExpiredMetadata
        }
        TufError::IllegalArgument(_) => {
            metrics::UpdateTufClientMetricDimensionResult::IllegalArgument
        }
        TufError::MissingMetadata(_) => {
            metrics::UpdateTufClientMetricDimensionResult::MissingMetadata
        }
        TufError::NoSupportedHashAlgorithm => {
            metrics::UpdateTufClientMetricDimensionResult::NoSupportedHashAlgorithm
        }
        TufError::NotFound => metrics::CreateTufClientMetricDimensionResult::NotFound,
        TufError::Opaque(_) => metrics::UpdateTufClientMetricDimensionResult::Opaque,
        TufError::Programming(_) => metrics::UpdateTufClientMetricDimensionResult::Programming,
        TufError::TargetUnavailable => {
            metrics::UpdateTufClientMetricDimensionResult::TargetUnavailable
        }
        TufError::UnknownKeyType(_) => {
            metrics::UpdateTufClientMetricDimensionResult::UnknownKeyType
        }
        TufError::VerificationFailure(_) => {
            metrics::UpdateTufClientMetricDimensionResult::VerificationFailure
        }
        TufError::Http(_) => metrics::CreateTufClientMetricDimensionResult::Http,
        TufError::Hyper(_) => metrics::CreateTufClientMetricDimensionResult::Hyper,
        _ => metrics::CreateTufClientMetricDimensionResult::UnexpectedTufErrorVariant,
    }
}

pub fn tuf_error_as_create_tuf_client_event_code(
    e: &TufError,
) -> metrics::CreateTufClientMetricDimensionResult {
    match e {
        TufError::BadSignature => metrics::CreateTufClientMetricDimensionResult::BadSignature,
        TufError::Encoding(_) => metrics::CreateTufClientMetricDimensionResult::Encoding,
        TufError::ExpiredMetadata(_) => {
            metrics::CreateTufClientMetricDimensionResult::ExpiredMetadata
        }
        TufError::IllegalArgument(_) => {
            metrics::CreateTufClientMetricDimensionResult::IllegalArgument
        }
        TufError::MissingMetadata(_) => {
            metrics::CreateTufClientMetricDimensionResult::MissingMetadata
        }
        TufError::NoSupportedHashAlgorithm => {
            metrics::CreateTufClientMetricDimensionResult::NoSupportedHashAlgorithm
        }
        TufError::NotFound => metrics::CreateTufClientMetricDimensionResult::NotFound,
        TufError::Opaque(_) => metrics::CreateTufClientMetricDimensionResult::Opaque,
        TufError::Programming(_) => metrics::CreateTufClientMetricDimensionResult::Programming,
        TufError::TargetUnavailable => {
            metrics::CreateTufClientMetricDimensionResult::TargetUnavailable
        }
        TufError::UnknownKeyType(_) => {
            metrics::CreateTufClientMetricDimensionResult::UnknownKeyType
        }
        TufError::VerificationFailure(_) => {
            metrics::CreateTufClientMetricDimensionResult::VerificationFailure
        }
        TufError::Http(_) => metrics::CreateTufClientMetricDimensionResult::Http,
        TufError::Hyper(_) => metrics::CreateTufClientMetricDimensionResult::Hyper,
        _ => metrics::CreateTufClientMetricDimensionResult::UnexpectedTufErrorVariant,
    }
}
