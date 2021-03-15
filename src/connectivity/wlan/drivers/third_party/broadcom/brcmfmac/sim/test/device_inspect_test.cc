/*
 * Copyright (c) 2020 The Fuchsia Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "device_inspect_test.h"

namespace wlan {
namespace brcmfmac {

constexpr uint16_t kUintPropertyNum = 6;
constexpr uint16_t kWindowPropertyNum = 6;

const std::vector<std::string> kRootMetrics = {"brcmfmac-phy"};
const std::vector<std::string> kConnMetrics = {"brcmfmac-phy", "connection-metrics"};
const std::vector<std::string> kArpReqFrame = {"brcmfmac-phy", "arp-req-frames"};

struct PropertyTestUnit {
  const std::vector<std::string> path_;
  std::string name_;
  std::function<void()> log_callback_;
  PropertyTestUnit(const std::vector<std::string> path, std::string name,
                   std::function<void()> callback)
      : path_(path), name_(name), log_callback_(callback) {}
};

struct ArpReqFrameTestInfo {
  bool frame_exists_;
  int64_t timestamp_;
  std::vector<uint8_t> frame_bytes_;
};

class DeviceInspectTest : public DeviceInspectTestHelper {
 public:
  void LogTxQfull() { device_->inspect_.LogTxQueueFull(); }
  void LogConnSuccess() { device_->inspect_.LogConnSuccess(); }
  void LogConnNoNetworkFail() { device_->inspect_.LogConnNoNetworkFail(); }
  void LogConnAuthFail() { device_->inspect_.LogConnAuthFail(); }
  void LogConnOtherFail() { device_->inspect_.LogConnOtherFail(); }
  void LogArpRequestFrame(zx_time_t time, const uint8_t* frame, size_t frame_size) {
    device_->inspect_.LogArpRequestFrame(time, frame, frame_size);
  }
  void LogRxFreeze() { device_->inspect_.LogRxFreeze(); }

  uint64_t GetUintProperty(const std::vector<std::string>& path, std::string name) {
    FetchHierarchy();
    auto* root = hierarchy_.value().GetByPath(path);
    EXPECT_TRUE(root);
    auto* uint_property = root->node().get_property<inspect::UintPropertyValue>(name);
    EXPECT_TRUE(uint_property);
    return uint_property->value();
  }

  ArpReqFrameTestInfo GetArpFrameNodeInfo(const std::string& frame_num) {
    FetchHierarchy();
    std::vector<std::string> path(kArpReqFrame);
    ArpReqFrameTestInfo frame_info_out = {};

    path.push_back(frame_num);
    auto* root = hierarchy_.value().GetByPath(path);
    if (root == nullptr) {
      frame_info_out.frame_exists_ = false;
      return frame_info_out;
    }
    frame_info_out.frame_exists_ = true;
    auto* int_property = root->node().get_property<inspect::IntPropertyValue>("timestamp");
    EXPECT_TRUE(int_property);
    frame_info_out.timestamp_ = int_property->value();
    auto* byte_vector_property =
        root->node().get_property<inspect::ByteVectorPropertyValue>("frame_data");
    EXPECT_TRUE(byte_vector_property);
    frame_info_out.frame_bytes_ = byte_vector_property->value();
    return frame_info_out;
  }

  // Defining properties which will be covered in the test cases.
  const PropertyTestUnit uint_properties_[kUintPropertyNum] = {
      PropertyTestUnit(kRootMetrics, "tx_qfull", std::bind(&DeviceInspectTest::LogTxQfull, this)),
      PropertyTestUnit(kConnMetrics, "success",
                       std::bind(&DeviceInspectTest::LogConnSuccess, this)),
      PropertyTestUnit(kConnMetrics, "no_network_fail",
                       std::bind(&DeviceInspectTest::LogConnNoNetworkFail, this)),
      PropertyTestUnit(kConnMetrics, "auth_fail",
                       std::bind(&DeviceInspectTest::LogConnAuthFail, this)),
      PropertyTestUnit(kConnMetrics, "other_fail",
                       std::bind(&DeviceInspectTest::LogConnOtherFail, this)),
      PropertyTestUnit(kRootMetrics, "rx_freeze",
                       std::bind(&DeviceInspectTest::LogRxFreeze, this)),
  };
  const PropertyTestUnit window_properties_[kWindowPropertyNum] = {
      PropertyTestUnit(kRootMetrics, "tx_qfull_24hrs",
                       std::bind(&DeviceInspectTest::LogTxQfull, this)),
      PropertyTestUnit(kConnMetrics, "success_24hrs",
                       std::bind(&DeviceInspectTest::LogConnSuccess, this)),
      PropertyTestUnit(kConnMetrics, "no_network_fail_24hrs",
                       std::bind(&DeviceInspectTest::LogConnNoNetworkFail, this)),
      PropertyTestUnit(kConnMetrics, "auth_fail_24hrs",
                       std::bind(&DeviceInspectTest::LogConnAuthFail, this)),
      PropertyTestUnit(kConnMetrics, "other_fail_24hrs",
                       std::bind(&DeviceInspectTest::LogConnOtherFail, this)),
      PropertyTestUnit(kRootMetrics, "rx_freeze_24hrs",
                       std::bind(&DeviceInspectTest::LogRxFreeze, this)),
  };
};

TEST_F(DeviceInspectTest, HierarchyCreation) {
  FetchHierarchy();
  ASSERT_TRUE(hierarchy_.is_ok());
}

TEST_F(DeviceInspectTest, SimpleIncrementCounterSingle) {
  // Going over all inspect counters inside this loop.
  for (uint16_t k = 0; k < kUintPropertyNum; k++) {
    BRCMF_INFO("Testing %s", uint_properties_[k].name_.c_str());
    EXPECT_EQ(0u, GetUintProperty(uint_properties_[k].path_, uint_properties_[k].name_));
    uint_properties_[k].log_callback_();
    EXPECT_EQ(1u, GetUintProperty(uint_properties_[k].path_, uint_properties_[k].name_));
  }
}

TEST_F(DeviceInspectTest, SimpleIncrementCounterMultiple) {
  const uint32_t property_cnt = 100;

  // Outer Loop is to go over all inspect counters.
  for (uint16_t k = 0; k < kUintPropertyNum; k++) {
    BRCMF_INFO("Testing %s", uint_properties_[k].name_.c_str());
    EXPECT_EQ(0u, GetUintProperty(uint_properties_[k].path_, uint_properties_[k].name_));
    for (uint32_t i = 0; i < property_cnt; i++) {
      uint_properties_[k].log_callback_();
    }
    EXPECT_EQ(property_cnt, GetUintProperty(uint_properties_[k].path_, uint_properties_[k].name_));
  }
}

TEST_F(DeviceInspectTest, SimpleIncrementCounter24HrsFor10Hrs) {
  // Outer Loop is to go over all inspect counters.
  for (uint16_t k = 0; k < kWindowPropertyNum; k++) {
    BRCMF_INFO("Testing %s", window_properties_[k].name_.c_str());
    EXPECT_EQ(0u, GetUintProperty(window_properties_[k].path_, window_properties_[k].name_));
    const uint32_t log_duration = 10;
    // Log 1 queue full event every hour, for 'log_duration' hours.
    for (uint32_t i = 0; i < log_duration; i++) {
      SCHEDULE_CALL(zx::hour(i), window_properties_[k].log_callback_);
    }
    env_->Run(zx::hour(log_duration));

    // Since we also log once at the beginning of the run, we will have one more count.
    EXPECT_EQ(log_duration,
              GetUintProperty(window_properties_[k].path_, window_properties_[k].name_));
  }
}

TEST_F(DeviceInspectTest, LogTxQfull24HrsFor100Hrs) {
  // Outer Loop is to go over all inspect counters.
  for (uint16_t k = 0; k < kWindowPropertyNum; k++) {
    BRCMF_INFO("Testing %s", window_properties_[k].name_.c_str());
    EXPECT_EQ(0u, GetUintProperty(window_properties_[k].path_, window_properties_[k].name_));
    const uint32_t log_duration = 100;

    // Log 1 queue full event every hour, for 'log_duration' hours.
    for (uint32_t i = 0; i < log_duration; i++) {
      SCHEDULE_CALL(zx::hour(i), window_properties_[k].log_callback_);
    }
    env_->Run(zx::hour(log_duration));

    // Since log_duration is > 24hrs, we expect the rolling counter to show
    // a count of only 24.
    EXPECT_EQ(24u, GetUintProperty(window_properties_[k].path_, window_properties_[k].name_));
  }
}

TEST_F(DeviceInspectTest, LogArpRequestFrameTest) {
  constexpr static size_t kFakeFirstFrameSize = 8;
  constexpr static size_t kFakeOtherFrameSize = 6;

  const zx_time_t base_time = 84848;
  const uint8_t fake_first_frame[kFakeFirstFrameSize] = {5, 5, 5, 5, 5, 5, 5, 5};
  const uint8_t fake_other_frames[kFakeOtherFrameSize] = {1, 2, 3, 4, 5, 6};
  ArpReqFrameTestInfo frame_info_out = {};

  // Log the first frame and verify it.
  LogArpRequestFrame(base_time, fake_first_frame, kFakeFirstFrameSize);
  frame_info_out = GetArpFrameNodeInfo("0");
  EXPECT_TRUE(frame_info_out.frame_exists_);
  EXPECT_EQ(frame_info_out.timestamp_, base_time);
  EXPECT_EQ(frame_info_out.frame_bytes_,
            std::vector<uint8_t>(fake_first_frame, fake_first_frame + kFakeFirstFrameSize));

  for (uint16_t k = 0; k < ArpFrameMetrics::kMaxArpRequestFrameCount; k++) {
    LogArpRequestFrame(base_time + 1 + k, fake_other_frames, kFakeOtherFrameSize);
  }

  // Verify that the first frame was deleted.
  frame_info_out = GetArpFrameNodeInfo("0");
  EXPECT_FALSE(frame_info_out.frame_exists_);

  // Verify that the second frame exists.
  frame_info_out = GetArpFrameNodeInfo("1");
  EXPECT_TRUE(frame_info_out.frame_exists_);
  EXPECT_EQ(frame_info_out.timestamp_, base_time + 1);
  EXPECT_EQ(frame_info_out.frame_bytes_,
            std::vector<uint8_t>(fake_other_frames, fake_other_frames + kFakeOtherFrameSize));

  // Verify the last frame is correct.
  frame_info_out = GetArpFrameNodeInfo(std::to_string(ArpFrameMetrics::kMaxArpRequestFrameCount));
  EXPECT_TRUE(frame_info_out.frame_exists_);
  EXPECT_EQ(frame_info_out.timestamp_,
            base_time + (const int64_t)ArpFrameMetrics::kMaxArpRequestFrameCount);
  EXPECT_EQ(frame_info_out.frame_bytes_,
            std::vector<uint8_t>(fake_other_frames, fake_other_frames + kFakeOtherFrameSize));
}

}  // namespace brcmfmac
}  // namespace wlan
