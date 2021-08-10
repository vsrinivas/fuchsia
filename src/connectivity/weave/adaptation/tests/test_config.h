// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_TEST_CONFIG_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_TEST_CONFIG_H_

namespace weave::adaptation::testing::testdata {

// Constants from testdata JSON. Values should be kept aligned with what is
// present in the JSON and are used in tests to make comparisons.
static constexpr uint16_t kTestDataProductId = 60209;
static constexpr uint16_t kTestDataVendorId = 5050;
static constexpr uint64_t kTestDataDeviceId = 65535;
static constexpr char kTestDataProductIdDescription[] = "Fuchsia Product";
static constexpr char kTestDataVendorIdDescription[] = "Fuchsia Vendor";
static constexpr char kTestDataFirmwareRevision[] = "prerelease-1";
static constexpr char kTestDataSerialNumber[] = "ABCD1234";
static constexpr char kTestDataPairingCode[] = "ABC123";
static constexpr uint32_t kTestThreadJoinableDuration = 1234;

static constexpr char kTestConfigFileName[] = "device_info.json";
static constexpr char kTestAltConfigFileName[] = "device_info_alt.json";

static constexpr uint16_t kTestAltDataProductId = 2000;
static constexpr uint16_t kTestAltDataVendorId = 1000;

static constexpr char kTestDataCertificateFileName[] = "testdata_cert";
static constexpr char kTestDataCertificateFileData[] = "TEST_CERT";
static constexpr char kTestDataPrivateKeyFileName[] = "testdata_privkey";
static constexpr char kTestDataPrivateKeyFileData[] = "TEST_PRIVKEY";

}  // namespace weave::adaptation::testing::testdata

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_TESTS_TEST_CONFIG_H_
