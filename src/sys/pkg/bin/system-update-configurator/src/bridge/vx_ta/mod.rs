// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod tee_client_api;

use self::tee_client_api::*;
use std::fmt;
use std::fmt::Debug;
use std::mem;
use std::ptr;
use thiserror::Error;
use tracing::debug;

use self::tee_client_api::{
    teec_operation_impl, TEEC_Operation as TeecOperation, TEEC_Value as TeecValue,
};

use self::tee_client_api::{
    TEEC_Parameter as TeecParameter, TEEC_NONE, TEEC_VALUE_INPUT, TEEC_VALUE_OUTPUT,
};

const TA_VX_CMD_OTA_CONFIG_SET: u32 = 24;
const TA_VX_CMD_OTA_CONFIG_GET: u32 = 25;

/// The general error type returned by TEE
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum TeeError {
    General(u32),
    Busy,
}

impl fmt::Display for TeeError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:?}", self)
    }
}

pub fn ota_config_get(default_value: u32) -> Result<u32, TeeError> {
    let param_type = teec_param_types(TEEC_VALUE_INPUT, TEEC_VALUE_OUTPUT, TEEC_NONE, TEEC_NONE);
    let params = [
        get_value_parameter(default_value, 0),
        get_value_parameter(0, 0),
        get_zero_parameter(),
        get_zero_parameter(),
    ];
    let mut op = create_operation(param_type, params);
    // SAFETY: op was initialized by create_operation and does not contain TEEC_MEMREF_*
    // parameters
    unsafe { call_command(&mut op, TA_VX_CMD_OTA_CONFIG_GET).map_err(map_tee_error)? };
    // SAFETY: op.params[1] is safe to use here because it was initialized by
    // call_command->tee_session.invoke_command invocation.
    let value = unsafe { op.params[1].value.a };
    Ok(value)
}

pub fn ota_config_set(value: u32) -> Result<(), TeeError> {
    let param_type = teec_param_types(TEEC_VALUE_INPUT, TEEC_NONE, TEEC_NONE, TEEC_NONE);
    let params = [
        get_value_parameter(value, 0),
        get_zero_parameter(),
        get_zero_parameter(),
        get_zero_parameter(),
    ];
    let mut op = create_operation(param_type, params);
    // SAFETY: op was initialized by create_operation and does not contain TEEC_MEMREF_*
    // parameters
    unsafe { call_command(&mut op, TA_VX_CMD_OTA_CONFIG_SET).map_err(map_tee_error) }
}

fn map_tee_error(error_code: u32) -> TeeError {
    match error_code {
        TEEC_ERROR_BUSY => TeeError::Busy,
        _ => TeeError::General(error_code),
    }
}

/// The TA UUID for VX: 99dc95b2-938e-47eb-80e8-9404ae8a1385.
static VX_TA_UUID: TEEC_UUID = TEEC_UUID {
    timeLow: 0x99dc95b2,
    timeMid: 0x938e,
    timeHiAndVersion: 0x47eb,
    clockSeqAndNode: [0x80, 0xe8, 0x94, 0x04, 0xae, 0x8a, 0x13, 0x85],
};

/// Gets a None parameter.
fn get_zero_parameter() -> TeecParameter {
    // SAFETY: All zeroes is a valid byte pattern for TeecParameter
    let zero_parameter: TeecParameter = unsafe { mem::zeroed() };
    zero_parameter
}

/// Gets a value parameter.
fn get_value_parameter(a: u32, b: u32) -> TeecParameter {
    TeecParameter { value: TeecValue { a, b } }
}

/// Creates an operation object that would be used in call_command.
fn create_operation(param_type: u32, params: [TeecParameter; 4]) -> TeecOperation {
    TeecOperation {
        started: 0,
        paramTypes: param_type,
        params,
        imp: teec_operation_impl { reserved: 0 as ::std::os::raw::c_char },
    }
}

/// This is the same macro definition as TEEC_PARAM_TYPES in tee-client-types.h
fn teec_param_types(param0_type: u32, param1_type: u32, param2_type: u32, param3_type: u32) -> u32 {
    (param0_type & 0xF)
        | ((param1_type & 0xF) << 4)
        | ((param2_type & 0xF) << 8)
        | ((param3_type & 0xF) << 12)
}

/// Creates a temporary session and call a command.
///
/// Returns error code on failure.
///
/// # Safety
///  - op should be prepared carefully (especially for TEEC_MEMREF_TEMP_*
///    param types: TEEC_TempMemoryReference::buffer should point to valid
///    memory block) otherwise dereference of arbitrary memory can happened.
///  - command_id is a valid TEE request ID
///
unsafe fn call_command(op: &mut TeecOperation, command_id: u32) -> Result<(), u32> {
    let mut tee_context = TeeContext::new()?;
    // SAFETY: tee_session is dropped at the end of the function, before the spawning context
    let mut tee_session = tee_context.new_session()?;
    let mut return_origin: u32 = 0;
    // SAFETY: op is a valid operation, return_origin points to a u32 that is valid for writes
    tee_session.invoke_command(command_id, op, &mut return_origin)
}

struct TeeContext {
    context: TEEC_Context,
}

impl TeeContext {
    pub fn new() -> Result<Self, u32> {
        // SAFETY: All zeroes is a valid byte pattern for TEEC_Context
        let mut context: TEEC_Context = unsafe { mem::zeroed() };
        // SAFETY: null is a valid name argument, context points to a TEEC_Context that is valid
        // for writes
        let result = unsafe { TEEC_InitializeContext(ptr::null(), &mut context) };
        if result != TEEC_SUCCESS {
            debug!("Failed to initialize context: {:?}", result);
            return Err(result);
        }
        Ok(TeeContext { context })
    }

    /// # Safety
    ///
    /// The returned session must be dropped before the context is dropped
    ///
    pub unsafe fn new_session(&mut self) -> Result<TeeSession, u32> {
        // SAFETY: All zeroes is a valid byte pattern for TEEC_Session
        let mut session: TEEC_Session = mem::zeroed();

        let mut return_origin: u32 = 0;
        // SAFETY:
        //  - self.context is initialized
        //  - session points to a TEEC_Session that is valid for writes
        //  - VA_TA_UUID points to a TEEC_UUID that is valid for reads
        //  - null is a valid argument for connection_data and operation
        //  - return_origin points to a u32 that is valid for writes
        let result = TEEC_OpenSession(
            &mut self.context,
            &mut session,
            &VX_TA_UUID,
            TEEC_LOGIN_PUBLIC,
            ptr::null_mut(),
            ptr::null_mut(),
            &mut return_origin,
        );
        if result != TEEC_SUCCESS {
            debug!("Failed to open session ({:?})\n", result);
            return Err(result);
        }
        Ok(TeeSession { session })
    }
}

impl Drop for TeeContext {
    fn drop(&mut self) {
        // SAFETY: all sessions related to this TEE context have been closed.
        unsafe { TEEC_FinalizeContext(&mut self.context) };
    }
}

struct TeeSession {
    session: TEEC_Session,
}

impl TeeSession {
    /// # Safety
    ///
    ///  - self.session points to an open connection
    ///  - command_id is valid TA request ID to invoke
    ///  - operation should be prepared carefully (especially for
    ///    TEEC_MEMREF_TEMP_* param types: TEEC_TempMemoryReference::buffer
    ///    should point to valid memory block) otherwise dereference of
    ///    arbitrary memory can happened.
    ///  - return_origin points to a u32 that is valid for writes.
    pub unsafe fn invoke_command(
        &mut self,
        command_id: u32,
        operation: *mut TEEC_Operation,
        return_origin: *mut u32,
    ) -> Result<(), u32> {
        // SAFETY:
        //  - self.session points to an open connection
        //  - command_id is the ID of the command to invoke
        //  - operation points to a TEEC_Operation that is valid for reads and writes
        //  - return_origin points to a u32 that is valid for writes
        let result = TEEC_InvokeCommand(&mut self.session, command_id, operation, return_origin);
        if result != TEEC_SUCCESS {
            debug!("TEEC_InvokeCommand failed with code {:?}", result);
            return Err(result);
        }
        Ok(())
    }
}

impl Drop for TeeSession {
    fn drop(&mut self) {
        // SAFETY: self.session is open and may be closed
        unsafe { TEEC_CloseSession(&mut self.session) };
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;

    #[fuchsia::test]
    async fn no_tee_connection_test() {
        let rc = ota_config_get(0);
        assert_matches!(rc, Err(TeeError::General(TEEC_ERROR_NOT_SUPPORTED)));

        let rc = ota_config_set(0);
        assert_matches!(rc, Err(TeeError::General(TEEC_ERROR_NOT_SUPPORTED)));
    }
}
