// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::handler::base::{Error, Response};

#[derive(Debug)]
/// Response type to a request to a setting handler. Used for accumulating
/// response type counts for inspect. This should be updated to have all
/// the errors in [handler::base::Error].
pub(crate) enum ResponseType {
    OkSome,
    OkNone,
    UnimplementedRequest,
    StorageFailure,
    InitFailure,
    RestoreFailure,
    InvalidArgument,
    IncompatibleArguments,
    ExternalFailure,
    UnhandledType,
    DeliveryError,
    UnexpectedError,
    UndeliverableError,
    UnsupportedError,
    CommunicationError,
    IrrecoverableError,
    TimeoutError,
}

impl From<Error> for ResponseType {
    fn from(error: Error) -> Self {
        match error {
            Error::UnimplementedRequest(_setting_type, _request) => {
                ResponseType::UnimplementedRequest
            }
            Error::StorageFailure(_setting_type) => ResponseType::StorageFailure,
            Error::InitFailure(_cause) => ResponseType::InitFailure,
            Error::RestoreFailure(_cause) => ResponseType::RestoreFailure,
            Error::InvalidArgument(_setting_type, _argument, _value) => {
                ResponseType::InvalidArgument
            }
            Error::IncompatibleArguments { .. } => ResponseType::IncompatibleArguments,
            Error::ExternalFailure(_setting_type, _dependency, _request, _error) => {
                ResponseType::ExternalFailure
            }
            Error::UnhandledType(_setting_type) => ResponseType::UnhandledType,
            Error::DeliveryError(_setting_type_1, _setting_type_2) => ResponseType::DeliveryError,
            Error::UnexpectedError(_error) => ResponseType::UnexpectedError,
            Error::UndeliverableError(_setting_type, _request) => ResponseType::UndeliverableError,
            Error::UnsupportedError(_setting_type) => ResponseType::UnsupportedError,
            Error::CommunicationError => ResponseType::CommunicationError,
            Error::IrrecoverableError => ResponseType::IrrecoverableError,
            Error::TimeoutError => ResponseType::TimeoutError,
        }
    }
}

impl From<Response> for ResponseType {
    fn from(response: Response) -> Self {
        match response {
            Ok(Some(_)) => ResponseType::OkSome,
            Ok(None) => ResponseType::OkNone,
            Err(error) => error.into(),
        }
    }
}
