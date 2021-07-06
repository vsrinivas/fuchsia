// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/drivers/x86/acpi/fidl.h"

#include <fuchsia/hardware/acpi/llcpp/fidl.h>
#include <lib/ddk/debug.h>

#include "lib/fidl/llcpp/message.h"

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
  return EvaluateObjectFidlHelper(acpi, device, std::move(path), request->parameters);
}

acpi::status<fuchsia_hardware_acpi::wire::DeviceEvaluateObjectResult>
EvaluateObjectFidlHelper::Evaluate(fidl::AnyAllocator& alloc) {
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

  auto fidl_val = EncodeReturnValue(alloc, value.value().get());
  if (fidl_val.is_error()) {
    return fidl_val.take_error();
  }

  return acpi::ok(fidl_val.value());
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
EvaluateObjectFidlHelper::EncodeReturnValue(fidl::AnyAllocator& alloc, ACPI_OBJECT* value) {
  auto result = EncodeObject(alloc, value);
  if (result.is_error()) {
    return result.take_error();
  }

  // TODO(fxbug.dev/79172): put the data in a VMO if it's too big.
  fuchsia_hardware_acpi::wire::EncodedObject encoded;
  encoded.set_object(alloc, result.value());

  fuchsia_hardware_acpi::wire::DeviceEvaluateObjectResponse response;
  response.result = encoded;
  fuchsia_hardware_acpi::wire::DeviceEvaluateObjectResult ret;
  ret.set_response(alloc, response);
  return acpi::ok(ret);
}

acpi::status<fuchsia_hardware_acpi::wire::Object> EvaluateObjectFidlHelper::EncodeObject(
    fidl::AnyAllocator& alloc, ACPI_OBJECT* value) {
  fuchsia_hardware_acpi::wire::Object result;
  switch (value->Type) {
    case ACPI_TYPE_INTEGER: {
      result.set_integer_val(alloc, value->Integer.Value);
      break;
    }
    case ACPI_TYPE_STRING: {
      fidl::StringView sv;
      sv.Set(alloc, cpp17::string_view(value->String.Pointer, value->String.Length));
      result.set_string_val(alloc, sv);
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
      result.set_package_val(alloc, list);
      break;
    }
    case ACPI_TYPE_BUFFER: {
      fidl::VectorView<uint8_t> data;
      data.Allocate(alloc, value->Buffer.Length);
      memcpy(data.mutable_data(), value->Buffer.Pointer, value->Buffer.Length);
      result.set_buffer_val(alloc, data);
      break;
    }
    case ACPI_TYPE_POWER: {
      fuchsia_hardware_acpi::wire::PowerResource power;
      power.resource_order = value->PowerResource.ResourceOrder;
      power.system_level = value->PowerResource.SystemLevel;
      result.set_power_resource_val(alloc, power);
      break;
    }
    case ACPI_TYPE_PROCESSOR: {
      fuchsia_hardware_acpi::wire::Processor processor;
      processor.id = value->Processor.ProcId;
      processor.pblk_address = value->Processor.PblkAddress;
      processor.pblk_length = value->Processor.PblkLength;
      result.set_processor_val(alloc, processor);
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
      result.set_reference_val(alloc, hnd);
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
  switch (obj.which()) {
    case Tag::kIntegerVal: {
      out->Integer.Type = ACPI_TYPE_INTEGER;
      out->Integer.Value = obj.integer_val();
      break;
    }
    case Tag::kStringVal: {
      // ACPI strings need to be null terminated. FIDL strings aren't, so we have to make copies
      // of them.
      allocated_strings_.emplace_front(
          std::string(obj.string_val().data(), obj.string_val().size()));
      out->String.Type = ACPI_TYPE_STRING;
      out->String.Length = obj.string_val().size();
      out->String.Pointer = allocated_strings_.front().data();
      break;
    }
    case Tag::kPackageVal: {
      auto& list = obj.package_val().value;
      std::vector<ACPI_OBJECT> package(list.count());
      for (size_t i = 0; i < list.count(); i++) {
        auto status = DecodeObject(list[i], &package[i]);
        if (status.is_error()) {
          return status.take_error();
        }
      }
      allocated_packages_.emplace_front(std::move(package));
      out->Package.Type = ACPI_TYPE_PACKAGE;
      out->Package.Count = list.count();
      out->Package.Elements = allocated_packages_.front().data();
      break;
    }
    case Tag::kBufferVal: {
      auto& buffer = obj.buffer_val();
      out->Buffer.Type = ACPI_TYPE_BUFFER;
      out->Buffer.Length = buffer.count();
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
