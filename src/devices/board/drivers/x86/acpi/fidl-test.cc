// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/drivers/x86/acpi/fidl.h"

#include <fidl/fuchsia.hardware.acpi/cpp/wire.h>
#include <lib/fidl/llcpp/vector_view.h>

#include <zxtest/zxtest.h>

#include "src/devices/board/drivers/x86/acpi/acpi.h"
#include "src/devices/board/drivers/x86/acpi/test/mock-acpi.h"

namespace facpi = fuchsia_hardware_acpi::wire;

using acpi::test::Device;
using EvaluateObjectRequestView =
    fidl::WireServer<fuchsia_hardware_acpi::Device>::EvaluateObjectRequestView;
using EvaluateObjectCompleter =
    fidl::WireServer<fuchsia_hardware_acpi::Device>::EvaluateObjectCompleter;

namespace {

void CheckEq(ACPI_OBJECT value, ACPI_OBJECT expected) {
  ASSERT_EQ(value.Type, expected.Type);
  switch (value.Type) {
    case ACPI_TYPE_INTEGER:
      ASSERT_EQ(value.Integer.Value, expected.Integer.Value);
      break;
    case ACPI_TYPE_STRING:
      ASSERT_EQ(value.String.Length, expected.String.Length);
      ASSERT_STR_EQ(value.String.Pointer, expected.String.Pointer);
      break;
    case ACPI_TYPE_PACKAGE:
      ASSERT_EQ(value.Package.Count, expected.Package.Count);
      for (size_t i = 0; i < value.Package.Count; i++) {
        ASSERT_NO_FATAL_FAILURES(CheckEq(value.Package.Elements[i], expected.Package.Elements[i]));
      }
      break;
    case ACPI_TYPE_BUFFER:
      ASSERT_EQ(value.Buffer.Length, expected.Buffer.Length);
      ASSERT_BYTES_EQ(value.Buffer.Pointer, expected.Buffer.Pointer, value.Buffer.Length);
      break;
    case ACPI_TYPE_POWER:
      ASSERT_EQ(value.PowerResource.ResourceOrder, expected.PowerResource.ResourceOrder);
      ASSERT_EQ(value.PowerResource.SystemLevel, expected.PowerResource.SystemLevel);
      break;
    case ACPI_TYPE_PROCESSOR:
      ASSERT_EQ(value.Processor.PblkAddress, expected.Processor.PblkAddress);
      ASSERT_EQ(value.Processor.PblkLength, expected.Processor.PblkLength);
      ASSERT_EQ(value.Processor.ProcId, expected.Processor.ProcId);
      break;
    case ACPI_TYPE_LOCAL_REFERENCE:
      ASSERT_EQ(value.Reference.ActualType, expected.Reference.ActualType);
      ASSERT_EQ(value.Reference.Handle, expected.Reference.Handle);
      break;
    default:
      ASSERT_FALSE(true, "Unexpected object type");
  }
}

void CheckEq(facpi::Object value, facpi::Object expected) {
  using Tag = fuchsia_hardware_acpi::wire::Object::Tag;
  ASSERT_EQ(value.which(), expected.which());
  switch (value.which()) {
    case Tag::kIntegerVal: {
      ASSERT_EQ(value.integer_val(), expected.integer_val());
      break;
    }
    case Tag::kStringVal: {
      ASSERT_EQ(value.string_val().size(), expected.string_val().size());
      ASSERT_BYTES_EQ(value.string_val().data(), expected.string_val().data(),
                      value.string_val().size());
      break;
    }
    case Tag::kPackageVal: {
      auto &val_list = value.package_val().value;
      auto &exp_list = expected.package_val().value;
      ASSERT_EQ(val_list.count(), exp_list.count());
      for (size_t i = 0; i < val_list.count(); i++) {
        ASSERT_NO_FATAL_FAILURES(CheckEq(val_list[i], exp_list[i]));
      }
      break;
    }
    case Tag::kBufferVal: {
      auto &val = value.buffer_val();
      auto &exp = expected.buffer_val();
      ASSERT_EQ(val.count(), exp.count());
      ASSERT_BYTES_EQ(val.data(), exp.data(), val.count());
      break;
    }
    case Tag::kPowerResourceVal: {
      auto &val = value.power_resource_val();
      auto &exp = expected.power_resource_val();
      ASSERT_EQ(val.resource_order, exp.resource_order);
      ASSERT_EQ(val.system_level, exp.system_level);
      break;
    }
    case Tag::kProcessorVal: {
      auto &val = value.processor_val();
      auto &exp = expected.processor_val();
      ASSERT_EQ(val.id, exp.id);
      ASSERT_EQ(val.pblk_address, exp.pblk_address);
      ASSERT_EQ(val.pblk_length, exp.pblk_length);
      break;
    }
    case Tag::kReferenceVal: {
      auto &val = value.reference_val();
      auto &exp = expected.reference_val();
      ASSERT_EQ(val.path.size(), exp.path.size());
      ASSERT_BYTES_EQ(val.path.data(), exp.path.data(), val.path.size());
      ASSERT_EQ(val.object_type, exp.object_type);
      break;
    }
    case Tag::kUnknown:
      ASSERT_TRUE(false);
  }
}

}  // namespace

class FidlEvaluateObjectTest : public zxtest::Test {
 public:
  void SetUp() override { acpi_.SetDeviceRoot(std::make_unique<Device>("\\")); }

  void InsertDeviceBelow(std::string path, std::unique_ptr<Device> d) {
    Device *parent = acpi_.GetDeviceRoot()->FindByPath(path);
    ASSERT_NE(parent, nullptr);
    parent->AddChild(std::move(d));
  }

 protected:
  acpi::test::MockAcpi acpi_;
};

TEST_F(FidlEvaluateObjectTest, TestCantEvaluateParent) {
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\", std::make_unique<Device>("_SB_")));
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\_SB_", std::make_unique<Device>("PCI0")));
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\_SB_.PCI0", std::make_unique<Device>("I2C0")));

  acpi::EvaluateObjectFidlHelper helper(&acpi_,
                                        acpi_.GetDeviceRoot()->FindByPath("\\_SB_.PCI0.I2C0"),
                                        "\\_SB_.PCI0", fidl::VectorView<facpi::Object>(nullptr, 0));

  auto result = helper.ValidateAndLookupPath("\\_SB_.PCI0");
  ASSERT_EQ(result.status_value(), AE_ACCESS);
}

TEST_F(FidlEvaluateObjectTest, TestCantEvaluateSibling) {
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\", std::make_unique<Device>("_SB_")));
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\_SB_", std::make_unique<Device>("PCI0")));
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\_SB_", std::make_unique<Device>("PCI1")));

  acpi::EvaluateObjectFidlHelper helper(&acpi_, acpi_.GetDeviceRoot()->FindByPath("\\_SB_.PCI1"),
                                        "\\_SB_.PCI0", fidl::VectorView<facpi::Object>(nullptr, 0));

  auto result = helper.ValidateAndLookupPath("\\_SB_.PCI0");
  ASSERT_EQ(result.status_value(), AE_ACCESS);
}

TEST_F(FidlEvaluateObjectTest, TestCanEvaluateChild) {
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\", std::make_unique<Device>("_SB_")));
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\_SB_", std::make_unique<Device>("PCI0")));
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\_SB_.PCI0", std::make_unique<Device>("I2C0")));

  acpi::EvaluateObjectFidlHelper helper(&acpi_, acpi_.GetDeviceRoot()->FindByPath("\\_SB_.PCI0"),
                                        "I2C0", fidl::VectorView<facpi::Object>(nullptr, 0));

  ACPI_HANDLE hnd;
  auto result = helper.ValidateAndLookupPath("I2C0", &hnd);
  ASSERT_EQ(result.status_value(), AE_OK);
  ASSERT_EQ(result.value(), "\\_SB_.PCI0.I2C0");
  ASSERT_EQ(hnd, acpi_.GetDeviceRoot()->FindByPath("\\_SB_.PCI0.I2C0"));
}

TEST_F(FidlEvaluateObjectTest, TestDecodeInteger) {
  acpi::EvaluateObjectFidlHelper helper(&acpi_, acpi_.GetDeviceRoot(), "\\",
                                        fidl::VectorView<facpi::Object>(nullptr, 0));

  facpi::Object obj;
  ACPI_OBJECT out;
  fidl::Arena<> alloc;
  obj.set_integer_val(alloc, 42);

  auto status = helper.DecodeObject(obj, &out);
  ASSERT_OK(status.status_value());
  ASSERT_NO_FATAL_FAILURES(CheckEq(out, ACPI_OBJECT{
                                            .Integer =
                                                {
                                                    .Type = ACPI_TYPE_INTEGER,
                                                    .Value = 42,
                                                },
                                        }));
}

TEST_F(FidlEvaluateObjectTest, TestDecodeString) {
  acpi::EvaluateObjectFidlHelper helper(&acpi_, acpi_.GetDeviceRoot(), "\\",
                                        fidl::VectorView<facpi::Object>(nullptr, 0));

  facpi::Object obj;
  ACPI_OBJECT out;
  fidl::Arena<> alloc;
  obj.set_string_val(alloc, "test string");
  auto status = helper.DecodeObject(obj, &out);
  ASSERT_OK(status.status_value());
  ASSERT_NO_FATAL_FAILURES(CheckEq(out, ACPI_OBJECT{
                                            .String =
                                                {
                                                    .Type = ACPI_TYPE_STRING,
                                                    .Length = 11,
                                                    .Pointer = const_cast<char *>("test string"),
                                                },
                                        }));
}

TEST_F(FidlEvaluateObjectTest, TestDecodeBuffer) {
  acpi::EvaluateObjectFidlHelper helper(&acpi_, acpi_.GetDeviceRoot(), "\\",
                                        fidl::VectorView<facpi::Object>(nullptr, 0));

  facpi::Object obj;
  ACPI_OBJECT out;
  fidl::Arena<> alloc;
  static constexpr uint8_t kBuffer[] = {0x12, 0x34, 0x56, 0x78, 0x76, 0x54, 0x32, 0x10};
  obj.set_buffer_val(alloc, fidl::VectorView<uint8_t>::FromExternal(const_cast<uint8_t *>(kBuffer),
                                                                    countof(kBuffer)));
  auto status = helper.DecodeObject(obj, &out);
  ASSERT_OK(status.status_value());
  ASSERT_NO_FATAL_FAILURES(CheckEq(out, ACPI_OBJECT{
                                            .Buffer =
                                                {
                                                    .Type = ACPI_TYPE_BUFFER,
                                                    .Length = countof(kBuffer),
                                                    .Pointer = const_cast<uint8_t *>(kBuffer),
                                                },
                                        }));
}

TEST_F(FidlEvaluateObjectTest, TestDecodePowerResource) {
  acpi::EvaluateObjectFidlHelper helper(&acpi_, acpi_.GetDeviceRoot(), "\\",
                                        fidl::VectorView<facpi::Object>(nullptr, 0));

  facpi::Object obj;
  ACPI_OBJECT out;
  fidl::Arena<> alloc;
  facpi::PowerResource power;
  power.resource_order = 9;
  power.system_level = 32;
  obj.set_power_resource_val(alloc, power);
  auto status = helper.DecodeObject(obj, &out);
  ASSERT_OK(status.status_value());
  ASSERT_NO_FATAL_FAILURES(CheckEq(out, ACPI_OBJECT{
                                            .PowerResource =
                                                {
                                                    .Type = ACPI_TYPE_POWER,
                                                    .SystemLevel = 32,
                                                    .ResourceOrder = 9,
                                                },
                                        }));
}

TEST_F(FidlEvaluateObjectTest, TestDecodeProcessorVal) {
  acpi::EvaluateObjectFidlHelper helper(&acpi_, acpi_.GetDeviceRoot(), "\\",
                                        fidl::VectorView<facpi::Object>(nullptr, 0));

  facpi::Object obj;
  ACPI_OBJECT out;
  fidl::Arena<> alloc;
  facpi::Processor processor;
  processor.pblk_address = 0xd00dfeed;
  processor.pblk_length = 0xabc;
  processor.id = 7;
  obj.set_processor_val(alloc, processor);
  auto status = helper.DecodeObject(obj, &out);
  ASSERT_OK(status.status_value());
  ASSERT_NO_FATAL_FAILURES(CheckEq(out, ACPI_OBJECT{
                                            .Processor =
                                                {
                                                    .Type = ACPI_TYPE_PROCESSOR,
                                                    .ProcId = 7,
                                                    .PblkAddress = 0xd00dfeed,
                                                    .PblkLength = 0xabc,
                                                },
                                        }));
}

TEST_F(FidlEvaluateObjectTest, TestDecodeReference) {
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\", std::make_unique<Device>("_SB_")));
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\_SB_", std::make_unique<Device>("PCI0")));
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\_SB_.PCI0", std::make_unique<Device>("I2C0")));

  acpi::EvaluateObjectFidlHelper helper(&acpi_, acpi_.GetDeviceRoot()->FindByPath("\\_SB_"),
                                        "\\_SB_", fidl::VectorView<facpi::Object>(nullptr, 0));
  facpi::Object obj;
  ACPI_OBJECT out;
  fidl::Arena<> alloc;
  facpi::Handle ref;
  ref.object_type = facpi::ObjectType::kDevice;
  ref.path = "PCI0.I2C0";
  obj.set_reference_val(alloc, ref);

  auto status = helper.DecodeObject(obj, &out);
  ASSERT_OK(status.status_value());
  ASSERT_NO_FATAL_FAILURES(
      CheckEq(out, ACPI_OBJECT{
                       .Reference =
                           {
                               .Type = ACPI_TYPE_LOCAL_REFERENCE,
                               .ActualType = ACPI_TYPE_DEVICE,
                               .Handle = acpi_.GetDeviceRoot()->FindByPath("\\_SB_.PCI0.I2C0"),
                           },
                   }));
}

TEST_F(FidlEvaluateObjectTest, TestDecodeParentReferenceFails) {
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\", std::make_unique<Device>("_SB_")));
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\_SB_", std::make_unique<Device>("PCI0")));
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\_SB_.PCI0", std::make_unique<Device>("I2C0")));

  acpi::EvaluateObjectFidlHelper helper(&acpi_, acpi_.GetDeviceRoot()->FindByPath("\\_SB_"),
                                        "\\_SB_", fidl::VectorView<facpi::Object>(nullptr, 0));
  facpi::Object obj;
  ACPI_OBJECT out;
  fidl::Arena<> alloc;
  facpi::Handle ref;
  ref.object_type = facpi::ObjectType::kDevice;
  ref.path = "\\";
  obj.set_reference_val(alloc, ref);

  auto status = helper.DecodeObject(obj, &out);
  ASSERT_EQ(status.status_value(), AE_ACCESS);
}

TEST_F(FidlEvaluateObjectTest, TestDecodePackage) {
  acpi::EvaluateObjectFidlHelper helper(&acpi_, acpi_.GetDeviceRoot(), "\\",
                                        fidl::VectorView<facpi::Object>(nullptr, 0));

  std::vector<facpi::Object> elements;
  facpi::Object obj;
  facpi::ObjectList list;
  ACPI_OBJECT out;
  fidl::Arena<> alloc;

  obj.set_integer_val(alloc, 32);
  elements.emplace_back(obj);
  obj.set_string_val(alloc, "test string");
  elements.emplace_back(obj);

  list.value = fidl::VectorView<facpi::Object>::FromExternal(elements);
  obj.set_package_val(alloc, list);

  auto status = helper.DecodeObject(obj, &out);
  ASSERT_OK(status.status_value());

  constexpr ACPI_OBJECT kObjects[2] = {
      {.Integer =
           {
               .Type = ACPI_TYPE_INTEGER,
               .Value = 32,
           }},
      {.String =
           {
               .Type = ACPI_TYPE_STRING,
               .Length = 11,
               .Pointer = const_cast<char *>("test string"),
           }},
  };
  ACPI_OBJECT expected = {
      .Package =
          {
              .Type = ACPI_TYPE_PACKAGE,
              .Count = 2,
              .Elements = const_cast<acpi_object *>(kObjects),
          },
  };

  ASSERT_NO_FATAL_FAILURES(CheckEq(out, expected));
}

TEST_F(FidlEvaluateObjectTest, TestDecodeParameters) {
  fidl::Arena<> alloc;
  std::vector<facpi::Object> params;
  facpi::Object object;
  object.set_integer_val(alloc, 32);
  params.emplace_back(object);
  object.set_string_val(alloc, "hello");
  params.emplace_back(object);
  constexpr uint8_t kData[] = {1, 2, 3};
  object.set_buffer_val(
      alloc, fidl::VectorView<uint8_t>::FromExternal(const_cast<uint8_t *>(kData), countof(kData)));
  params.emplace_back(object);

  auto view = fidl::VectorView<facpi::Object>::FromExternal(params);
  acpi::EvaluateObjectFidlHelper helper(&acpi_, acpi_.GetDeviceRoot(), "\\", view);

  auto result = helper.DecodeParameters(view);
  ASSERT_OK(result.zx_status_value());
  ACPI_OBJECT expected[] = {
      {.Integer = {.Type = ACPI_TYPE_INTEGER, .Value = 32}},
      {.String = {.Type = ACPI_TYPE_STRING, .Length = 5, .Pointer = const_cast<char *>("hello")}},
      {.Buffer = {.Type = ACPI_TYPE_BUFFER, .Length = 3, .Pointer = const_cast<uint8_t *>(kData)}},
  };
  auto value = std::move(result.value());
  ASSERT_EQ(value.size(), countof(expected));
  for (size_t i = 0; i < countof(expected); i++) {
    ASSERT_NO_FATAL_FAILURES(CheckEq(value[i], expected[i]), "param %zd", i);
  }
}

TEST_F(FidlEvaluateObjectTest, TestEncodeInt) {
  acpi::EvaluateObjectFidlHelper helper(&acpi_, acpi_.GetDeviceRoot(), "\\",
                                        fidl::VectorView<facpi::Object>(nullptr, 0));
  fidl::Arena<> alloc;
  ACPI_OBJECT obj = {.Integer = {.Type = ACPI_TYPE_INTEGER, .Value = 320}};
  auto result = helper.EncodeObject(alloc, &obj);
  ASSERT_OK(result.zx_status_value());

  facpi::Object expected;
  expected.set_integer_val(alloc, 320);
  ASSERT_NO_FATAL_FAILURES(CheckEq(result.value(), expected));
}

TEST_F(FidlEvaluateObjectTest, TestEncodeString) {
  acpi::EvaluateObjectFidlHelper helper(&acpi_, acpi_.GetDeviceRoot(), "\\",
                                        fidl::VectorView<facpi::Object>(nullptr, 0));
  fidl::Arena<> alloc;
  ACPI_OBJECT obj = {.String = {
                         .Type = ACPI_TYPE_STRING,
                         .Length = 3,
                         .Pointer = const_cast<char *>("abc"),
                     }};
  auto result = helper.EncodeObject(alloc, &obj);
  ASSERT_OK(result.zx_status_value());

  facpi::Object expected;
  expected.set_string_val(alloc, "abc");
  ASSERT_NO_FATAL_FAILURES(CheckEq(result.value(), expected));
}

TEST_F(FidlEvaluateObjectTest, TestEncodeBuffer) {
  acpi::EvaluateObjectFidlHelper helper(&acpi_, acpi_.GetDeviceRoot(), "\\",
                                        fidl::VectorView<facpi::Object>(nullptr, 0));
  fidl::Arena<> alloc;
  static constexpr uint8_t kBuffer[] = {0x12, 0x34, 0x56, 0x78, 0x76, 0x54, 0x32, 0x10};
  ACPI_OBJECT obj = {.Buffer = {
                         .Type = ACPI_TYPE_BUFFER,
                         .Length = countof(kBuffer),
                         .Pointer = const_cast<uint8_t *>(kBuffer),
                     }};
  auto result = helper.EncodeObject(alloc, &obj);
  ASSERT_OK(result.zx_status_value());

  facpi::Object expected;
  expected.set_buffer_val(alloc, fidl::VectorView<uint8_t>::FromExternal(
                                     const_cast<uint8_t *>(kBuffer), countof(kBuffer)));
  ASSERT_NO_FATAL_FAILURES(CheckEq(result.value(), expected));
}

TEST_F(FidlEvaluateObjectTest, TestEncodeProcessorVal) {
  acpi::EvaluateObjectFidlHelper helper(&acpi_, acpi_.GetDeviceRoot(), "\\",
                                        fidl::VectorView<facpi::Object>(nullptr, 0));

  fidl::Arena<> alloc;
  ACPI_OBJECT obj = {
      .Processor =
          {
              .Type = ACPI_TYPE_PROCESSOR,
              .ProcId = 7,
              .PblkAddress = 0xd00dfeed,
              .PblkLength = 0xabc,
          },
  };
  auto result = helper.EncodeObject(alloc, &obj);
  ASSERT_OK(result.zx_status_value());
  facpi::Object expected;
  facpi::Processor processor;
  processor.pblk_address = 0xd00dfeed;
  processor.pblk_length = 0xabc;
  processor.id = 7;
  expected.set_processor_val(alloc, processor);

  ASSERT_NO_FATAL_FAILURES(CheckEq(result.value(), expected));
}

TEST_F(FidlEvaluateObjectTest, TestEncodeReference) {
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\", std::make_unique<Device>("_SB_")));
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\_SB_", std::make_unique<Device>("PCI0")));
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\_SB_.PCI0", std::make_unique<Device>("I2C0")));

  acpi::EvaluateObjectFidlHelper helper(&acpi_, acpi_.GetDeviceRoot()->FindByPath("\\_SB_"),
                                        "\\_SB_", fidl::VectorView<facpi::Object>(nullptr, 0));
  fidl::Arena<> alloc;
  ACPI_OBJECT obj = {.Reference = {
                         .Type = ACPI_TYPE_LOCAL_REFERENCE,
                         .ActualType = ACPI_TYPE_DEVICE,
                         .Handle = acpi_.GetDeviceRoot()->FindByPath("\\_SB_.PCI0.I2C0"),
                     }};
  facpi::Object expected;
  facpi::Handle ref;
  ref.object_type = facpi::ObjectType::kDevice;
  ref.path = "\\_SB_.PCI0.I2C0";
  expected.set_reference_val(alloc, ref);

  auto result = helper.EncodeObject(alloc, &obj);
  ASSERT_OK(result.zx_status_value());
  ASSERT_NO_FATAL_FAILURES(CheckEq(result.value(), expected));
}

TEST_F(FidlEvaluateObjectTest, TestEncodeParentReferenceFails) {
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\", std::make_unique<Device>("_SB_")));
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\_SB_", std::make_unique<Device>("PCI0")));
  ASSERT_NO_FATAL_FAILURES(InsertDeviceBelow("\\_SB_.PCI0", std::make_unique<Device>("I2C0")));

  acpi::EvaluateObjectFidlHelper helper(&acpi_, acpi_.GetDeviceRoot()->FindByPath("\\_SB_"),
                                        "\\_SB_", fidl::VectorView<facpi::Object>(nullptr, 0));
  ACPI_OBJECT obj = {.Reference = {
                         .Type = ACPI_TYPE_LOCAL_REFERENCE,
                         .ActualType = ACPI_TYPE_DEVICE,
                         .Handle = acpi_.GetDeviceRoot(),
                     }};
  fidl::Arena<> alloc;
  auto status = helper.EncodeObject(alloc, &obj);
  ASSERT_EQ(status.status_value(), AE_ACCESS);
}

TEST_F(FidlEvaluateObjectTest, TestEncodePackage) {
  acpi::EvaluateObjectFidlHelper helper(&acpi_, acpi_.GetDeviceRoot(), "\\",
                                        fidl::VectorView<facpi::Object>(nullptr, 0));
  constexpr ACPI_OBJECT kObjects[2] = {
      {.Integer =
           {
               .Type = ACPI_TYPE_INTEGER,
               .Value = 32,
           }},
      {.String =
           {
               .Type = ACPI_TYPE_STRING,
               .Length = 11,
               .Pointer = const_cast<char *>("test string"),
           }},
  };
  ACPI_OBJECT obj = {
      .Package =
          {
              .Type = ACPI_TYPE_PACKAGE,
              .Count = 2,
              .Elements = const_cast<acpi_object *>(kObjects),
          },
  };
  fidl::Arena<> alloc;

  auto result = helper.EncodeObject(alloc, &obj);
  ASSERT_OK(result.zx_status_value());
  std::vector<facpi::Object> elements;
  facpi::Object expected;
  facpi::ObjectList list;

  expected.set_integer_val(alloc, 32);
  elements.emplace_back(expected);
  expected.set_string_val(alloc, "test string");
  elements.emplace_back(expected);

  list.value = fidl::VectorView<facpi::Object>::FromExternal(elements);
  expected.set_package_val(alloc, list);

  ASSERT_NO_FATAL_FAILURES(CheckEq(result.value(), expected));
}

TEST_F(FidlEvaluateObjectTest, TestEncodeReturnValue) {
  ACPI_OBJECT obj = {.Integer = {.Type = ACPI_TYPE_INTEGER, .Value = 47}};
  acpi::EvaluateObjectFidlHelper helper(&acpi_, acpi_.GetDeviceRoot(), "\\",
                                        fidl::VectorView<facpi::Object>(nullptr, 0));

  fidl::Arena<> alloc;
  auto result = helper.EncodeReturnValue(alloc, &obj);
  ASSERT_OK(result.zx_status_value());
  ASSERT_FALSE(result.value().is_err());
  auto &object = result.value().response().result;
  // Expect a value of this size to be encoded in-line.
  fuchsia_hardware_acpi::wire::Object expected;
  expected.set_integer_val(alloc, 47);
  ASSERT_NO_FATAL_FAILURES(CheckEq(object.object(), expected));
}
