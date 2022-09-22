// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod tee_client_api;

use self::tee_client_api::*;
use std::mem;
use std::ptr;
use tracing::error;

use self::tee_client_api::{
    teec_operation_impl, TEEC_Operation as TeecOperation,
    TEEC_TempMemoryReference as TeecTempMemoryReference, TEEC_Value as TeecValue,
};

pub use self::tee_client_api::{
    TEEC_Parameter as TeecParameter, TEEC_MEMREF_TEMP_INPUT, TEEC_MEMREF_TEMP_OUTPUT, TEEC_NONE,
    TEEC_VALUE_INPUT,
};

/// The TA UUID for keysafe: the trust side app for KMS.
pub static KEYSAFE_TA_UUID: TEEC_UUID = TEEC_UUID {
    timeLow: 0x808032e0,
    timeMid: 0xfd9e,
    timeHiAndVersion: 0x4e6f,
    clockSeqAndNode: [0x88, 0x96, 0x54, 0x47, 0x35, 0xc9, 0x84, 0x80],
};

/// Gets a None parameter.
pub fn get_zero_parameter() -> TeecParameter {
    let zero_parameter: TeecParameter = unsafe { mem::zeroed() };
    zero_parameter
}

/// Gets a value parameter.
pub fn get_value_parameter(a: u32, b: u32) -> TeecParameter {
    TeecParameter { value: TeecValue { a, b } }
}

/// Gets a temp memory reference parameter as output.
pub fn get_memref_output_parameter(data: &mut [u8]) -> TeecParameter {
    TeecParameter {
        tmpref: TeecTempMemoryReference {
            buffer: data.as_mut_ptr() as *mut std::ffi::c_void,
            size: data.len(),
        },
    }
}

/// Gets a temp memory reference parameter as input.
pub fn get_memref_input_parameter(data: &[u8]) -> TeecParameter {
    TeecParameter {
        tmpref: TeecTempMemoryReference {
            buffer: data.as_ptr() as *mut std::ffi::c_void,
            size: data.len(),
        },
    }
}

/// Creates an operation object that would be used in call_command.
pub fn create_operation(param_type: u32, params: [TeecParameter; 4]) -> TeecOperation {
    TeecOperation {
        started: 0,
        paramTypes: param_type,
        params,
        imp: teec_operation_impl { reserved: 0 as ::std::os::raw::c_char },
    }
}

/// Creates a temporary session and call a command.
///
/// Returns error code on failure.
pub fn call_command(op: &mut TeecOperation, command_id: u32) -> Result<(), u32> {
    let mut tee_context = TeeContext::new()?;
    let mut tee_session = tee_context.new_session()?;
    let mut return_origin: u32 = 0;
    tee_session.invoke_command(command_id, op, &mut return_origin)
}

struct TeeContext {
    context: TEEC_Context,
}

impl TeeContext {
    pub fn new() -> Result<Self, u32> {
        let mut context: TEEC_Context = unsafe { mem::zeroed() };
        let result = unsafe { TEEC_InitializeContext(ptr::null(), &mut context) };
        if result != TEEC_SUCCESS {
            error!("Failed to initialize context: {:?}", result);
            return Err(result);
        }
        Ok(TeeContext { context })
    }

    pub fn new_session(&mut self) -> Result<TeeSession, u32> {
        let mut session: TEEC_Session = unsafe { mem::zeroed() };

        let mut return_origin: u32 = 0;
        let result = unsafe {
            TEEC_OpenSession(
                &mut self.context,
                &mut session,
                &KEYSAFE_TA_UUID,
                TEEC_LOGIN_PUBLIC,
                ptr::null_mut(),
                ptr::null_mut(),
                &mut return_origin,
            )
        };
        if result != TEEC_SUCCESS {
            error!("Failed to open session ({:?} {:?})\n", result, return_origin);
            return Err(result);
        }
        Ok(TeeSession { session })
    }
}

impl Drop for TeeContext {
    fn drop(&mut self) {
        unsafe { TEEC_FinalizeContext(&mut self.context) };
    }
}

struct TeeSession {
    session: TEEC_Session,
}

impl TeeSession {
    pub fn invoke_command(
        &mut self,
        command_id: u32,
        operation: *mut TEEC_Operation,
        return_origin: *mut u32,
    ) -> Result<(), u32> {
        let result =
            unsafe { TEEC_InvokeCommand(&mut self.session, command_id, operation, return_origin) };
        if result != TEEC_SUCCESS {
            error!("TEEC_InvokeCommand failed with code {:?} origin {:?}", result, return_origin);
            return Err(result);
        }
        Ok(())
    }
}

impl Drop for TeeSession {
    fn drop(&mut self) {
        unsafe { TEEC_CloseSession(&mut self.session) };
    }
}
