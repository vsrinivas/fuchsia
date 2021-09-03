// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>

#include <sdk/lib/inspect/testing/cpp/zxtest/inspect.h>
#include <zxtest/zxtest.h>

#include "fidl/fuchsia.acpi.chromeos/cpp/wire.h"
#include "src/devices/lib/acpi/mock/mock-acpi.h"
#include "src/devices/misc/drivers/chromeos-acpi/chromeos_acpi.h"
#include "src/devices/testing/mock-ddk/mock-device.h"
#include "third_party/vboot_reference/firmware/include/vboot_struct.h"

namespace chromeos_acpi {

using inspect::InspectTestHelper;

class ChromeosAcpiTest : public InspectTestHelper, public zxtest::Test {
 public:
  ChromeosAcpiTest() : loop_(&kAsyncLoopConfigNeverAttachToThread) {}
  void SetUp() override {
    ASSERT_OK(loop_.StartThread("test-fidl-thread"));
    fake_root_ = MockDevice::FakeRootParent();
    acpi_.SetEvaluateObject([this](acpi::mock::Device::EvaluateObjectRequestView request,
                                   acpi::mock::Device::EvaluateObjectCompleter::Sync& completer) {
      if (strncmp(request->path.data(), "MLST", request->path.size()) == 0) {
        std::vector<std::string> keys(values_.size());
        std::transform(values_.begin(), values_.end(), keys.begin(),
                       [](auto& pair) { return pair.first; });
        facpi::EncodedObject result;
        result.set_object(arena_, ToPackage(keys));
        completer.ReplySuccess(std::move(result));
        return;
      }
      auto pair = values_.find(std::string(request->path.data(), request->path.size()));
      if (pair == values_.end()) {
        completer.ReplyError(facpi::Status::kNotFound);
        return;
      }

      facpi::EncodedObject result;
      result.set_object(arena_, pair->second);
      completer.ReplySuccess(std::move(result));
    });
  }

  void CreateDevice() {
    auto client = acpi_.CreateClient(loop_.dispatcher());
    ASSERT_OK(client.status_value());
    auto device = std::make_unique<ChromeosAcpi>(fake_root_.get(), std::move(client.value()));
    ASSERT_OK(device->Bind());
    __UNUSED auto unused = device.release();

    MockDevice* dev = fake_root_->GetLatestChild();
    dev->InitOp();
    ASSERT_OK(dev->WaitUntilInitReplyCalled());

    auto value = fidl::CreateEndpoints<fuchsia_acpi_chromeos::Device>();
    ASSERT_OK(value.status_value());

    fidl::BindServer(loop_.dispatcher(), std::move(value->server), GetDevice());
    fidl_client_.client_end() = std::move(value->client);
  }

  ChromeosAcpi* GetDevice() {
    return fake_root_->GetLatestChild()->GetDeviceContext<ChromeosAcpi>();
  }

  void TearDown() override {}

  facpi::Object MakeObject(std::string val) {
    facpi::Object ret;
    fidl::StringView view;
    view.Set(arena_, val);
    ret.set_string_val(arena_, view);
    return ret;
  }

  facpi::Object MakeObject(uint64_t val) {
    facpi::Object ret;
    ret.set_integer_val(arena_, val);
    return ret;
  }

  facpi::Object MakeObject(cpp20::span<uint8_t> buf) {
    facpi::Object ret;
    fidl::VectorView<uint8_t> data;
    data.Allocate(arena_, buf.size());
    memcpy(data.mutable_data(), buf.data(), buf.size());
    ret.set_buffer_val(arena_, data);
    return ret;
  }

  template <typename T>
  facpi::Object ToPackage(std::vector<T> values) {
    fidl::VectorView<facpi::Object> result;
    result.Allocate(arena_, values.size());
    size_t i = 0;
    for (auto& val : values) {
      result[i] = MakeObject(val);
      i++;
    }

    facpi::ObjectList list{
        .value = result,
    };
    facpi::Object obj;
    obj.set_package_val(arena_, list);
    return obj;
  }

 protected:
  std::shared_ptr<MockDevice> fake_root_;
  acpi::mock::Device acpi_;
  fidl::Arena<> arena_;
  std::unordered_map<std::string, facpi::Object> values_;
  async::Loop loop_;
  fidl::WireSyncClient<fuchsia_acpi_chromeos::Device> fidl_client_;
};

TEST_F(ChromeosAcpiTest, NoMethodsTest) { ASSERT_NO_FATAL_FAILURES(CreateDevice()); }

TEST_F(ChromeosAcpiTest, HardwareIDTest) {
  std::vector<std::string> args{std::string("ATLAS 1234")};
  values_.emplace("HWID", ToPackage(args));

  ASSERT_NO_FATAL_FAILURES(CreateDevice());
  auto device = GetDevice();
  ASSERT_NO_FATAL_FAILURES(ReadInspect(device->inspect_vmo()));
  CheckProperty(hierarchy().node(), "method-list", inspect::StringPropertyValue("HWID"));
  CheckProperty(hierarchy().node(), "hwid", inspect::StringPropertyValue(*args.begin()));
}

TEST_F(ChromeosAcpiTest, ROFirmwareIDTest) {
  std::vector<std::string> args{std::string("Google_Atlas.11827.162.2021_08_03_1442")};
  values_.emplace("FRID", ToPackage(args));

  ASSERT_NO_FATAL_FAILURES(CreateDevice());
  auto device = GetDevice();
  ASSERT_NO_FATAL_FAILURES(ReadInspect(device->inspect_vmo()));
  CheckProperty(hierarchy().node(), "method-list", inspect::StringPropertyValue("FRID"));
  CheckProperty(hierarchy().node(), "ro-fwid", inspect::StringPropertyValue(*args.begin()));
}

TEST_F(ChromeosAcpiTest, RWFirmwareIDTest) {
  std::vector<std::string> args{std::string("Google_Atlas.11827.162.2021_08_05_0000")};
  values_.emplace("FWID", ToPackage(args));

  ASSERT_NO_FATAL_FAILURES(CreateDevice());
  auto device = GetDevice();
  ASSERT_NO_FATAL_FAILURES(ReadInspect(device->inspect_vmo()));
  CheckProperty(hierarchy().node(), "method-list", inspect::StringPropertyValue("FWID"));
  CheckProperty(hierarchy().node(), "rw-fwid", inspect::StringPropertyValue(*args.begin()));
}

TEST_F(ChromeosAcpiTest, NvramLocationTest) {
  std::vector<uint64_t> args{10, 20};
  values_.emplace("VBNV", ToPackage(args));

  ASSERT_NO_FATAL_FAILURES(CreateDevice());
  auto device = GetDevice();
  ASSERT_NO_FATAL_FAILURES(ReadInspect(device->inspect_vmo()));
  CheckProperty(hierarchy().node(), "method-list", inspect::StringPropertyValue("VBNV"));
  CheckProperty(hierarchy().node(), "nvram-data-base", inspect::UintPropertyValue(10));
  CheckProperty(hierarchy().node(), "nvram-data-size", inspect::UintPropertyValue(20));
}

TEST_F(ChromeosAcpiTest, FlashmapBaseTest) {
  std::vector<uint64_t> args{0xfffe1234};
  values_.emplace("FMAP", ToPackage(args));

  ASSERT_NO_FATAL_FAILURES(CreateDevice());
  auto device = GetDevice();
  ASSERT_NO_FATAL_FAILURES(ReadInspect(device->inspect_vmo()));
  CheckProperty(hierarchy().node(), "method-list", inspect::StringPropertyValue("FMAP"));
  CheckProperty(hierarchy().node(), "flashmap-addr", inspect::UintPropertyValue(*args.begin()));
}

TEST_F(ChromeosAcpiTest, NvdataVersionTestV2) {
  VbSharedDataHeader data = {
      .magic = VB_SHARED_DATA_MAGIC,
      .struct_version = VB_SHARED_DATA_VERSION,
      .struct_size = VB_SHARED_DATA_HEADER_SIZE_V2,
      .flags = kVbootSharedDataNvdataV2,
  };

  std::vector<cpp20::span<uint8_t>> entries{
      cpp20::span(reinterpret_cast<uint8_t*>(&data), sizeof(data))};
  values_.emplace("VDAT", ToPackage(entries));
  ASSERT_NO_FATAL_FAILURES(CreateDevice());

  auto result = fidl_client_.GetNvdataVersion();
  ASSERT_OK(result.status());
  ASSERT_EQ(result->result.response().version, 2);
}

TEST_F(ChromeosAcpiTest, NvdataVersionTestV1) {
  VbSharedDataHeader data = {
      .magic = VB_SHARED_DATA_MAGIC,
      .struct_version = VB_SHARED_DATA_VERSION,
      .struct_size = VB_SHARED_DATA_HEADER_SIZE_V2,
      .flags = 0,
  };

  std::vector<cpp20::span<uint8_t>> entries{
      cpp20::span(reinterpret_cast<uint8_t*>(&data), sizeof(data))};
  values_.emplace("VDAT", ToPackage(entries));
  ASSERT_NO_FATAL_FAILURES(CreateDevice());

  auto result = fidl_client_.GetNvdataVersion();
  ASSERT_OK(result.status());
  ASSERT_EQ(result->result.response().version, 1);
}

}  // namespace chromeos_acpi
