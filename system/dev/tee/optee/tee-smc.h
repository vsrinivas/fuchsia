// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inttypes.h>

#include <zircon/syscalls/smc.h>

#define __DEFINE_SMC_RESULT_ARG_4(type0, name0, type1, name1, type2, name2, type3, name3) \
    alignas(alignof(decltype(zx_smc_result_t::arg0))) type0 name0;                        \
    alignas(alignof(decltype(zx_smc_result_t::arg1))) type1 name1;                        \
    alignas(alignof(decltype(zx_smc_result_t::arg2))) type2 name2;                        \
    alignas(alignof(decltype(zx_smc_result_t::arg3))) type3 name3;

#define __DEFINE_SMC_RESULT_ARG_3(...) \
    __DEFINE_SMC_RESULT_ARG_4(__VA_ARGS__, uint64_t, unused3)

#define __DEFINE_SMC_RESULT_ARG_2(...) \
    __DEFINE_SMC_RESULT_ARG_3(__VA_ARGS__, uint64_t, unused2)

#define __DEFINE_SMC_RESULT_ARG_1(...) \
    __DEFINE_SMC_RESULT_ARG_2(__VA_ARGS__, uint64_t, unused1)

#define __CHECK_SMC_RESULT_OFFSETS_ARG_4(result_type, _0, name0, _1, name1, _2, name2, _3, name3) \
    static_assert(offsetof(result_type, name0) == offsetof(zx_smc_result_t, arg0),                \
                  "name0 is not aligned with the offset of zx_smc_result_t::arg0");               \
    static_assert(offsetof(result_type, name1) == offsetof(zx_smc_result_t, arg1),                \
                  "name1 is not aligned with the offset of zx_smc_result_t::arg1");               \
    static_assert(offsetof(result_type, name2) == offsetof(zx_smc_result_t, arg2),                \
                  "name2 is not aligned with the offset of zx_smc_result_t::arg2");               \
    static_assert(offsetof(result_type, name3) == offsetof(zx_smc_result_t, arg3),                \
                  "name3 is not aligned with the offset of zx_smc_result_t::arg3");

#define __CHECK_SMC_RESULT_OFFSETS_ARG_3(...) \
    __CHECK_SMC_RESULT_OFFSETS_ARG_4(__VA_ARGS__, uint64_t, unused3)

#define __CHECK_SMC_RESULT_OFFSETS_ARG_2(...) \
    __CHECK_SMC_RESULT_OFFSETS_ARG_3(__VA_ARGS__, uint64_t, unused2)

#define __CHECK_SMC_RESULT_OFFSETS_ARG_1(...) \
    __CHECK_SMC_RESULT_OFFSETS_ARG_2(__VA_ARGS__, uint64_t, unused1)

// Helper macro for defining a struct that is intended to overlay with a zx_smc_result_t. The
// zx_smc_result_t has four uint64_t members that are read from registers x0-x3 on the SMC return,
// but the values returned could actually int64_t, int32_t or uint32_t. It is dependent on the SMC
// function that was invoked and the return code in x0. The macro allows for the definition of up
// to four members within the result type that should align with arg0-arg3. For examples, see the
// usages later in this file.
//
// Parameters:
// result_type - Name of the type to be defined
// num_members - Number of data members in the type to be defined
// ... - List of types and names for the data members (see examples below)
#define DEFINE_SMC_RESULT_STRUCT(result_type, num_members, ...)                               \
    static_assert(num_members > 0, "SMC result structure must have more than 0 members");     \
    static_assert(num_members <= 4, "SMC result structure must have no more than 4 members"); \
    struct result_type {                                                                      \
        __DEFINE_SMC_RESULT_ARG_##num_members(__VA_ARGS__)                                    \
    };                                                                                        \
    static_assert(sizeof(result_type) == sizeof(zx_smc_result_t),                             \
                  "result_type must be the same size of zx_smc_result_t");                    \
    __CHECK_SMC_RESULT_OFFSETS_ARG_##num_members(result_type, __VA_ARGS__)

namespace tee {

enum CallType : uint8_t {
    kYieldingCall = 0,
    kFastCall = 1,
};

enum CallConvention : uint8_t {
    kSmc32CallConv = 0,
    kSmc64CallConv = 1,
};

enum Service : uint8_t {
    kArchService = 0x00,
    kCpuService = 0x01,
    kSipService = 0x02,
    kOemService = 0x03,
    kStandardService = 0x04,
    kTrustedOsService = 0x32,
    kTrustedOsServiceEnd = 0x3F,
};

constexpr uint8_t kCallTypeMask = 0x01;
constexpr uint8_t kCallTypeShift = 31;
constexpr uint8_t kCallConvMask = 0x01;
constexpr uint8_t kCallConvShift = 30;
constexpr uint8_t kServiceMask = 0x3F;
constexpr uint8_t kServiceShift = 24;

constexpr uint64_t kSmc64ReturnUnknownFunction = static_cast<uint64_t>(-1);
constexpr uint32_t kSmc32ReturnUnknownFunction = static_cast<uint32_t>(-1);

static constexpr uint32_t CreateFunctionId(CallType call_type,
                                           CallConvention call_conv,
                                           Service service,
                                           uint16_t function_num) {
    return (((call_type & kCallTypeMask) << kCallTypeShift) |
            ((call_conv & kCallConvMask) << kCallConvShift) |
            ((service & kServiceMask) << kServiceShift) |
            function_num);
}

// C++ wrapper function for constructing a zx_smc_parameters_t object. Most of the arguments are
// rarely used, so this defaults everything other than the function id to 0. Most of the function
// calls are also constant, so they should be populated at compile time if possible.
static constexpr zx_smc_parameters_t CreateSmcFunctionCall(
    uint32_t func_id,
    uint64_t arg1 = 0, uint64_t arg2 = 0, uint64_t arg3 = 0,
    uint64_t arg4 = 0, uint64_t arg5 = 0, uint64_t arg6 = 0,
    uint16_t client_id = 0, uint16_t secure_os_id = 0) {
    return {func_id, arg1, arg2, arg3, arg4, arg5, arg6, client_id, secure_os_id};
}

//
// Call Count Query (0xFF00)
//
// Returns a 32-bit count of the available service calls. The count includes both 32 and 64
// calling convention service calls and both fast and yielding calls.
//
// Parameters:
// arg1..arg6 - not used
//
// Results:
// arg0 - call count
// arg1..arg3 - not used
constexpr uint32_t kTrustedOsCallCountFuncId = CreateFunctionId(kFastCall,
                                                                kSmc32CallConv,
                                                                kTrustedOsServiceEnd,
                                                                0xFF00);

DEFINE_SMC_RESULT_STRUCT(TrustedOsCallCountResult, 1, uint32_t, call_count);

//
// Call UID Query (0xFF01)
//
// Returns a unique identifier of the service provider.
//
// Parameters:
// arg1..arg6 - not used
//
// Results:
// arg0 - UID Bytes 0:3
// arg1 - UID Bytes 4:7
// arg2 - UID Bytes 8:11
// arg3 - UID Bytes 12:15
constexpr uint32_t kTrustedOsCallUidFuncId = CreateFunctionId(kFastCall,
                                                              kSmc32CallConv,
                                                              kTrustedOsServiceEnd,
                                                              0xFF01);

DEFINE_SMC_RESULT_STRUCT(TrustedOsCallUidResult, 4,
                         uint32_t, uid_0_3,
                         uint32_t, uid_4_7,
                         uint32_t, uid_8_11,
                         uint32_t, uid_12_15);

//
// Call Revision Query (0xFF03)
//
// Returns revision details of the service. Different major version values indicate a possible
// incompatibility between SMC/HVC APIs, for the affected range.
//
// For two revisions, A and B, where the major version values are identical, and the minor version
// value of revision B is greater than the minor version value of revision B, every SMC/HVC
// instruction in the affected range that works in revision A must also work in revision B, with a
// compatible effect.
//
// Parameters:
// arg1..arg6 - not used
//
// Results:
// arg0 - major version
// arg1 - minor version
// arg2..3 - not used
constexpr uint32_t kTrustedOsCallRevisionFuncId = CreateFunctionId(kFastCall,
                                                                   kSmc32CallConv,
                                                                   kTrustedOsServiceEnd,
                                                                   0xFF03);

DEFINE_SMC_RESULT_STRUCT(TrustedOsCallRevisionResult, 2,
                         uint32_t, major,
                         uint32_t, minor);

} // namespace tee
