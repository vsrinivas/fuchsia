// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::lowlevel::{Argument, Arguments};
use crate::serde::DeserializeErrorCause;

pub fn extract_vec_from_args(
    arguments: &Arguments,
) -> Result<&Vec<Argument>, DeserializeErrorCause> {
    if let Arguments::ArgumentList(arg_vec) = arguments {
        Ok(arg_vec)
    } else {
        Err(DeserializeErrorCause::UnknownArguments(arguments.clone()))
    }
}

pub fn extract_vec_vec_from_args(
    arguments: &Arguments,
) -> Result<&Vec<Vec<Argument>>, DeserializeErrorCause> {
    if let Arguments::ParenthesisDelimitedArgumentLists(arg_vec_vec) = arguments {
        Ok(arg_vec_vec)
    } else {
        Err(DeserializeErrorCause::UnknownArguments(arguments.clone()))
    }
}

pub fn extract_primitive_from_field<'a>(
    field: &'a Argument,
    args_for_error_reporting: &Arguments,
) -> Result<&'a String, DeserializeErrorCause> {
    if let Argument::PrimitiveArgument(arg) = field {
        Ok(arg)
    } else {
        Err(DeserializeErrorCause::UnknownArguments(args_for_error_reporting.clone()))
    }
}

pub fn extract_int_from_primitive(
    field: &str,
    args_for_error_reporting: &Arguments,
) -> Result<i64, DeserializeErrorCause> {
    field
        .parse()
        .map_err(|_| DeserializeErrorCause::UnknownArguments(args_for_error_reporting.clone()))
}

pub fn extract_key_from_field<'a>(
    field: &'a Argument,
    args_for_error_reporting: &'a Arguments,
) -> Result<&'a String, DeserializeErrorCause> {
    if let Argument::KeyValueArgument { key, .. } = field {
        Ok(key)
    } else {
        Err(DeserializeErrorCause::UnknownArguments(args_for_error_reporting.clone()))
    }
}

pub fn extract_value_from_field<'a>(
    field: &'a Argument,
    args_for_error_reporting: &'a Arguments,
) -> Result<&'a String, DeserializeErrorCause> {
    if let Argument::KeyValueArgument { value, .. } = field {
        Ok(value)
    } else {
        Err(DeserializeErrorCause::UnknownArguments(args_for_error_reporting.clone()))
    }
}
