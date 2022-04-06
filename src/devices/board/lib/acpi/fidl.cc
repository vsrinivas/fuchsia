// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/lib/acpi/fidl.h"

#include <fidl/fuchsia.hardware.acpi/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/zircon-internal/align.h>

#include "src/devices/board/lib/acpi/device.h"

namespace acpi {

namespace {

ACPI_OBJECT_TYPE FidlTypeToAcpiType(fuchsia_hardware_acpi::wire::ObjectType type) {
  using ObjectType = fuchsia_hardware_acpi::wire::ObjectType;
  switch (type) {
    case ObjectType::kAny:
      return ACPI_TYPE_ANY;
    case ObjectType::kBuffer:
      return ACPI_TYPE_BUFFER;
    case ObjectType::kBufferField:
      return ACPI_TYPE_BUFFER_FIELD;
    case ObjectType::kDebugObject:
      return ACPI_TYPE_DEBUG_OBJECT;
    case ObjectType::kDevice:
      return ACPI_TYPE_DEVICE;
    case ObjectType::kEvent:
      return ACPI_TYPE_EVENT;
    case ObjectType::kFieldUnit:
      return ACPI_TYPE_FIELD_UNIT;
    case ObjectType::kInteger:
      return ACPI_TYPE_INTEGER;
    case ObjectType::kMethod:
      return ACPI_TYPE_METHOD;
    case ObjectType::kMutex:
      return ACPI_TYPE_MUTEX;
    case ObjectType::kOperationRegion:
      return ACPI_TYPE_REGION;
    case ObjectType::kPackage:
      return ACPI_TYPE_PACKAGE;
    case ObjectType::kPowerResource:
      return ACPI_TYPE_POWER;
    case ObjectType::kString:
      return ACPI_TYPE_STRING;
    case ObjectType::kThermalZone:
      return ACPI_TYPE_THERMAL;
  }
  zxlogf(ERROR, "Unknown ACPI object type %d", int(type));
  return ACPI_TYPE_ANY;
}

fuchsia_hardware_acpi::wire::ObjectType AcpiTypeToFidlType(ACPI_OBJECT_TYPE type) {
  using ObjectType = fuchsia_hardware_acpi::wire::ObjectType;
  switch (type) {
    case ACPI_TYPE_ANY:
      return ObjectType::kAny;
    case ACPI_TYPE_BUFFER:
      return ObjectType::kBuffer;
    case ACPI_TYPE_BUFFER_FIELD:
      return ObjectType::kBufferField;
    case ACPI_TYPE_DEBUG_OBJECT:
      return ObjectType::kDebugObject;
    case ACPI_TYPE_DEVICE:
      return ObjectType::kDevice;
    case ACPI_TYPE_EVENT:
      return ObjectType::kEvent;
    case ACPI_TYPE_FIELD_UNIT:
      return ObjectType::kFieldUnit;
    case ACPI_TYPE_INTEGER:
      return ObjectType::kInteger;
    case ACPI_TYPE_METHOD:
      return ObjectType::kMethod;
    case ACPI_TYPE_MUTEX:
      return ObjectType::kMutex;
    case ACPI_TYPE_REGION:
      return ObjectType::kOperationRegion;
    case ACPI_TYPE_PACKAGE:
      return ObjectType::kPackage;
    case ACPI_TYPE_POWER:
      return ObjectType::kPowerResource;
    case ACPI_TYPE_STRING:
      return ObjectType::kString;
    case ACPI_TYPE_THERMAL:
      return ObjectType::kThermalZone;
  }
  zxlogf(ERROR, "Unknown ACPI object type %d", type);
  return ObjectType::Unknown();
}

}  // namespace

EvaluateObjectFidlHelper EvaluateObjectFidlHelper::FromRequest(acpi::Acpi* acpi, ACPI_HANDLE device,
                                                               EvaluateObjectRequestView& request) {
  std::string path(request->path.data(), request->path.size());
  return EvaluateObjectFidlHelper(acpi, device, std::move(path), request->mode,
                                  request->parameters);
}

acpi::status<fuchsia_hardware_acpi::wire::DeviceEvaluateObjectResult>
EvaluateObjectFidlHelper::Evaluate(fidl::AnyArena& alloc) {
  auto path = ValidateAndLookupPath(request_path_.data());
  if (path.is_error()) {
    return path.take_error();
  }

  auto result = DecodeParameters(request_params_);
  if (result.is_error()) {
    return result.take_error();
  }

  auto value = acpi_->EvaluateObject(nullptr, path->data(), result.value());
  if (value.is_error()) {
    return value.take_error();
  }

  acpi::status<fuchsia_hardware_acpi::wire::DeviceEvaluateObjectResult> fidl_val;
  switch (mode_) {
    using fuchsia_hardware_acpi::wire::EvaluateObjectMode;
    case EvaluateObjectMode::kPlainObject: {
      fidl_val = EncodeReturnValue(alloc, value.value().get());
      break;
    }
    case EvaluateObjectMode::kParseResources: {
      fidl_val = EncodeResourcesReturnValue(alloc, value.value().get());
      break;
    }
    default: {
      fidl_val = acpi::error(AE_NOT_IMPLEMENTED);
      break;
    }
  }

  if (fidl_val.is_error()) {
    return fidl_val.take_error();
  }

  return acpi::ok(std::move(fidl_val.value()));
}

acpi::status<std::string> EvaluateObjectFidlHelper::ValidateAndLookupPath(const char* request_path,
                                                                          ACPI_HANDLE* hnd) {
  auto result = acpi_->GetHandle(device_handle_, request_path);
  if (result.is_error()) {
    return result.take_error();
  }

  auto my_path = acpi_->GetPath(device_handle_);
  if (my_path.is_error()) {
    return my_path.take_error();
  }

  ACPI_HANDLE target = result.value();
  auto abs_path = acpi_->GetPath(target);
  if (abs_path.is_error()) {
    return abs_path.take_error();
  }

  if (!strncmp(my_path->data(), abs_path->data(), my_path->size())) {
    if (hnd) {
      *hnd = target;
    }
    return acpi::ok(std::move(abs_path.value()));
  }

  return acpi::error(AE_ACCESS);
}

acpi::status<std::vector<ACPI_OBJECT>> EvaluateObjectFidlHelper::DecodeParameters(
    fidl::VectorView<fuchsia_hardware_acpi::wire::Object>& request_params) {
  std::vector<ACPI_OBJECT> result(request_params.count());
  size_t i = 0;
  for (auto& param : request_params) {
    auto status = DecodeObject(param, &result[i]);
    if (status.is_error()) {
      return status.take_error();
    }
    i++;
  }
  return acpi::ok(std::move(result));
}

acpi::status<fuchsia_hardware_acpi::wire::DeviceEvaluateObjectResult>
EvaluateObjectFidlHelper::EncodeResourcesReturnValue(fidl::AnyArena& alloc, ACPI_OBJECT* value) {
  if (value->Type != ACPI_TYPE_BUFFER) {
    return acpi::error(AE_BAD_VALUE);
  }

  std::vector<fuchsia_hardware_acpi::wire::Resource> resources;
  auto resource = acpi_->BufferToResource(cpp20::span(value->Buffer.Pointer, value->Buffer.Length));
  if (resource.is_error()) {
    return resource.take_error();
  }

  ACPI_RESOURCE* cur = resource.value().get();
  while (true) {
    if (cur->Type > ACPI_RESOURCE_TYPE_MAX || cur->Length == 0) {
      return acpi::error(AE_AML_BAD_RESOURCE_LENGTH);
    }
    if (cur->Type == ACPI_RESOURCE_TYPE_END_TAG) {
      break;
    }

    acpi::status<fuchsia_hardware_acpi::wire::Resource> resource;
    switch (cur->Type) {
      case ACPI_RESOURCE_TYPE_ADDRESS64:
        resource = EncodeMmioResource(alloc, cur);
        break;
      default:
        resource = acpi::error(AE_NOT_IMPLEMENTED);
        break;
    }
    if (resource.is_ok()) {
      resources.emplace_back(std::move(resource.value()));
    } else {
      zxlogf(WARNING, "Error encoding resource (type 0x%x) to FIDL: 0x%x, ignoring.", cur->Type,
             resource.error_value());
    }

    // Advance to the next resource in the list.
    cur = reinterpret_cast<ACPI_RESOURCE*>(reinterpret_cast<uint8_t*>(cur) + cur->Length);
  }

  fidl::VectorView<fuchsia_hardware_acpi::wire::Resource> result(alloc, resources.size());
  for (size_t i = 0; i < resources.size(); i++) {
    result[i] = std::move(resources[i]);
  }
  auto encoded = fuchsia_hardware_acpi::wire::EncodedObject::WithResources(alloc, result);
  fuchsia_hardware_acpi::wire::DeviceEvaluateObjectResponse response;
  response.result = std::move(encoded);
  auto ret = fuchsia_hardware_acpi::wire::DeviceEvaluateObjectResult::WithResponse(
      alloc, std::move(response));
  return acpi::ok(std::move(ret));
}

acpi::status<fuchsia_hardware_acpi::wire::Resource> EvaluateObjectFidlHelper::EncodeMmioResource(
    fidl::AnyArena& alloc, ACPI_RESOURCE* resource) {
  zx_paddr_t paddr;
  size_t size;
  switch (resource->Type) {
    case ACPI_RESOURCE_TYPE_ADDRESS64:
      paddr = resource->Data.Address64.Address.Minimum;
      size = resource->Data.Address64.Address.AddressLength;
      break;
    default:
      return acpi::error(AE_NOT_IMPLEMENTED);
  }

  zx::vmo vmo;
  zx_paddr_t page_start = ZX_ROUNDDOWN(paddr, zx_system_get_page_size());
  size_t page_offset = (paddr & (zx_system_get_page_size() - 1));
  size_t page_size = ZX_ROUNDUP(page_offset + size, zx_system_get_page_size());

  zx_status_t st =
      zx_vmo_create_physical(mmio_resource_, page_start, page_size, vmo.reset_and_get_address());
  if (st != ZX_OK) {
    zxlogf(ERROR, "vmo_create_physical failed (0x%lx len=0x%lx): %s", page_start, page_size,
           zx_status_get_string(st));
    return acpi::error(AE_ERROR);
  }
  fuchsia_mem::wire::Range range{
      .vmo = std::move(vmo),
      .offset = page_offset,
      .size = size,
  };
  return acpi::ok(fuchsia_hardware_acpi::wire::Resource::WithMmio(alloc, std::move(range)));
}

acpi::status<fuchsia_hardware_acpi::wire::DeviceEvaluateObjectResult>
EvaluateObjectFidlHelper::EncodeReturnValue(fidl::AnyArena& alloc, ACPI_OBJECT* value) {
  // TODO(fxbug.dev/79172): put the data in a VMO if it's too big.
  fuchsia_hardware_acpi::wire::EncodedObject encoded;
  if (value != nullptr) {
    auto result = EncodeObject(alloc, value);
    if (result.is_error()) {
      return result.take_error();
    }

    encoded = fuchsia_hardware_acpi::wire::EncodedObject::WithObject(alloc, result.value());
  }

  fuchsia_hardware_acpi::wire::DeviceEvaluateObjectResponse response;
  response.result = std::move(encoded);
  return acpi::ok(fuchsia_hardware_acpi::wire::DeviceEvaluateObjectResult::WithResponse(
      alloc, std::move(response)));
}

acpi::status<fuchsia_hardware_acpi::wire::Object> EvaluateObjectFidlHelper::EncodeObject(
    fidl::AnyArena& alloc, ACPI_OBJECT* value) {
  fuchsia_hardware_acpi::wire::Object result;
  switch (value->Type) {
    case ACPI_TYPE_INTEGER: {
      result = fuchsia_hardware_acpi::wire::Object::WithIntegerVal(alloc, value->Integer.Value);
      break;
    }
    case ACPI_TYPE_STRING: {
      fidl::StringView sv;
      sv.Set(alloc, cpp17::string_view(value->String.Pointer, value->String.Length));
      result = fuchsia_hardware_acpi::wire::Object::WithStringVal(alloc, sv);
      break;
    }
    case ACPI_TYPE_PACKAGE: {
      fidl::VectorView<fuchsia_hardware_acpi::wire::Object> view;
      view.Allocate(alloc, value->Package.Count);
      for (size_t i = 0; i < value->Package.Count; i++) {
        auto ret = EncodeObject(alloc, &value->Package.Elements[i]);
        if (ret.is_error()) {
          return ret.take_error();
        }
        view[i] = ret.value();
      }
      fuchsia_hardware_acpi::wire::ObjectList list;
      list.value = view;
      result = fuchsia_hardware_acpi::wire::Object::WithPackageVal(alloc, list);
      break;
    }
    case ACPI_TYPE_BUFFER: {
      fidl::VectorView<uint8_t> data;
      data.Allocate(alloc, value->Buffer.Length);
      memcpy(data.mutable_data(), value->Buffer.Pointer, value->Buffer.Length);
      result = fuchsia_hardware_acpi::wire::Object::WithBufferVal(alloc, data);
      break;
    }
    case ACPI_TYPE_POWER: {
      fuchsia_hardware_acpi::wire::PowerResource power;
      power.resource_order = value->PowerResource.ResourceOrder;
      power.system_level = value->PowerResource.SystemLevel;
      result = fuchsia_hardware_acpi::wire::Object::WithPowerResourceVal(alloc, power);
      break;
    }
    case ACPI_TYPE_PROCESSOR: {
      fuchsia_hardware_acpi::wire::Processor processor;
      processor.id = value->Processor.ProcId;
      processor.pblk_address = value->Processor.PblkAddress;
      processor.pblk_length = value->Processor.PblkLength;
      result = fuchsia_hardware_acpi::wire::Object::WithProcessorVal(alloc, processor);
      break;
    }
    case ACPI_TYPE_LOCAL_REFERENCE: {
      auto handle_path = acpi_->GetPath(value->Reference.Handle);
      if (handle_path.is_error()) {
        return handle_path.take_error();
      }
      auto my_path = acpi_->GetPath(device_handle_);
      if (my_path.is_error()) {
        return my_path.take_error();
      }
      if (strncmp(my_path->data(), handle_path->data(), my_path->size()) != 0) {
        zxlogf(WARNING, "EvaluateObject returned a reference to an external object: %s",
               handle_path->data());
        return acpi::error(AE_ACCESS);
      }

      fuchsia_hardware_acpi::wire::Handle hnd;
      hnd.object_type = AcpiTypeToFidlType(value->Reference.ActualType);
      fidl::StringView sv;
      sv.Set(alloc, cpp17::string_view(handle_path.value()));
      hnd.path = sv;
      result = fuchsia_hardware_acpi::wire::Object::WithReferenceVal(alloc, hnd);
      break;
    }
    default:
      zxlogf(ERROR, "Unexpected return type from EvaluateObject: %d", value->Type);
      return acpi::error(AE_NOT_IMPLEMENTED);
  }
  return acpi::ok(result);
}

acpi::status<> EvaluateObjectFidlHelper::DecodeObject(
    const fuchsia_hardware_acpi::wire::Object& obj, ACPI_OBJECT* out) {
  using Tag = fuchsia_hardware_acpi::wire::Object::Tag;
  switch (obj.Which()) {
    case Tag::kIntegerVal: {
      out->Integer.Type = ACPI_TYPE_INTEGER;
      out->Integer.Value = obj.integer_val();
      break;
    }
    case Tag::kStringVal: {
      if (obj.string_val().size() > std::numeric_limits<uint32_t>::max()) {
        return acpi::error(AE_BAD_VALUE);
      }
      // ACPI strings need to be null terminated. FIDL strings aren't, so we have to make copies
      // of them.
      allocated_strings_.emplace_front(
          std::string(obj.string_val().data(), obj.string_val().size()));
      out->String.Type = ACPI_TYPE_STRING;
      out->String.Length = static_cast<uint32_t>(obj.string_val().size());
      out->String.Pointer = allocated_strings_.front().data();
      break;
    }
    case Tag::kPackageVal: {
      auto& list = obj.package_val().value;
      if (list.count() > std::numeric_limits<uint32_t>::max()) {
        return acpi::error(AE_BAD_VALUE);
      }
      std::vector<ACPI_OBJECT> package(list.count());
      for (size_t i = 0; i < list.count(); i++) {
        auto status = DecodeObject(list[i], &package[i]);
        if (status.is_error()) {
          return status.take_error();
        }
      }
      allocated_packages_.emplace_front(std::move(package));
      out->Package.Type = ACPI_TYPE_PACKAGE;
      out->Package.Count = static_cast<uint32_t>(list.count());
      out->Package.Elements = allocated_packages_.front().data();
      break;
    }
    case Tag::kBufferVal: {
      auto& buffer = obj.buffer_val();
      if (buffer.count() > std::numeric_limits<uint32_t>::max()) {
        return acpi::error(AE_BAD_VALUE);
      }
      out->Buffer.Type = ACPI_TYPE_BUFFER;
      out->Buffer.Length = static_cast<uint32_t>(buffer.count());
      out->Buffer.Pointer = buffer.mutable_data();
      break;
    }
    case Tag::kPowerResourceVal: {
      auto& power = obj.power_resource_val();
      out->PowerResource.Type = ACPI_TYPE_POWER;
      out->PowerResource.ResourceOrder = power.resource_order;
      out->PowerResource.SystemLevel = power.system_level;
      break;
    }
    case Tag::kProcessorVal: {
      auto& processor = obj.processor_val();
      out->Processor.Type = ACPI_TYPE_PROCESSOR;
      out->Processor.PblkAddress = processor.pblk_address;
      out->Processor.PblkLength = processor.pblk_length;
      out->Processor.ProcId = processor.id;
      break;
    }
    case Tag::kReferenceVal: {
      auto& ref = obj.reference_val();
      std::string path(ref.path.data(), ref.path.size());
      ACPI_HANDLE hnd = nullptr;
      auto result = ValidateAndLookupPath(path.data(), &hnd);
      if (result.is_error()) {
        return result.take_error();
      }
      out->Reference.Type = ACPI_TYPE_LOCAL_REFERENCE;
      out->Reference.ActualType = FidlTypeToAcpiType(ref.object_type);
      out->Reference.Handle = hnd;
      break;
    }
    case Tag::kUnknown:
      return acpi::error(AE_NOT_IMPLEMENTED);
  }
  return acpi::ok();
}
}  // namespace acpi
