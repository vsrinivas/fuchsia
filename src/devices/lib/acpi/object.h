// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_ACPI_OBJECT_H_
#define SRC_DEVICES_LIB_ACPI_OBJECT_H_

#include <fidl/fuchsia.hardware.acpi/cpp/wire.h>

namespace acpi {

using Processor = fuchsia_hardware_acpi::wire::Processor;
using PowerResource = fuchsia_hardware_acpi::wire::PowerResource;
using Status = fuchsia_hardware_acpi::wire::Status;

struct Handle {
  fuchsia_hardware_acpi::wire::ObjectType object_type;
  std::string path;
};

// Owned version of |fuchsia_hardware_acpi::wire::Object|.
class Object {
 public:
  // Initialize this object when the FIDL method returned an error.
  explicit Object(Status status) : value_(status) {}
  // Initialize this object with a FIDL ACPI object.
  explicit Object(const fuchsia_hardware_acpi::wire::Object& object) {
    using Tag = fuchsia_hardware_acpi::wire::Object::Tag;
    switch (object.Which()) {
      case Tag::kIntegerVal: {
        value_ = object.integer_val();
        break;
      }
      case Tag::kStringVal: {
        value_ = std::string(object.string_val().data(), object.string_val().size());
        break;
      }
      case Tag::kBufferVal: {
        std::vector<uint8_t> val(object.buffer_val().count());
        memcpy(val.data(), object.buffer_val().data(), val.size());
        break;
      }
      case Tag::kPackageVal: {
        std::vector<Object> objects;
        for (auto& val : object.package_val().value) {
          objects.emplace_back(Object(val));
        }
        value_ = std::move(objects);
        break;
      }
      case Tag::kReferenceVal: {
        value_ = Handle{
            .object_type = object.reference_val().object_type,
            .path =
                std::string(object.reference_val().path.data(), object.reference_val().path.size()),
        };
        break;
      }
      case Tag::kProcessorVal: {
        value_ = object.processor_val();
        break;
      }
      case Tag::kPowerResourceVal: {
        value_ = object.power_resource_val();
        break;
      }
      case Tag::kUnknown: {
        ZX_ASSERT_MSG(false, "Unknown object type");
      }
    }
  }

  // MEMBER defines two types:
  // |NAME_val()| which asserts that the returned value is NAME and returns it.
  // |is_NAME()| which returns true if the contained value is NAME.
#define MEMBER(NAME, TYPE)                  \
  TYPE& NAME##_val() {                      \
    TYPE* val = std::get_if<TYPE>(&value_); \
    ZX_ASSERT(val != nullptr);              \
    return *val;                            \
  }                                         \
  bool is_##NAME() { return std::get_if<TYPE>(&value_) != nullptr; }

  MEMBER(integer, uint64_t)
  MEMBER(string, std::string)
  MEMBER(bytes, std::vector<uint8_t>)
  MEMBER(package, std::vector<Object>)
  MEMBER(handle, Handle)
  MEMBER(processor, Processor)
  MEMBER(power_resource, PowerResource)
  MEMBER(status, Status)

#undef MEMBER
 private:
  std::variant<uint64_t, std::string, std::vector<uint8_t>, std::vector<Object>, Handle, Processor,
               PowerResource, Status>
      value_;
};

}  // namespace acpi

#endif  // SRC_DEVICES_LIB_ACPI_OBJECT_H_
