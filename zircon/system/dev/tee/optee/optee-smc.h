// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/assert.h>

#include "tee-smc.h"

#define __DEFINE_RPC_RESULT_ARG_6(type1, name1, type2, name2, type3, name3, type4, name4, type5, \
                                  name5, type6, name6)                                           \
  alignas(alignof(decltype(zx_smc_parameters_t::func_id))) uint32_t func_id;                     \
  alignas(alignof(decltype(zx_smc_parameters_t::arg1))) type1 name1;                             \
  alignas(alignof(decltype(zx_smc_parameters_t::arg2))) type2 name2;                             \
  alignas(alignof(decltype(zx_smc_parameters_t::arg3))) type3 name3;                             \
  alignas(alignof(decltype(zx_smc_parameters_t::arg4))) type4 name4;                             \
  alignas(alignof(decltype(zx_smc_parameters_t::arg5))) type5 name5;                             \
  alignas(alignof(decltype(zx_smc_parameters_t::arg6))) type6 name6;

#define __DEFINE_RPC_RESULT_ARG_5(...) __DEFINE_RPC_RESULT_ARG_6(__VA_ARGS__, uint64_t, unused5)

#define __DEFINE_RPC_RESULT_ARG_4(...) __DEFINE_RPC_RESULT_ARG_5(__VA_ARGS__, uint64_t, unused4)

#define __DEFINE_RPC_RESULT_ARG_3(...) __DEFINE_RPC_RESULT_ARG_4(__VA_ARGS__, uint64_t, unused3)

#define __DEFINE_RPC_RESULT_ARG_2(...) __DEFINE_RPC_RESULT_ARG_3(__VA_ARGS__, uint64_t, unused2)

#define __DEFINE_RPC_RESULT_ARG_1(...) __DEFINE_RPC_RESULT_ARG_2(__VA_ARGS__, uint64_t, unused1)

#define __DEFINE_RPC_RESULT_ARG_0(...) __DEFINE_RPC_RESULT_ARG_1(uint64_t, unused0)

#define __CHECK_RPC_RESULT_OFFSETS_ARG_6(result_type, _1, name1, _2, name2, _3, name3, _4, name4, \
                                         _5, name5, _6, name6)                                    \
  static_assert(offsetof(result_type, func_id) == offsetof(zx_smc_parameters_t, func_id),         \
                "func_id is not aligned with the offset of zx_smc_parameters_t::func_id");        \
  static_assert(offsetof(result_type, name1) == offsetof(zx_smc_parameters_t, arg1),              \
                "name1 is not aligned with the offset of zx_smc_parameters_t::arg1");             \
  static_assert(offsetof(result_type, name2) == offsetof(zx_smc_parameters_t, arg2),              \
                "name2 is not aligned with the offset of zx_smc_parameters_t::arg2");             \
  static_assert(offsetof(result_type, name3) == offsetof(zx_smc_parameters_t, arg3),              \
                "name3 is not aligned with the offset of zx_smc_parameters_t::arg3");             \
  static_assert(offsetof(result_type, name4) == offsetof(zx_smc_parameters_t, arg4),              \
                "name4 is not aligned with the offset of zx_smc_parameters_t::arg4");             \
  static_assert(offsetof(result_type, name5) == offsetof(zx_smc_parameters_t, arg5),              \
                "name5 is not aligned with the offset of zx_smc_parameters_t::arg5");             \
  static_assert(offsetof(result_type, name6) == offsetof(zx_smc_parameters_t, arg6),              \
                "name6 is not aligned with the offset of zx_smc_parameters_t::arg6");

#define __CHECK_RPC_RESULT_OFFSETS_ARG_5(...) \
  __CHECK_RPC_RESULT_OFFSETS_ARG_6(__VA_ARGS__, uint64_t, unused5)

#define __CHECK_RPC_RESULT_OFFSETS_ARG_4(...) \
  __CHECK_RPC_RESULT_OFFSETS_ARG_5(__VA_ARGS__, uint64_t, unused4)

#define __CHECK_RPC_RESULT_OFFSETS_ARG_3(...) \
  __CHECK_RPC_RESULT_OFFSETS_ARG_4(__VA_ARGS__, uint64_t, unused3)

#define __CHECK_RPC_RESULT_OFFSETS_ARG_2(...) \
  __CHECK_RPC_RESULT_OFFSETS_ARG_3(__VA_ARGS__, uint64_t, unused2)

#define __CHECK_RPC_RESULT_OFFSETS_ARG_1(...) \
  __CHECK_RPC_RESULT_OFFSETS_ARG_2(__VA_ARGS__, uint64_t, unused1)

#define __CHECK_RPC_RESULT_OFFSETS_ARG_0(result_type, ...) \
  __CHECK_RPC_RESULT_OFFSETS_ARG_1(result_type, uint64_t, unused0)

// Helper macro for defining a struct that is intended to overlay with a zx_smc_parameters_t. The
// zx_smc_parameters_t has six uint64_t members that are passed as arguments to an SMC call, but
// the values being used could actually int64_t, int32_t or uint32_t. It is dependent on the RPC
// function that was invoked. The macro allows for the definition of up to six members within the
// result type that should align with arg1-arg6. For examples, see the usages later in this file.
//
// Parameters:
// result_type:  Name of the type to be defined.
// num_members:  Number of arguments in the type to be defined.
// ...:          List of types and names for the data members (see examples below).
#define DEFINE_RPC_RESULT_STRUCT(result_type, num_members, ...)                             \
  static_assert(num_members <= 6, "RPC result structure must have no more than 6 members"); \
  struct result_type {                                                                      \
    __DEFINE_RPC_RESULT_ARG_##num_members(__VA_ARGS__)                                      \
  };                                                                                        \
  static_assert(sizeof(result_type) <= sizeof(zx_smc_parameters_t),                         \
                "result_type cannot be larger in size than zx_smc_parameters_t");           \
  __CHECK_RPC_RESULT_OFFSETS_ARG_##num_members(result_type, __VA_ARGS__)

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
  return (return_code != tee_smc::kSmc32ReturnUnknownFunction) &&
         ((return_code & kReturnRpcPrefixMask) == kReturnRpcPrefix);
}

// Helper function for getting the RPC function code from a return code.
// Note: only return codes containing the RPC prefix should be passed to this function. See
// optee::IsReturnRpc() for details.
static constexpr uint32_t GetRpcFunctionCode(uint32_t return_code) {
  ZX_DEBUG_ASSERT_MSG(IsReturnRpc(return_code), "Return code must contain the RPC prefix!");
  return return_code & kReturnRpcFunctionMask;
}

//
// Function ID helpers
//
// The Function IDs for OP-TEE SMC calls only vary in the call type and the function number. The
// calling convention is always SMC32 and obviously it's always accessing the Trusted OS Service.
// These wrapper functions eliminate the need to specify those each time.
static constexpr uint32_t CreateFastOpteeFuncId(uint16_t func_num) {
  return tee_smc::CreateFunctionId(tee_smc::kFastCall, tee_smc::kSmc32CallConv,
                                   tee_smc::kTrustedOsService, func_num);
}

static constexpr uint32_t CreateYieldOpteeFuncId(uint16_t func_num) {
  return tee_smc::CreateFunctionId(tee_smc::kYieldingCall, tee_smc::kSmc32CallConv,
                                   tee_smc::kTrustedOsService, func_num);
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
// OP-TEE SMC Functions
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
// Get the UUID of the Trusted OS. For OP-TEE, this should return OP-TEE's UUID.
//
// Parameters:
// arg1-6:  Unused.
//
// Results:
// arg0:    UUID Bytes 0:3
// arg1:    UUID Bytes 4:7
// arg2:    UUID Bytes 8:11
// arg3:    UUID Bytes 12:15
constexpr uint32_t kGetOsUuidFuncId = CreateFastOpteeFuncId(0x0000);

DEFINE_SMC_RESULT_STRUCT(GetOsUuidResult, 4, uint32_t, uuid_0, uint32_t, uuid_1, uint32_t, uuid_2,
                         uint32_t, uuid_3)

//
// Get Trusted OS Revision (0x0001)
//
// Get the revision number of the Trusted OS. Note that this is different from the revision of the
// Call API revision.
//
// Parameters:
// arg1-6:  Unused.
//
// Results:
// arg0:    Major version.
// arg1:    Minor version
// arg2-3:  Unused.
constexpr uint32_t kGetOsRevisionFuncId = CreateFastOpteeFuncId(0x0001);

DEFINE_SMC_RESULT_STRUCT(GetOsRevisionResult, 2, uint32_t, major, uint32_t, minor)

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

DEFINE_SMC_RESULT_STRUCT(CallWithArgResult, 4, uint32_t, status, uint32_t, arg1, uint32_t, arg2,
                         uint32_t, arg3)

//
// Get Shared Memory Config (0x0007)
//
// TODO(rjascani) - Document parameters and result values
constexpr uint32_t kGetSharedMemConfigFuncId = CreateFastOpteeFuncId(0x0007);

DEFINE_SMC_RESULT_STRUCT(GetSharedMemConfigResult, 4, int32_t, status, uint32_t, start, uint32_t,
                         size, uint32_t, settings)

//
// Exchange Capabilities (0x0009)
//
// Exchange capabilities between non-secure and secure world.
//
// Parameters:
// arg1:    Non-secure world capabilities bitfield.
// arg2-6:  Unused.
//
// Results:
// arg0:    Status code indicating whether secure world can use non-secure capabilities.
// arg1:    Secure world capabilities bitfield
// arg2-3:  Unused.
constexpr uint32_t kExchangeCapabilitiesFuncId = CreateFastOpteeFuncId(0x0009);

constexpr uint32_t kNonSecureCapUniprocessor = (1 << 0);

constexpr uint32_t kSecureCapHasReservedSharedMem = (1 << 0);
constexpr uint32_t kSecureCapCanUsePrevUnregisteredSharedMem = (1 << 1);
constexpr uint32_t kSecureCapCanUseDynamicSharedMem = (1 << 2);

DEFINE_SMC_RESULT_STRUCT(ExchangeCapabilitiesResult, 2, int32_t, status, uint32_t,
                         secure_world_capabilities)

//
// Disable Shared Memory Cache (0x000A)
//
// TODO(rjascani) - Document parameters and result values
constexpr uint32_t kDisableSharedMemCacheFuncId = CreateFastOpteeFuncId(0x000A);

DEFINE_SMC_RESULT_STRUCT(DisableSharedMemCacheResult, 3, int32_t, status, uint32_t,
                         shared_mem_upper32, uint32_t, shared_mem_lower32)

//
// Enable Shared Memory Cache (0x000B)
//
// TODO(rjascani) - Document parameters and result values
constexpr uint32_t kEnableSharedMemCacheFuncId = CreateFastOpteeFuncId(0x000B);

//
// OP-TEE RPC Functions
//
// The below section defines the format for OP-TEE specific RPC functions. An RPC function is an
// action the TEE OS is requesting the driver perform. After completing the requested action, the
// driver calls back into the TEE via another SMC with the parameters of the call containing the
// results. For each OP-TEE RPC function, there is a function identifier, a result structure
// (as input), and a params structure (as output).
//
// The function identifier determines which RPC function is being called by the TEE OS.
//
// The input result structure is a CallWithArgResult where the TEE OS passes the arguments for the
// RPC function. Each argument's significance is determined by the RPC function being called.
//
// The output params structure is a zx_smc_parameters_t with which to call the TEE OS back, once the
// requested RPC function has been completed.

//
// Allocate Memory (0x0000)
//
// Allocate shared memory for driver <-> TEE communication.
//
// Parameters:
// arg1:    The size, in bytes, of requested memory.
// arg2:    Unused.
// arg3:    Unused.
//
// Results:
// arg1-2:  The upper (arg1) and lower (arg2) 32-bit parts of a 64-bit physical pointer to the
//          allocated memory. Value will be 0 if requested size was 0 or if memory allocation
//          failed.
// arg3:    Unused.
// arg4-5:  The upper (arg4) and lower (arg5) 32-bit parts of a 64-bit identifier for the allocated
//          memory. The value of the identifier is implementation-defined and is passed from the
//          secure world via another RPC when the secure world wants to free the allocated memory
//          region.
// arg6:    Unused.
constexpr uint32_t kRpcFunctionIdAllocateMemory = 0x0;
DEFINE_SMC_RESULT_STRUCT(RpcFunctionAllocateMemoryArgs, 2, int32_t, status, uint32_t, size)
DEFINE_RPC_RESULT_STRUCT(RpcFunctionAllocateMemoryResult, 5, uint32_t, phys_addr_upper32, uint32_t,
                         phys_addr_lower32, uint64_t, __unused3, uint32_t, mem_id_upper32, uint32_t,
                         mem_id_lower32)

//
// Free Memory (0x0002)
//
// Free shared memory previously allocated.
//
// Parameters:
// arg1-2:  The upper (arg1) and lower (arg2) 32-bit parts of a 64-bit identifier for the memory to
//          be freed. The value of the identifier is implementation-defined and determined during
//          allocation.
// arg3:    Unused.
//
// Results:
// arg1-6:  Unused.
constexpr uint32_t kRpcFunctionIdFreeMemory = 0x2;
DEFINE_SMC_RESULT_STRUCT(RpcFunctionFreeMemoryArgs, 3, int32_t, status, uint32_t, mem_id_upper32,
                         uint32_t, mem_id_lower32)
DEFINE_RPC_RESULT_STRUCT(RpcFunctionFreeMemoryResult, 0)

//
// Deliver IRQ (0x0004)
//
// Deliver an IRQ to the rich environment.
//
// Parameters:
// arg1-3:  Unused.
//
// Results:
// arg1-6:  Unused.
constexpr uint32_t kRpcFunctionIdDeliverIrq = 0x4;
DEFINE_SMC_RESULT_STRUCT(RpcFunctionDeliverIrqArgs, 1, int32_t, status)
DEFINE_RPC_RESULT_STRUCT(RpcFunctionDeliverIrqResult, 0)

//
// Execute Command (0x0004)
//
// Execute a command specified in the provided message.
//
// Parameters:
// arg1-2:  The upper (arg1) and lower (arg2) 32-bit parts of a 64-bit identifier for the command
//          message. The value of the identifier is implementation-defined and determined during
//          allocation.
// arg3:    Unused.
//
// Results:
// arg1-6:  Unused.
constexpr uint32_t kRpcFunctionIdExecuteCommand = 0x5;
DEFINE_SMC_RESULT_STRUCT(RpcFunctionExecuteCommandsArgs, 3, int32_t, status, uint32_t,
                         msg_mem_id_upper32, uint32_t, msg_mem_id_lower32)
DEFINE_RPC_RESULT_STRUCT(RpcFunctionExecuteCommandsResult, 0)

typedef union {
  CallWithArgResult generic;
  RpcFunctionAllocateMemoryArgs allocate_memory;
  RpcFunctionFreeMemoryArgs free_memory;
  RpcFunctionDeliverIrqArgs deliver_irq;
  RpcFunctionExecuteCommandsArgs execute_command;
} RpcFunctionArgs;

typedef union {
  zx_smc_parameters_t generic;
  RpcFunctionAllocateMemoryResult allocate_memory;
  RpcFunctionFreeMemoryResult free_memory;
  RpcFunctionDeliverIrqResult delivery_irq;
  RpcFunctionExecuteCommandsResult execute_command;
} RpcFunctionResult;

enum SharedMemoryType : uint64_t {
  // Memory that can be shared with a userspace application
  kApplication = 0x0,

  // Memory that can only be shared with the "kernel"
  // "Kernel" means access up to the driver but not the userspace application, but does not
  // translate strictly to "kernel space only" due to the microkernel nature of Zircon in Fuchsia.
  kKernel = 0x1,

  // Memory that is shared with "kernel" but can be exported to userspace
  // "Kernel" means access up to the driver but not the userspace application, but does not
  // translate strictly to "kernel space only" due to the microkernel nature of Zircon in Fuchsia.
  kGlobal = 0x2
};

}  // namespace optee
