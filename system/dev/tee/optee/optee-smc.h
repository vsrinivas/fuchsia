// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "tee-smc.h"

namespace optee {

//
// OP-TEE Return codes
//
// These are the possible return codes that could come back in x0 of the SMC call. OP-TEE allocates
// the upper 16 bits of the return code to designate whether the OP-TEE is initiating an RPC call
// that the non secure world must complete.
constexpr uint32_t kReturnOk = 0x0;
constexpr uint32_t kReturnEThreadLimit = 0x1;
constexpr uint32_t kReturnEBusy = 0x2;
constexpr uint32_t kReturnEResume = 0x3;
constexpr uint32_t kReturnEBadAddress = 0x4;
constexpr uint32_t kReturnEBadCommand = 0x5;
constexpr uint32_t kReturnENoMemory = 0x6;
constexpr uint32_t kReturnENotAvailable = 0x7;

constexpr uint32_t kReturnRpcPrefixMask = 0xFFFF0000;
constexpr uint32_t kReturnRpcPrefix = 0xFFFF0000;
constexpr uint32_t kReturnRpcFunctionMask = 0x0000FFFF;

// Helper function for identifying return codes that are actually an RPC initiating function. Care
// must be taken to ensure that we don't misidentify an SMC Unknown Function return code as an RPC
// return code, as the bits do overlap.
static constexpr bool IsReturnRpc(uint32_t return_code) {
    return (return_code != tee::kSmc32ReturnUnknownFunction) &&
           ((return_code & kReturnRpcPrefixMask) == kReturnRpcPrefix);
}

//
// Function ID helpers
//
// The Function IDs for OP-TEE SMC calls only vary in the call type and the function number. The
// calling convention is always SMC32 and obviously it's always accessing the Trusted OS Service.
// These wrapper functions eliminate the need to specify those each time.
static constexpr uint32_t CreateFastOpteeFuncId(uint16_t func_num) {
    return tee::CreateFunctionId(tee::kFastCall,
                                 tee::kSmc32CallConv,
                                 tee::kTrustedOsService,
                                 func_num);
}

static constexpr uint32_t CreateYieldOpteeFuncId(uint16_t func_num) {
    return tee::CreateFunctionId(tee::kYieldingCall,
                                 tee::kSmc32CallConv,
                                 tee::kTrustedOsService,
                                 func_num);
}

//
// OP-TEE API constants
//
// These constants represent the expected values to the Call UID and Revision general service
// queries for OP-TEE.
constexpr uint32_t kOpteeApiUid_0 = 0x384FB3E0;
constexpr uint32_t kOpteeApiUid_1 = 0xE7F811E3;
constexpr uint32_t kOpteeApiUid_2 = 0xAF630002;
constexpr uint32_t kOpteeApiUid_3 = 0xA5D5C51B;

constexpr uint32_t kOpteeApiRevisionMajor = 2;
constexpr uint32_t kOpteeApiRevisionMinor = 0;

//
// OP-TEE SMC Messages
//
// The below section defines the format for OP-TEE specific Secure Monitor Calls. For each OP-TEE
// function, there should be a function identifier and an expected result structure. The result
// structures are intended to be overlaid with the zx_smc_result_t structure that is populated
// by the SMC call. It should be noted that the zx_smc_result_t structure is made up of four 64
// bit values that represent the x0-x3 registers, but OP-TEE always uses the SMC32 calling
// convention. As such, fields in the result structures will only have 32 relevant bits.

//
// Get Trusted OS UUID (0x0000)
//
// This SMC function will return the UUID of the Trusted OS. In our case, it should return
// OP-TEE's UUID.
//
// Parameters:
// arg1..6 - not used
//
// Results:
// arg0 - UUID Bytes 0:3
// arg1 - UUID Bytes 4:7
// arg2 - UUID Bytes 8:11
// arg3 - UUID Bytes 12:15
constexpr uint32_t kGetOsUuidFuncId = CreateFastOpteeFuncId(0x0000);

DEFINE_SMC_RESULT_STRUCT(GetOsUuidResult, 4,
                         uint32_t, uuid_0,
                         uint32_t, uuid_1,
                         uint32_t, uuid_2,
                         uint32_t, uuid_3)

constexpr uint32_t kOpteeOsUuid_0 = 0x486178E0;
constexpr uint32_t kOpteeOsUuid_1 = 0xE7F811E3;
constexpr uint32_t kOpteeOsUuid_2 = 0xBC5E0002;
constexpr uint32_t kOpteeOsUuid_3 = 0xA5D5C51B;

//
// Get Trusted OS Revision (0x0001)
//
// This SMC function will return the revision of the Trusted OS. Note that this is different than
// the revision of the Call API revision.
//
// Parameters:
// arg1..6 - not used
//
// Results:
// arg0 - major version
// arg1 - minor version
// arg2..3 - not used
constexpr uint32_t kGetOsRevisionFuncId = CreateFastOpteeFuncId(0x0001);

DEFINE_SMC_RESULT_STRUCT(GetOsRevisionResult, 2,
                         uint32_t, major,
                         uint32_t, minor)

//
// Resume from RPC (0x0003)
//
// TODO(rjascani) - Document parameters and result values
constexpr uint32_t kReturnFromRpcFuncId = CreateYieldOpteeFuncId(0x0003);

//
// Call with Arguments (0x0004)
//
// TODO(rjascani) - Document parameters and result values
constexpr uint32_t kCallWithArgFuncId = CreateYieldOpteeFuncId(0x0004);

//
// Get Shared Memory Config (0x0007)
//
// TODO(rjascani) - Document parameters and result values
constexpr uint32_t kGetSharedMemConfigFuncId = CreateFastOpteeFuncId(0x0007);

DEFINE_SMC_RESULT_STRUCT(GetSharedMemConfigResult, 4,
                         int32_t, status,
                         uint32_t, start,
                         uint32_t, size,
                         uint32_t, settings)

//
// Exchange Capabilities (0x0009)
//
// Exchange capabilities between nonsecure and secure world.
//
// Parameters:
// arg1 - Non-Secure world capabilities bitfield
// arg2..6 - not used
//
// Results:
// arg0 - Status code indicating whether secure world can use non-secure capabilities
// arg1 - Secure world capabilities bitfield
// arg2..3 - not used
constexpr uint32_t kExchangeCapabilitiesFuncId = CreateFastOpteeFuncId(0x0009);

constexpr uint32_t kNonSecureCapUniprocessor = (1 << 0);

constexpr uint32_t kSecureCapHasReservedSharedMem = (1 << 0);
constexpr uint32_t kSecureCapCanUsePrevUnregisteredSharedMem = (1 << 1);
constexpr uint32_t kSecureCapCanUseDynamicSharedMem = (1 << 2);

DEFINE_SMC_RESULT_STRUCT(ExchangeCapabilitiesResult, 2,
                         int32_t, status,
                         uint32_t, secure_world_capabilities)

//
// Disable Shared Memory Cache (0x000A)
//
// TODO(rjascani) - Document parameters and result values
constexpr uint32_t kDisableSharedMemCacheFuncId = CreateFastOpteeFuncId(0x000A);

DEFINE_SMC_RESULT_STRUCT(DisableSharedMemCacheResult, 3,
                         int32_t, status,
                         uint32_t, shared_mem_upper32,
                         uint32_t, shared_mem_lower32)

//
// Enable Shared Memory Cache (0x000B)
//
// TODO(rjascani) - Document parameters and result values
constexpr uint32_t kEnableSharedMemCacheFuncId = CreateFastOpteeFuncId(0x000B);

} // namespace optee
