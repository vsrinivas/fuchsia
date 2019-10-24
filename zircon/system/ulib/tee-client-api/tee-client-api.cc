// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <fcntl.h>
#include <fuchsia/hardware/tee/llcpp/fidl.h>
#include <fuchsia/tee/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <cstring>
#include <string_view>

#include <tee-client-api/tee_client_api.h>

namespace fuchsia_tee = ::llcpp::fuchsia::tee;
namespace fuchsia_hardware_tee = ::llcpp::fuchsia::hardware::tee;

namespace {

// Most clients should use this.
constexpr std::string_view kTeeServicePath("/svc/fuchsia.tee.Device");

// Presently only used by clients that need to connect before the service is available / don't need
// the TEE to be able to use file services.
constexpr std::string_view kTeeDevClass("/dev/class/tee/");

constexpr uint32_t GetParamTypeForIndex(uint32_t param_types, size_t index) {
  constexpr uint32_t kBitsPerParamType = 4;
  return ((param_types >> (index * kBitsPerParamType)) & 0xF);
}

constexpr bool IsSharedMemFlagInOut(uint32_t flags) {
  constexpr uint32_t kInOutFlags = TEEC_MEM_INPUT | TEEC_MEM_OUTPUT;
  return (flags & kInOutFlags) == kInOutFlags;
}

constexpr bool IsDirectionInput(fuchsia_tee::Direction direction) {
  return ((direction == fuchsia_tee::Direction::INPUT) ||
          (direction == fuchsia_tee::Direction::INOUT));
}

constexpr bool IsDirectionOutput(fuchsia_tee::Direction direction) {
  return ((direction == fuchsia_tee::Direction::OUTPUT) ||
          (direction == fuchsia_tee::Direction::INOUT));
}

bool IsGlobalPlatformCompliant(zx::unowned_channel tee_channel) {
  auto result = fuchsia_tee::Device::Call::GetOsInfo(std::move(tee_channel));
  if (!result.ok()) {
    return false;
  }
  auto os_info = std::move(result->info);
  return os_info.is_global_platform_compliant;
}

void ConvertTeecUuidToZxUuid(const TEEC_UUID& teec_uuid, fuchsia_tee::Uuid* out_uuid) {
  ZX_DEBUG_ASSERT(out_uuid);

  out_uuid->time_low = teec_uuid.timeLow;
  out_uuid->time_mid = teec_uuid.timeMid;
  out_uuid->time_hi_and_version = teec_uuid.timeHiAndVersion;

  std::memcpy(out_uuid->clock_seq_and_node.data(), teec_uuid.clockSeqAndNode,
              sizeof(out_uuid->clock_seq_and_node));
}

constexpr TEEC_Result ConvertStatusToResult(zx_status_t status) {
  switch (status) {
    case ZX_ERR_PEER_CLOSED:
      return TEEC_ERROR_COMMUNICATION;
    case ZX_ERR_INVALID_ARGS:
      return TEEC_ERROR_BAD_PARAMETERS;
    case ZX_ERR_NOT_SUPPORTED:
      return TEEC_ERROR_NOT_SUPPORTED;
    case ZX_ERR_NO_MEMORY:
      return TEEC_ERROR_OUT_OF_MEMORY;
    case ZX_OK:
      return TEEC_SUCCESS;
  }
  return TEEC_ERROR_GENERIC;
}

constexpr uint32_t ConvertZxToTeecReturnOrigin(fuchsia_tee::ReturnOrigin return_origin) {
  switch (return_origin) {
    case fuchsia_tee::ReturnOrigin::COMMUNICATION:
      return TEEC_ORIGIN_COMMS;
    case fuchsia_tee::ReturnOrigin::TRUSTED_OS:
      return TEEC_ORIGIN_TEE;
    case fuchsia_tee::ReturnOrigin::TRUSTED_APPLICATION:
      return TEEC_ORIGIN_TRUSTED_APP;
    default:
      return TEEC_ORIGIN_API;
  }
}

constexpr size_t CountOperationParameters(const TEEC_Operation& operation) {
  // Find the highest-indexed non-none parameter.
  for (size_t param_num = static_cast<size_t>(TEEC_NUM_PARAMS_MAX); param_num != 0; param_num--) {
    uint32_t param_type = GetParamTypeForIndex(operation.paramTypes, param_num - 1);
    if (param_type != TEEC_NONE) {
      return param_num;
    }
  }

  return 0;
}

void PreprocessValue(uint32_t param_type, const TEEC_Value& teec_value,
                     fuchsia_tee::Parameter* out_zx_param) {
  ZX_DEBUG_ASSERT(out_zx_param);

  fuchsia_tee::Direction direction;
  switch (param_type) {
    case TEEC_VALUE_INPUT:
      direction = fuchsia_tee::Direction::INPUT;
      break;
    case TEEC_VALUE_OUTPUT:
      direction = fuchsia_tee::Direction::OUTPUT;
      break;
    case TEEC_VALUE_INOUT:
      direction = fuchsia_tee::Direction::INOUT;
      break;
    default:
      ZX_PANIC("Unknown param type");
  }

  fuchsia_tee::Value value;
  value.direction = direction;
  if (IsDirectionInput(direction)) {
    // The TEEC_Value type only includes two generic fields, whereas the Fuchsia TEE interface
    // supports three. The c field cannot be used by the TEE Client API.
    value.a = teec_value.a;
    value.b = teec_value.b;
    value.c = 0;
  }
  out_zx_param->set_value(std::move(value));
}

TEEC_Result PreprocessTemporaryMemref(uint32_t param_type,
                                      const TEEC_TempMemoryReference& temp_memory_ref,
                                      fuchsia_tee::Parameter* out_zx_param) {
  ZX_DEBUG_ASSERT(out_zx_param);

  fuchsia_tee::Direction direction;
  switch (param_type) {
    case TEEC_MEMREF_TEMP_INPUT:
      direction = fuchsia_tee::Direction::INPUT;
      break;
    case TEEC_MEMREF_TEMP_OUTPUT:
      direction = fuchsia_tee::Direction::OUTPUT;
      break;
    case TEEC_MEMREF_TEMP_INOUT:
      direction = fuchsia_tee::Direction::INOUT;
      break;
    default:
      ZX_PANIC("TEE Client API Unknown parameter type\n");
  }

  zx::vmo vmo;

  if (temp_memory_ref.buffer) {
    // We either have data to input or have a buffer to output data to, so create a VMO for it.
    zx_status_t status = zx::vmo::create(temp_memory_ref.size, 0, &vmo);
    if (status != ZX_OK) {
      return ConvertStatusToResult(status);
    }

    // If the memory reference is used as an input, then we must copy the data from the user
    // provided buffer into the VMO. There is no need to do this for parameters that are output
    // only.
    if (IsDirectionInput(direction)) {
      status = vmo.write(temp_memory_ref.buffer, 0, temp_memory_ref.size);
      if (status != ZX_OK) {
        return ConvertStatusToResult(status);
      }
    }
  }

  fuchsia_tee::Buffer buffer;
  buffer.direction = direction;
  buffer.vmo = std::move(vmo);
  buffer.offset = 0;
  buffer.size = temp_memory_ref.size;
  out_zx_param->set_buffer(std::move(buffer));
  return TEEC_SUCCESS;
}

TEEC_Result PreprocessWholeMemref(const TEEC_RegisteredMemoryReference& memory_ref,
                                  fuchsia_tee::Parameter* out_zx_param) {
  ZX_DEBUG_ASSERT(out_zx_param);

  if (!memory_ref.parent) {
    return TEEC_ERROR_BAD_PARAMETERS;
  }

  TEEC_SharedMemory* shared_mem = memory_ref.parent;
  fuchsia_tee::Direction direction;
  if (IsSharedMemFlagInOut(shared_mem->flags)) {
    direction = fuchsia_tee::Direction::INOUT;
  } else if (shared_mem->flags & TEEC_MEM_INPUT) {
    direction = fuchsia_tee::Direction::INPUT;
  } else if (shared_mem->flags & TEEC_MEM_OUTPUT) {
    direction = fuchsia_tee::Direction::OUTPUT;
  } else {
    return TEEC_ERROR_BAD_PARAMETERS;
  }

  zx::vmo vmo;
  zx_status_t status = zx::unowned_vmo(shared_mem->imp.vmo)->duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo);
  if (status != ZX_OK) {
    return ConvertStatusToResult(status);
  }

  fuchsia_tee::Buffer buffer;
  buffer.direction = direction;
  buffer.vmo = std::move(vmo);
  buffer.offset = 0;
  buffer.size = shared_mem->size;
  out_zx_param->set_buffer(std::move(buffer));
  return TEEC_SUCCESS;
}

TEEC_Result PreprocessPartialMemref(uint32_t param_type,
                                    const TEEC_RegisteredMemoryReference& memory_ref,
                                    fuchsia_tee::Parameter* out_zx_param) {
  ZX_DEBUG_ASSERT(out_zx_param);

  if (!memory_ref.parent) {
    return TEEC_ERROR_BAD_PARAMETERS;
  }

  uint32_t expected_shm_flags = 0;
  fuchsia_tee::Direction direction;
  switch (param_type) {
    case TEEC_MEMREF_PARTIAL_INPUT:
      expected_shm_flags = TEEC_MEM_INPUT;
      direction = fuchsia_tee::Direction::INPUT;
      break;
    case TEEC_MEMREF_PARTIAL_OUTPUT:
      expected_shm_flags = TEEC_MEM_OUTPUT;
      direction = fuchsia_tee::Direction::OUTPUT;
      break;
    case TEEC_MEMREF_PARTIAL_INOUT:
      expected_shm_flags = TEEC_MEM_INPUT | TEEC_MEM_OUTPUT;
      direction = fuchsia_tee::Direction::INOUT;
      break;
    default:
      ZX_DEBUG_ASSERT(param_type == TEEC_MEMREF_PARTIAL_INPUT ||
                      param_type == TEEC_MEMREF_PARTIAL_OUTPUT ||
                      param_type == TEEC_MEMREF_PARTIAL_INOUT);
  }

  TEEC_SharedMemory* shared_mem = memory_ref.parent;

  if ((shared_mem->flags & expected_shm_flags) != expected_shm_flags) {
    return TEEC_ERROR_BAD_PARAMETERS;
  }

  zx::vmo vmo;
  zx_status_t status = zx::unowned_vmo(shared_mem->imp.vmo)->duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo);
  if (status != ZX_OK) {
    return ConvertStatusToResult(status);
  }

  fuchsia_tee::Buffer buffer;
  buffer.direction = direction;
  buffer.vmo = std::move(vmo);
  buffer.offset = memory_ref.offset;
  buffer.size = memory_ref.size;
  out_zx_param->set_buffer(std::move(buffer));
  return TEEC_SUCCESS;
}

TEEC_Result PreprocessOperation(const TEEC_Operation* operation,
                                fuchsia_tee::ParameterSet* out_parameter_set) {
  if (!operation) {
    return TEEC_SUCCESS;
  }

  size_t num_params = CountOperationParameters(*operation);

  TEEC_Result rc = TEEC_SUCCESS;
  for (size_t i = 0; i < num_params; i++) {
    uint32_t param_type = GetParamTypeForIndex(operation->paramTypes, i);

    switch (param_type) {
      case TEEC_NONE:
        out_parameter_set->parameters[i].set_empty(fuchsia_tee::Empty{});
        break;
      case TEEC_VALUE_INPUT:
      case TEEC_VALUE_OUTPUT:
      case TEEC_VALUE_INOUT:
        PreprocessValue(param_type, operation->params[i].value, &out_parameter_set->parameters[i]);
        break;
      case TEEC_MEMREF_TEMP_INPUT:
      case TEEC_MEMREF_TEMP_OUTPUT:
      case TEEC_MEMREF_TEMP_INOUT:
        rc = PreprocessTemporaryMemref(param_type, operation->params[i].tmpref,
                                       &out_parameter_set->parameters[i]);
        break;
      case TEEC_MEMREF_WHOLE:
        rc = PreprocessWholeMemref(operation->params[i].memref, &out_parameter_set->parameters[i]);
        break;
      case TEEC_MEMREF_PARTIAL_INPUT:
      case TEEC_MEMREF_PARTIAL_OUTPUT:
      case TEEC_MEMREF_PARTIAL_INOUT:
        rc = PreprocessPartialMemref(param_type, operation->params[i].memref,
                                     &out_parameter_set->parameters[i]);
        break;
      default:
        rc = TEEC_ERROR_BAD_PARAMETERS;
        break;
    }

    if (rc != TEEC_SUCCESS) {
      return rc;
    }
  }

  out_parameter_set->count = static_cast<uint16_t>(num_params);

  return rc;
}

TEEC_Result PostprocessValue(uint32_t param_type, const fuchsia_tee::Parameter& zx_param,
                             TEEC_Value* out_teec_value) {
  ZX_DEBUG_ASSERT(out_teec_value);
  ZX_DEBUG_ASSERT(param_type == TEEC_VALUE_INPUT || param_type == TEEC_VALUE_OUTPUT ||
                  param_type == TEEC_VALUE_INOUT);

  if (zx_param.which() != fuchsia_tee::Parameter::Tag::kValue) {
    return TEEC_ERROR_BAD_PARAMETERS;
  }

  const fuchsia_tee::Value& zx_value = zx_param.value();

  // Validate that the direction of the returned parameter matches the expected.
  if ((param_type == TEEC_VALUE_INPUT) && (zx_value.direction != fuchsia_tee::Direction::INPUT)) {
    return TEEC_ERROR_BAD_PARAMETERS;
  }
  if ((param_type == TEEC_VALUE_OUTPUT) && (zx_value.direction != fuchsia_tee::Direction::OUTPUT)) {
    return TEEC_ERROR_BAD_PARAMETERS;
  }
  if ((param_type == TEEC_VALUE_INOUT) && (zx_value.direction != fuchsia_tee::Direction::INOUT)) {
    return TEEC_ERROR_BAD_PARAMETERS;
  }

  // The TEEC_Value type only includes two generic fields, whereas the Fuchsia TEE interface
  // supports three. The c field cannot be used by the TEE Client API.
  out_teec_value->a = static_cast<uint32_t>(zx_value.a);
  out_teec_value->b = static_cast<uint32_t>(zx_value.b);
  return TEEC_SUCCESS;
}

TEEC_Result PostprocessTemporaryMemref(uint32_t param_type, const fuchsia_tee::Parameter& zx_param,
                                       TEEC_TempMemoryReference* out_temp_memory_ref) {
  ZX_DEBUG_ASSERT(out_temp_memory_ref);
  ZX_DEBUG_ASSERT(param_type == TEEC_MEMREF_TEMP_INPUT || param_type == TEEC_MEMREF_TEMP_OUTPUT ||
                  param_type == TEEC_MEMREF_TEMP_INOUT);

  if (zx_param.which() != fuchsia_tee::Parameter::Tag::kBuffer) {
    return TEEC_ERROR_BAD_PARAMETERS;
  }

  const fuchsia_tee::Buffer& zx_buffer = zx_param.buffer();

  if ((param_type == TEEC_MEMREF_TEMP_INPUT) &&
      (zx_buffer.direction != fuchsia_tee::Direction::INPUT)) {
    return TEEC_ERROR_BAD_PARAMETERS;
  }
  if ((param_type == TEEC_MEMREF_TEMP_OUTPUT) &&
      (zx_buffer.direction != fuchsia_tee::Direction::OUTPUT)) {
    return TEEC_ERROR_BAD_PARAMETERS;
  }
  if ((param_type == TEEC_MEMREF_TEMP_INOUT) &&
      (zx_buffer.direction != fuchsia_tee::Direction::INOUT)) {
    return TEEC_ERROR_BAD_PARAMETERS;
  }

  TEEC_Result rc = TEEC_SUCCESS;
  if (IsDirectionOutput(zx_buffer.direction)) {
    // For output buffers, if we don't have enough space in the temporary memory reference to
    // copy the data out, we still need to update the size to indicate to the user how large of
    // a buffer they need to perform the requested operation.
    if (out_temp_memory_ref->buffer && out_temp_memory_ref->size >= zx_buffer.size) {
      zx_status_t status =
          zx_buffer.vmo.read(out_temp_memory_ref->buffer, zx_buffer.offset, zx_buffer.size);
      rc = ConvertStatusToResult(status);
    }
    out_temp_memory_ref->size = zx_buffer.size;
  }

  return rc;
}

TEEC_Result PostprocessWholeMemref(const fuchsia_tee::Parameter& zx_param,
                                   TEEC_RegisteredMemoryReference* out_memory_ref) {
  ZX_DEBUG_ASSERT(out_memory_ref);
  ZX_DEBUG_ASSERT(out_memory_ref->parent);

  if (zx_param.which() != fuchsia_tee::Parameter::Tag::kBuffer) {
    return TEEC_ERROR_BAD_PARAMETERS;
  }

  const fuchsia_tee::Buffer& zx_buffer = zx_param.buffer();

  if (IsDirectionOutput(zx_buffer.direction)) {
    out_memory_ref->size = zx_buffer.size;
  }

  return TEEC_SUCCESS;
}

TEEC_Result PostprocessPartialMemref(uint32_t param_type, const fuchsia_tee::Parameter& zx_param,
                                     TEEC_RegisteredMemoryReference* out_memory_ref) {
  ZX_DEBUG_ASSERT(out_memory_ref);
  ZX_DEBUG_ASSERT(param_type == TEEC_MEMREF_PARTIAL_INPUT ||
                  param_type == TEEC_MEMREF_PARTIAL_OUTPUT ||
                  param_type == TEEC_MEMREF_PARTIAL_INOUT);

  if (zx_param.which() != fuchsia_tee::Parameter::Tag::kBuffer) {
    return TEEC_ERROR_BAD_PARAMETERS;
  }

  const fuchsia_tee::Buffer& zx_buffer = zx_param.buffer();

  if ((param_type == TEEC_MEMREF_PARTIAL_INPUT) &&
      (zx_buffer.direction != fuchsia_tee::Direction::INPUT)) {
    return TEEC_ERROR_BAD_PARAMETERS;
  }
  if ((param_type == TEEC_MEMREF_PARTIAL_OUTPUT) &&
      (zx_buffer.direction != fuchsia_tee::Direction::OUTPUT)) {
    return TEEC_ERROR_BAD_PARAMETERS;
  }
  if ((param_type == TEEC_MEMREF_PARTIAL_INOUT) &&
      (zx_buffer.direction != fuchsia_tee::Direction::INOUT)) {
    return TEEC_ERROR_BAD_PARAMETERS;
  }

  if (IsDirectionOutput(zx_buffer.direction)) {
    out_memory_ref->size = zx_buffer.size;
  }

  return TEEC_SUCCESS;
}

TEEC_Result PostprocessOperation(fuchsia_tee::ParameterSet* parameter_set,
                                 TEEC_Operation* out_operation) {
  if (!out_operation) {
    return TEEC_SUCCESS;
  }

  size_t num_params = CountOperationParameters(*out_operation);

  TEEC_Result rc = TEEC_SUCCESS;
  for (size_t i = 0; i < num_params; i++) {
    uint32_t param_type = GetParamTypeForIndex(out_operation->paramTypes, i);

    // This check catches the case where we did not receive all the parameters we expected.
    if (i >= parameter_set->count && param_type != TEEC_NONE) {
      rc = TEEC_ERROR_BAD_PARAMETERS;
      break;
    }

    switch (param_type) {
      case TEEC_NONE:
        if (parameter_set->parameters[i].which() != fuchsia_tee::Parameter::Tag::kEmpty) {
          rc = TEEC_ERROR_BAD_PARAMETERS;
        }
        break;
      case TEEC_VALUE_INPUT:
      case TEEC_VALUE_OUTPUT:
      case TEEC_VALUE_INOUT:
        rc = PostprocessValue(param_type, parameter_set->parameters[i],
                              &out_operation->params[i].value);
        break;
      case TEEC_MEMREF_TEMP_INPUT:
      case TEEC_MEMREF_TEMP_OUTPUT:
      case TEEC_MEMREF_TEMP_INOUT:
        rc = PostprocessTemporaryMemref(param_type, parameter_set->parameters[i],
                                        &out_operation->params[i].tmpref);
        break;
      case TEEC_MEMREF_WHOLE:
        rc = PostprocessWholeMemref(parameter_set->parameters[i], &out_operation->params[i].memref);
        break;
      case TEEC_MEMREF_PARTIAL_INPUT:
      case TEEC_MEMREF_PARTIAL_OUTPUT:
      case TEEC_MEMREF_PARTIAL_INOUT:
        rc = PostprocessPartialMemref(param_type, parameter_set->parameters[i],
                                      &out_operation->params[i].memref);
        break;
      default:
        rc = TEEC_ERROR_BAD_PARAMETERS;
    }

    if (rc != TEEC_SUCCESS) {
      break;
    }
  }

  // This check catches the case where we received more parameters than we expected.
  for (size_t i = num_params; i < parameter_set->count; i++) {
    if (parameter_set->parameters[i].which() != fuchsia_tee::Parameter::Tag::kEmpty) {
      return TEEC_ERROR_BAD_PARAMETERS;
    }
  }

  return rc;
}

zx_status_t ConnectToService(zx::channel* tee_channel) {
  ZX_DEBUG_ASSERT(tee_channel);

  zx::channel client_channel;
  zx::channel server_channel;
  zx_status_t status = zx::channel::create(0, &client_channel, &server_channel);
  if (status != ZX_OK) {
    return status;
  }

  status = fdio_service_connect(kTeeServicePath.data(), server_channel.release());
  if (status != ZX_OK) {
    return status;
  }

  *tee_channel = std::move(client_channel);
  return ZX_OK;
}

// Connects the client directly to the TEE Driver.
//
// This is a temporary measure to allow clients that come up before component services to still
// access the TEE. This requires that the client has access to the TEE device class. Additionally,
// the client's entire context will not have any filesystem support, so if the client sends a
// command to a trusted application that then needs persistent storage to complete, the persistent
// storage request will be rejected by the driver.
zx_status_t ConnectToDriver(const char* tee_device, zx::channel* tee_channel) {
  ZX_DEBUG_ASSERT(tee_device);
  ZX_DEBUG_ASSERT(tee_channel);

  int fd = open(tee_device, O_RDWR);
  if (fd < 0) {
    return ZX_ERR_NOT_FOUND;
  }

  zx::channel connector_channel;
  zx_status_t status = fdio_get_service_handle(fd, connector_channel.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }

  zx::channel client_channel;
  zx::channel server_channel;
  status = zx::channel::create(0, &client_channel, &server_channel);
  if (status != ZX_OK) {
    return status;
  }

  // Connect to the device interface with no supporting service provider
  fuchsia_hardware_tee::DeviceConnector::SyncClient client(std::move(connector_channel));
  auto result = client.ConnectTee(zx::channel(ZX_HANDLE_INVALID), std::move(server_channel));
  status = result.status();
  if (status != ZX_OK) {
    return status;
  }

  *tee_channel = std::move(client_channel);
  return ZX_OK;
}
}  // namespace

__EXPORT
TEEC_Result TEEC_InitializeContext(const char* name, TEEC_Context* context) {
  if (!context) {
    return TEEC_ERROR_BAD_PARAMETERS;
  }

  zx_status_t status;
  zx::channel tee_channel;
  std::string_view name_view(name != nullptr ? name : "");

  if (name == nullptr || kTeeServicePath == name_view) {
    status = ConnectToService(&tee_channel);
    if (status != ZX_OK) {
      return TEEC_ERROR_COMMUNICATION;
    }
  } else if (name_view.compare(0, kTeeDevClass.size(), kTeeDevClass) == 0) {
    // TODO: use `std::string_view::starts_with()` when C++20 is available.

    // The client has specified a direct connection to some TEE device
    // See comments on `ConnectToDriver()` for details.
    status = ConnectToDriver(name, &tee_channel);
    if (status != ZX_OK) {
      if (status == ZX_ERR_NOT_FOUND) {
        return TEEC_ERROR_ITEM_NOT_FOUND;
      } else {
        return TEEC_ERROR_COMMUNICATION;
      }
    }
  } else {
    return TEEC_ERROR_BAD_PARAMETERS;
  }

  if (!IsGlobalPlatformCompliant(zx::unowned_channel(tee_channel))) {
    // This API is only designed to support TEEs that are Global Platform compliant.
    return TEEC_ERROR_NOT_SUPPORTED;
  }
  context->imp.tee_channel = tee_channel.release();

  return TEEC_SUCCESS;
}

__EXPORT
void TEEC_FinalizeContext(TEEC_Context* context) {
  if (context) {
    zx_handle_close(context->imp.tee_channel);
    context->imp.tee_channel = ZX_HANDLE_INVALID;
  }
}

__EXPORT
TEEC_Result TEEC_RegisterSharedMemory(TEEC_Context* context, TEEC_SharedMemory* sharedMem) {
  /* This function is supposed to register an existing buffer for use as shared memory. We don't
   * have a way of discovering the VMO handle for an arbitrary address, so implementing this would
   * require an extra VMO that would be copied into at invocation. Since we currently don't have
   * any use cases for this function and TEEC_AllocateSharedMemory should be the preferred method
   * of acquiring shared memory, we're going to leave this unimplemented for now. */
  return TEEC_ERROR_NOT_IMPLEMENTED;
}

__EXPORT
TEEC_Result TEEC_AllocateSharedMemory(TEEC_Context* context, TEEC_SharedMemory* sharedMem) {
  if (!context || !sharedMem) {
    return TEEC_ERROR_BAD_PARAMETERS;
  }

  if (sharedMem->flags & ~(TEEC_MEM_INPUT | TEEC_MEM_OUTPUT)) {
    return TEEC_ERROR_BAD_PARAMETERS;
  }

  std::memset(&sharedMem->imp, 0, sizeof(sharedMem->imp));

  size_t size = sharedMem->size;

  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(size, 0, &vmo);
  if (status != ZX_OK) {
    return ConvertStatusToResult(status);
  }

  uintptr_t mapped_addr;
  status =
      zx::vmar::root_self()->map(0, vmo, 0, size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &mapped_addr);
  if (status != ZX_OK) {
    return ConvertStatusToResult(status);
  }

  sharedMem->buffer = reinterpret_cast<void*>(mapped_addr);
  sharedMem->imp.vmo = vmo.release();
  sharedMem->imp.mapped_addr = mapped_addr;
  sharedMem->imp.mapped_size = size;

  return TEEC_SUCCESS;
}

__EXPORT
void TEEC_ReleaseSharedMemory(TEEC_SharedMemory* sharedMem) {
  if (!sharedMem) {
    return;
  }
  zx::vmar::root_self()->unmap(sharedMem->imp.mapped_addr, sharedMem->imp.mapped_size);
  zx_handle_close(sharedMem->imp.vmo);
  sharedMem->imp.vmo = ZX_HANDLE_INVALID;
}

__EXPORT
TEEC_Result TEEC_OpenSession(TEEC_Context* context, TEEC_Session* session,
                             const TEEC_UUID* destination, uint32_t connectionMethod,
                             const void* connectionData, TEEC_Operation* operation,
                             uint32_t* returnOrigin) {
  if (!context || !session || !destination) {
    if (returnOrigin) {
      *returnOrigin = TEEC_ORIGIN_API;
    }
    return TEEC_ERROR_BAD_PARAMETERS;
  }

  if (connectionMethod != TEEC_LOGIN_PUBLIC) {
    // TODO(rjascani): Investigate whether non public login is needed.
    if (returnOrigin) {
      *returnOrigin = TEEC_ORIGIN_API;
    }
    return TEEC_ERROR_NOT_IMPLEMENTED;
  }

  fuchsia_tee::Uuid trusted_app;
  ConvertTeecUuidToZxUuid(*destination, &trusted_app);

  fuchsia_tee::ParameterSet parameter_set;
  TEEC_Result processing_rc = PreprocessOperation(operation, &parameter_set);
  if (processing_rc != TEEC_SUCCESS) {
    if (returnOrigin) {
      *returnOrigin = TEEC_ORIGIN_COMMS;
    }
    return processing_rc;
  }

  auto result = fuchsia_tee::Device::Call::OpenSession(
      zx::unowned_channel(context->imp.tee_channel), trusted_app, std::move(parameter_set));
  zx_status_t status = result.status();

  if (status != ZX_OK) {
    if (returnOrigin) {
      *returnOrigin = TEEC_ORIGIN_COMMS;
    }
    return ConvertStatusToResult(status);
  }

  uint32_t out_session_id = result->session_id;
  fuchsia_tee::OpResult out_result = std::move(result->op_result);

  // Run post-processing regardless of TEE operation status. The operation was invoked
  // successfully, so the parameter set should be okay to post-process.
  processing_rc = PostprocessOperation(&out_result.parameter_set, operation);

  if (out_result.return_code != TEEC_SUCCESS) {
    // If the TEE operation failed, use that return code above any processing failure codes.
    if (returnOrigin) {
      *returnOrigin = ConvertZxToTeecReturnOrigin(out_result.return_origin);
    }
    return static_cast<uint32_t>(out_result.return_code);
  }
  if (processing_rc != TEEC_SUCCESS) {
    // The TEE operation succeeded but the processing operation failed.
    if (returnOrigin) {
      *returnOrigin = TEEC_ORIGIN_COMMS;
    }
    return processing_rc;
  }

  session->imp.session_id = out_session_id;
  session->imp.context_imp = &context->imp;

  return static_cast<uint32_t>(out_result.return_code);
}

__EXPORT
void TEEC_CloseSession(TEEC_Session* session) {
  if (!session || !session->imp.context_imp) {
    return;
  }

  // TEEC_CloseSession simply swallows errors, so no need to check here.
  fuchsia_tee::Device::Call::CloseSession(
      zx::unowned_channel(session->imp.context_imp->tee_channel), session->imp.session_id);
  session->imp.context_imp = NULL;
}

__EXPORT
TEEC_Result TEEC_InvokeCommand(TEEC_Session* session, uint32_t commandID, TEEC_Operation* operation,
                               uint32_t* returnOrigin) {
  if (!session || !session->imp.context_imp) {
    if (returnOrigin) {
      *returnOrigin = TEEC_ORIGIN_API;
    }
    return TEEC_ERROR_BAD_PARAMETERS;
  }

  fuchsia_tee::ParameterSet parameter_set;
  TEEC_Result processing_rc = PreprocessOperation(operation, &parameter_set);
  if (processing_rc != TEEC_SUCCESS) {
    if (returnOrigin) {
      *returnOrigin = TEEC_ORIGIN_COMMS;
    }
    return processing_rc;
  }

  auto result = fuchsia_tee::Device::Call::InvokeCommand(
      zx::unowned_channel(session->imp.context_imp->tee_channel), session->imp.session_id,
      commandID, std::move(parameter_set));
  zx_status_t status = result.status();
  if (status != ZX_OK) {
    if (returnOrigin) {
      *returnOrigin = TEEC_ORIGIN_COMMS;
    }
    return ConvertStatusToResult(status);
  }

  auto& out_result = result->op_result;

  // Run post-processing regardless of TEE operation status. The operation was invoked
  // successfully, so the parameter set should be okay to post-process.
  processing_rc = PostprocessOperation(&out_result.parameter_set, operation);

  if (out_result.return_code != TEEC_SUCCESS) {
    // If the TEE operation failed, use that return code above any processing failure codes.
    if (returnOrigin) {
      *returnOrigin = ConvertZxToTeecReturnOrigin(out_result.return_origin);
    }
    return static_cast<uint32_t>(out_result.return_code);
  }
  if (processing_rc != TEEC_SUCCESS) {
    // The TEE operation succeeded but the processing operation failed.
    if (returnOrigin) {
      *returnOrigin = TEEC_ORIGIN_COMMS;
    }
    return processing_rc;
  }

  return static_cast<uint32_t>(out_result.return_code);
}

__EXPORT
void TEEC_RequestCancellation(TEEC_Operation* operation) {}
