// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include "platform_auth_delegate.h"
// clang-format on

#include <lib/fit/defer.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <Weave/Core/WeaveTLV.h>

#include "fake_weave_signer.h"
#include "test_configuration_manager.h"
#include "test_thread_stack_manager.h"
#include "weave_test_fixture.h"

namespace weave::adaptation::testing {
namespace {

using nl::Ble::PacketBuffer;
using nl::Weave::WeaveKeyExport;
using nl::Weave::DeviceLayer::ConfigurationMgr;
using nl::Weave::DeviceLayer::ConfigurationMgrImpl;
using nl::Weave::DeviceLayer::PlatformMgrImpl;
using nl::Weave::DeviceLayer::ThreadStackMgrImpl;
using nl::Weave::DeviceLayer::Internal::BeginSessionContext;
using nl::Weave::DeviceLayer::Internal::PlatformAuthDelegate;
using nl::Weave::DeviceLayer::Internal::ValidationContext;
using nl::Weave::DeviceLayer::Internal::testing::WeaveTestFixture;
using nl::Weave::Profiles::DeviceDescription::WeaveDeviceDescriptor;
using nl::Weave::Profiles::Security::WeaveCertificateData;
using nl::Weave::Profiles::Security::WeaveCertificateSet;

namespace ASN1 = nl::Weave::ASN1;
namespace Profiles = nl::Weave::Profiles;
namespace Security = nl::Weave::Profiles::Security;
namespace TLV = nl::Weave::TLV;
namespace Weave = nl::Weave;

using WeaveKeyId = nl::Weave::WeaveKeyId;

// A dummy account certificate, provided by openweave-core.
constexpr char kTestDeviceCertName[] = "DUMMY-ACCOUNT-ID";
constexpr uint8_t kTestDeviceCert[] = {
    0xd5, 0x00, 0x00, 0x04, 0x00, 0x01, 0x00, 0x30, 0x01, 0x08, 0x4e, 0x2f, 0x32, 0x4b, 0x41, 0xd7,
    0x3a, 0xdb, 0x24, 0x02, 0x04, 0x37, 0x03, 0x2c, 0x81, 0x10, 0x44, 0x55, 0x4d, 0x4d, 0x59, 0x2d,
    0x41, 0x43, 0x43, 0x4f, 0x55, 0x4e, 0x54, 0x2d, 0x49, 0x44, 0x18, 0x26, 0x04, 0xcb, 0xa8, 0xfa,
    0x1b, 0x26, 0x05, 0x4b, 0x35, 0x4f, 0x42, 0x37, 0x06, 0x2c, 0x81, 0x10, 0x44, 0x55, 0x4d, 0x4d,
    0x59, 0x2d, 0x41, 0x43, 0x43, 0x4f, 0x55, 0x4e, 0x54, 0x2d, 0x49, 0x44, 0x18, 0x24, 0x07, 0x02,
    0x26, 0x08, 0x25, 0x00, 0x5a, 0x23, 0x30, 0x0a, 0x39, 0x04, 0x2b, 0xd9, 0xdb, 0x5a, 0x62, 0xef,
    0xba, 0xb1, 0x53, 0x2a, 0x0f, 0x99, 0x63, 0xb7, 0x8a, 0x30, 0xc5, 0x8a, 0x41, 0x29, 0xa5, 0x19,
    0x4e, 0x4b, 0x0b, 0xf3, 0x7e, 0xda, 0xc5, 0xe9, 0xb3, 0x35, 0xf0, 0x75, 0x18, 0x6d, 0x49, 0x5d,
    0x86, 0xc4, 0x44, 0x25, 0x07, 0x41, 0xb4, 0xd3, 0xa9, 0xef, 0xee, 0xb4, 0x2a, 0xd6, 0x0a, 0x5d,
    0x9d, 0xe0, 0x35, 0x83, 0x29, 0x01, 0x18, 0x35, 0x82, 0x29, 0x01, 0x24, 0x02, 0x05, 0x18, 0x35,
    0x84, 0x29, 0x01, 0x36, 0x02, 0x04, 0x02, 0x04, 0x01, 0x18, 0x18, 0x35, 0x81, 0x30, 0x02, 0x08,
    0x42, 0x3c, 0x95, 0x5f, 0x46, 0x1e, 0x52, 0xdb, 0x18, 0x35, 0x80, 0x30, 0x02, 0x08, 0x42, 0x3c,
    0x95, 0x5f, 0x46, 0x1e, 0x52, 0xdb, 0x18, 0x35, 0x0c, 0x30, 0x01, 0x1d, 0x00, 0x8a, 0x61, 0x86,
    0x62, 0x3d, 0x17, 0xb2, 0xd2, 0xcf, 0xd2, 0x6d, 0x39, 0x3d, 0xe4, 0x25, 0x69, 0xe0, 0x91, 0xea,
    0x05, 0x6a, 0x75, 0xce, 0xdd, 0x45, 0xeb, 0x83, 0xcf, 0x30, 0x02, 0x1c, 0x74, 0xb4, 0x2b, 0xa4,
    0x6d, 0x14, 0x65, 0xb7, 0xb7, 0x71, 0x9a, 0x5a, 0xaf, 0x64, 0xd2, 0x88, 0x60, 0x6e, 0xb3, 0xb1,
    0xa0, 0x31, 0xca, 0x92, 0x6f, 0xca, 0xf2, 0x43, 0x18, 0x18};

// The below hash was generated with the following process, using the dummy
// private key provided by openweave-core.
//
// weave convert-key --pem weave-dummy.key weave-dummy.key.pem
// openssl dgst -sha256 -sign weave-dummy.key.pem hash > signed-hash
constexpr uint8_t kTestHash[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ123456";
constexpr uint8_t kTestSignedHash[] = {
    0x30, 0x3d, 0x02, 0x1d, 0x00, 0xdf, 0x70, 0x31, 0xb3, 0xbe, 0x63, 0x80, 0xc9, 0xd0, 0xd8, 0x67,
    0x70, 0xf5, 0xa6, 0x50, 0xf1, 0x18, 0xb4, 0x42, 0x1a, 0x5a, 0x72, 0x68, 0xd0, 0x13, 0x0c, 0xd6,
    0xda, 0x02, 0x1c, 0x59, 0xba, 0x2d, 0x92, 0xc0, 0x49, 0x94, 0x22, 0xbd, 0xb2, 0x46, 0x8c, 0x78,
    0x8c, 0x44, 0x59, 0xd9, 0xbc, 0x13, 0xe6, 0x3a, 0x93, 0x85, 0x5c, 0xe9, 0x04, 0x53, 0xe7};

// Using test-dev-18B4300000000000-key.pem from openweave.
constexpr uint8_t kTestPrivateKey[] = {
    0xD5, 0x00, 0x00, 0x04, 0x00, 0x02, 0x00, 0x26, 0x01, 0x25, 0x00, 0x5A, 0x23, 0x30, 0x02,
    0x1C, 0x31, 0xBD, 0xA7, 0x6B, 0xDD, 0x92, 0x3C, 0xBB, 0x56, 0x80, 0xBA, 0x90, 0xF6, 0xDC,
    0x49, 0xC8, 0x0B, 0x89, 0xC6, 0xCF, 0x7A, 0x54, 0x94, 0xB0, 0x8C, 0xED, 0x05, 0x5E, 0x30,
    0x03, 0x39, 0x04, 0x15, 0xF7, 0xC7, 0xD3, 0xCF, 0xD4, 0xCE, 0x17, 0x71, 0x7C, 0x15, 0x4E,
    0x62, 0xCA, 0xB0, 0x03, 0x0A, 0xE0, 0x7F, 0xA9, 0x86, 0x69, 0xA3, 0x94, 0x41, 0xFA, 0xAB,
    0xF2, 0xED, 0x80, 0xDF, 0x69, 0x7C, 0x86, 0x91, 0xDC, 0x1B, 0xE9, 0x67, 0xD2, 0x3C, 0x10,
    0x06, 0xD9, 0x6C, 0x03, 0xCC, 0xFE, 0x84, 0xE5, 0x49, 0x88, 0xA1, 0x35, 0x0C, 0xBC, 0x18};

// A dummy service configuration, provided by openweave-core. This is the
// contents of the configuration after base64 decoding.
constexpr uint8_t kTestServiceConfig[] = {
    0xd5, 0x00, 0x00, 0x0f, 0x00, 0x01, 0x00, 0x36, 0x01, 0x15, 0x30, 0x01, 0x08, 0x4e, 0x2f, 0x32,
    0x4b, 0x41, 0xd7, 0x3a, 0xdb, 0x24, 0x02, 0x04, 0x37, 0x03, 0x2c, 0x81, 0x10, 0x44, 0x55, 0x4d,
    0x4d, 0x59, 0x2d, 0x41, 0x43, 0x43, 0x4f, 0x55, 0x4e, 0x54, 0x2d, 0x49, 0x44, 0x18, 0x26, 0x04,
    0xcb, 0xa8, 0xfa, 0x1b, 0x26, 0x05, 0x4b, 0x35, 0x4f, 0x42, 0x37, 0x06, 0x2c, 0x81, 0x10, 0x44,
    0x55, 0x4d, 0x4d, 0x59, 0x2d, 0x41, 0x43, 0x43, 0x4f, 0x55, 0x4e, 0x54, 0x2d, 0x49, 0x44, 0x18,
    0x24, 0x07, 0x02, 0x26, 0x08, 0x25, 0x00, 0x5a, 0x23, 0x30, 0x0a, 0x39, 0x04, 0x2b, 0xd9, 0xdb,
    0x5a, 0x62, 0xef, 0xba, 0xb1, 0x53, 0x2a, 0x0f, 0x99, 0x63, 0xb7, 0x8a, 0x30, 0xc5, 0x8a, 0x41,
    0x29, 0xa5, 0x19, 0x4e, 0x4b, 0x0b, 0xf3, 0x7e, 0xda, 0xc5, 0xe9, 0xb3, 0x35, 0xf0, 0x75, 0x18,
    0x6d, 0x49, 0x5d, 0x86, 0xc4, 0x44, 0x25, 0x07, 0x41, 0xb4, 0xd3, 0xa9, 0xef, 0xee, 0xb4, 0x2a,
    0xd6, 0x0a, 0x5d, 0x9d, 0xe0, 0x35, 0x83, 0x29, 0x01, 0x18, 0x35, 0x82, 0x29, 0x01, 0x24, 0x02,
    0x05, 0x18, 0x35, 0x84, 0x29, 0x01, 0x36, 0x02, 0x04, 0x02, 0x04, 0x01, 0x18, 0x18, 0x35, 0x81,
    0x30, 0x02, 0x08, 0x42, 0x3c, 0x95, 0x5f, 0x46, 0x1e, 0x52, 0xdb, 0x18, 0x35, 0x80, 0x30, 0x02,
    0x08, 0x42, 0x3c, 0x95, 0x5f, 0x46, 0x1e, 0x52, 0xdb, 0x18, 0x35, 0x0c, 0x30, 0x01, 0x1d, 0x00,
    0x8a, 0x61, 0x86, 0x62, 0x3d, 0x17, 0xb2, 0xd2, 0xcf, 0xd2, 0x6d, 0x39, 0x3d, 0xe4, 0x25, 0x69,
    0xe0, 0x91, 0xea, 0x05, 0x6a, 0x75, 0xce, 0xdd, 0x45, 0xeb, 0x83, 0xcf, 0x30, 0x02, 0x1c, 0x74,
    0xb4, 0x2b, 0xa4, 0x6d, 0x14, 0x65, 0xb7, 0xb7, 0x71, 0x9a, 0x5a, 0xaf, 0x64, 0xd2, 0x88, 0x60,
    0x6e, 0xb3, 0xb1, 0xa0, 0x31, 0xca, 0x92, 0x6f, 0xca, 0xf2, 0x43, 0x18, 0x18, 0x15, 0x30, 0x01,
    0x09, 0x00, 0xa8, 0x34, 0x22, 0xe9, 0xd9, 0x75, 0xe4, 0x55, 0x24, 0x02, 0x04, 0x57, 0x03, 0x00,
    0x27, 0x13, 0x01, 0x00, 0x00, 0xee, 0xee, 0x30, 0xb4, 0x18, 0x18, 0x26, 0x04, 0x95, 0x23, 0xa9,
    0x19, 0x26, 0x05, 0x15, 0xc1, 0xd2, 0x2c, 0x57, 0x06, 0x00, 0x27, 0x13, 0x01, 0x00, 0x00, 0xee,
    0xee, 0x30, 0xb4, 0x18, 0x18, 0x24, 0x07, 0x02, 0x24, 0x08, 0x15, 0x30, 0x0a, 0x31, 0x04, 0x78,
    0x52, 0xe2, 0x9c, 0x92, 0xba, 0x70, 0x19, 0x58, 0x46, 0x6d, 0xae, 0x18, 0x72, 0x4a, 0xfb, 0x43,
    0x0d, 0xf6, 0x07, 0x29, 0x33, 0x0d, 0x61, 0x55, 0xe5, 0x65, 0x46, 0x8e, 0xba, 0x0d, 0xa5, 0x3f,
    0xb5, 0x17, 0xc0, 0x47, 0x64, 0x44, 0x02, 0x18, 0x4f, 0xa8, 0x11, 0x24, 0x50, 0xd4, 0x7b, 0x35,
    0x83, 0x29, 0x01, 0x29, 0x02, 0x18, 0x35, 0x82, 0x29, 0x01, 0x24, 0x02, 0x60, 0x18, 0x35, 0x81,
    0x30, 0x02, 0x08, 0x42, 0x0c, 0xac, 0xf6, 0xb4, 0x64, 0x71, 0xe6, 0x18, 0x35, 0x80, 0x30, 0x02,
    0x08, 0x42, 0x0c, 0xac, 0xf6, 0xb4, 0x64, 0x71, 0xe6, 0x18, 0x35, 0x0c, 0x30, 0x01, 0x19, 0x00,
    0xbe, 0x0e, 0xda, 0xa1, 0x63, 0x5a, 0x8e, 0xf1, 0x52, 0x17, 0x45, 0x80, 0xbd, 0xdc, 0x94, 0x12,
    0xd4, 0xcc, 0x1c, 0x2c, 0x33, 0x4e, 0x29, 0xdc, 0x30, 0x02, 0x19, 0x00, 0x8b, 0xe7, 0xee, 0x2e,
    0x11, 0x17, 0x14, 0xae, 0x92, 0xda, 0x2b, 0x3b, 0x6d, 0x2f, 0xd7, 0x5d, 0x9e, 0x5f, 0xcd, 0xb8,
    0xba, 0x2f, 0x65, 0x76, 0x18, 0x18, 0x18, 0x35, 0x02, 0x27, 0x01, 0x01, 0x00, 0x00, 0x00, 0x02,
    0x30, 0xb4, 0x18, 0x36, 0x02, 0x15, 0x2c, 0x01, 0x22, 0x66, 0x72, 0x6f, 0x6e, 0x74, 0x64, 0x6f,
    0x6f, 0x72, 0x2e, 0x69, 0x6e, 0x74, 0x65, 0x67, 0x72, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x2e, 0x6e,
    0x65, 0x73, 0x74, 0x6c, 0x61, 0x62, 0x73, 0x2e, 0x63, 0x6f, 0x6d, 0x18, 0x18, 0x18, 0x18};
}  // namespace

class PlatformAuthDelegateTest : public WeaveTestFixture<> {
 public:
  PlatformAuthDelegateTest() {
    context_provider_.service_directory_provider()->AddService(
        fake_weave_signer_.GetHandler(dispatcher()));
  }

  void SetUp() override {
    WeaveTestFixture<>::SetUp();
    WeaveTestFixture<>::RunFixtureLoop();

    PlatformMgrImpl().SetComponentContextForProcess(context_provider_.TakeContext());
    ConfigurationMgrImpl().SetDelegate(std::make_unique<TestConfigurationManager>());
    ThreadStackMgrImpl().SetDelegate(std::make_unique<TestThreadStackManager>());

    // Initialize and set default certificate, service config. Disable the use
    // of the signing key except for tests that explicitly want to provide the
    // test signing key.
    configuration_delegate().InitForTesting();
    configuration_delegate().set_use_signing_key(false);
    EXPECT_EQ(ConfigurationMgr().StoreManufacturerDeviceCertificate(kTestDeviceCert,
                                                                    sizeof(kTestDeviceCert)),
              WEAVE_NO_ERROR);
    EXPECT_EQ(ConfigurationMgr().StoreServiceConfig(kTestServiceConfig, sizeof(kTestServiceConfig)),
              WEAVE_NO_ERROR);
  }

  void TearDown() override {
    WeaveTestFixture<>::StopFixtureLoop();
    WeaveTestFixture<>::TearDown();

    ConfigurationMgrImpl().SetDelegate(nullptr);
    ThreadStackMgrImpl().SetDelegate(nullptr);
  }

 protected:
  FakeWeaveSigner& fake_weave_signer() { return fake_weave_signer_; }
  PlatformAuthDelegate& platform_auth_delegate() { return platform_auth_delegate_; }

  TestConfigurationManager& configuration_delegate() {
    return *reinterpret_cast<TestConfigurationManager*>(ConfigurationMgrImpl().GetDelegate());
  }

 private:
  FakeWeaveSigner fake_weave_signer_{kTestSignedHash, sizeof(kTestSignedHash)};
  PlatformAuthDelegate platform_auth_delegate_;
  sys::testing::ComponentContextProvider context_provider_;
};

TEST_F(PlatformAuthDelegateTest, CASEAuth_EncodeNodePayload) {
  BeginSessionContext context;
  WeaveDeviceDescriptor descriptor;
  uint8_t payload_buf[WeaveDeviceDescriptor::kMaxEncodedTLVLength] = {};
  uint16_t payload_len = 0;

  // Get descriptor used when encoding node payload.
  EXPECT_EQ(ConfigurationMgr().GetDeviceDescriptor(descriptor), WEAVE_NO_ERROR);

  // Acquire node payload and decoded contents to device descriptor.
  WeaveDeviceDescriptor decoded_descriptor;
  EXPECT_EQ(platform_auth_delegate().EncodeNodePayload(context, payload_buf, sizeof(payload_buf),
                                                       payload_len),
            WEAVE_NO_ERROR);
  EXPECT_EQ(WeaveDeviceDescriptor::DecodeTLV(payload_buf, payload_len, decoded_descriptor),
            WEAVE_NO_ERROR);

  // Compare device descriptor from delegate and configuration manager.
  uint8_t descriptor_buf[WeaveDeviceDescriptor::kMaxEncodedTLVLength] = {};
  uint32_t descriptor_buf_len = 0;
  EXPECT_EQ(WeaveDeviceDescriptor::EncodeTLV(descriptor, descriptor_buf, sizeof(descriptor_buf),
                                             descriptor_buf_len),
            WEAVE_NO_ERROR);

  uint8_t decoded_descriptor_buf[WeaveDeviceDescriptor::kMaxEncodedTLVLength] = {};
  uint32_t decoded_descriptor_buf_len = 0;
  EXPECT_EQ(
      WeaveDeviceDescriptor::EncodeTLV(decoded_descriptor, decoded_descriptor_buf,
                                       sizeof(decoded_descriptor_buf), decoded_descriptor_buf_len),
      WEAVE_NO_ERROR);

  EXPECT_EQ(memcmp(descriptor_buf, decoded_descriptor_buf, descriptor_buf_len), 0);
}

TEST_F(PlatformAuthDelegateTest, CASEAuth_EncodeNodePayloadEncodingFailure) {
  BeginSessionContext context;
  WeaveDeviceDescriptor descriptor;
  uint8_t payload_buf[WeaveDeviceDescriptor::kMaxEncodedTLVLength] = {};
  uint16_t payload_len = 0;

  // Get descriptor used when encoding node payload.
  EXPECT_EQ(ConfigurationMgr().GetDeviceDescriptor(descriptor), WEAVE_NO_ERROR);

  // Acquire node payload and decoded contents to device descriptor, but supply
  // insufficient space to encode the descriptor.
  uint64_t payload_len_stored = payload_len;
  EXPECT_EQ(platform_auth_delegate().EncodeNodePayload(context, payload_buf, 0, payload_len),
            WEAVE_ERROR_BUFFER_TOO_SMALL);
  EXPECT_EQ(payload_len, payload_len_stored);
}

TEST_F(PlatformAuthDelegateTest, CASEAuth_EncodeNodeCertInfo) {
  constexpr size_t kMaxCerts = 4;
  constexpr size_t kCertDecodeBufferSize = 800;
  BeginSessionContext context;
  TLVWriter writer;

  PacketBuffer* buffer = PacketBuffer::New();
  writer.Init(buffer);

  EXPECT_EQ(platform_auth_delegate().EncodeNodeCertInfo(context, writer), WEAVE_NO_ERROR);
  EXPECT_EQ(writer.Finalize(), WEAVE_NO_ERROR);

  TLVReader reader;
  reader.Init(buffer, WEAVE_SYSTEM_PACKETBUFFER_SIZE);

  // Verify that the structure of the packet buffer contains sane information
  // from the manufacturer certificate.
  reader.ImplicitProfileId = Profiles::kWeaveProfile_Security;
  EXPECT_EQ(reader.Next(TLV::kTLVType_Structure,
                        TLV::ProfileTag(Profiles::kWeaveProfile_Security,
                                        Security::kTag_WeaveCASECertificateInformation)),
            WEAVE_NO_ERROR);

  WeaveCertificateSet cert_set;
  WeaveCertificateData* cert;

  TLV::TLVType type;
  EXPECT_EQ(reader.EnterContainer(type), WEAVE_NO_ERROR);
  EXPECT_EQ(reader.Next(), WEAVE_NO_ERROR);
  EXPECT_EQ(reader.GetTag(), TLV::ContextTag(Security::kTag_CASECertificateInfo_EntityCertificate));

  // Check that the CommonName of the certificate matches our dummy account id.
  auto release_cert_set = fit::defer([&] { cert_set.Release(); });
  EXPECT_EQ(cert_set.Init(kMaxCerts, kCertDecodeBufferSize), WEAVE_NO_ERROR);
  EXPECT_EQ(cert_set.LoadCert(reader, Security::kDecodeFlag_GenerateTBSHash, cert), WEAVE_NO_ERROR);
  EXPECT_EQ(cert->SubjectDN.AttrOID, ASN1::kOID_AttributeType_CommonName);
  EXPECT_EQ(strncmp((const char*)cert->SubjectDN.AttrValue.String.Value, kTestDeviceCertName,
                    cert->SubjectDN.AttrValue.String.Len),
            0);

  EXPECT_EQ(reader.ExitContainer(type), WEAVE_NO_ERROR);
  EXPECT_EQ(reader.VerifyEndOfContainer(), WEAVE_NO_ERROR);

  PacketBuffer::Free(buffer);
}

TEST_F(PlatformAuthDelegateTest, CASEAuth_EncodeNodeCertInfoInvalidCert) {
  constexpr uint8_t kTestInvalidCert[] = "invalid-cert-data";
  BeginSessionContext context;
  TLVWriter writer;

  PacketBuffer* buffer = PacketBuffer::New();
  writer.Init(buffer);

  EXPECT_EQ(ConfigurationMgr().StoreManufacturerDeviceCertificate(kTestInvalidCert,
                                                                  sizeof(kTestInvalidCert)),
            WEAVE_NO_ERROR);
  EXPECT_EQ(platform_auth_delegate().EncodeNodeCertInfo(context, writer),
            WEAVE_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(writer.Finalize(), WEAVE_NO_ERROR);

  // TODO(fxbug.dev/51891): Test when manufacturer certificate does not exist. This
  // requires initializing ConfigurationMgr from this test, or removing the lazy
  // init from the factory certificate read operation.

  PacketBuffer::Free(buffer);
}

TEST_F(PlatformAuthDelegateTest, CASEAuth_GenerateNodeSignature) {
  BeginSessionContext context;
  TLVWriter writer;

  PacketBuffer* buffer = PacketBuffer::New();
  writer.Init(buffer);

  EXPECT_EQ(
      platform_auth_delegate().GenerateNodeSignature(
          context, kTestHash, sizeof(kTestHash) - 1, writer,
          TLV::ProfileTag(Profiles::kWeaveProfile_Security, Security::kTag_WeaveCASESignature)),
      WEAVE_NO_ERROR);
  EXPECT_EQ(writer.Finalize(), WEAVE_NO_ERROR);

  TLVReader reader;
  reader.Init(buffer, WEAVE_SYSTEM_PACKETBUFFER_SIZE);
  reader.ImplicitProfileId = Profiles::kWeaveProfile_Security;

  TLV::TLVType type;
  EXPECT_EQ(
      reader.Next(TLV::kTLVType_Structure, TLV::ProfileTag(Profiles::kWeaveProfile_Security,
                                                           Security::kTag_WeaveCASESignature)),
      WEAVE_NO_ERROR);
  EXPECT_EQ(reader.EnterContainer(type), WEAVE_NO_ERROR);
  EXPECT_EQ(reader.Next(TLV::kTLVType_ByteString, TLV::ContextTag(Security::kTag_ECDSASignature_r)),
            WEAVE_NO_ERROR);
  EXPECT_EQ(reader.Next(TLV::kTLVType_ByteString, TLV::ContextTag(Security::kTag_ECDSASignature_s)),
            WEAVE_NO_ERROR);

  EXPECT_EQ(reader.ExitContainer(type), WEAVE_NO_ERROR);
  EXPECT_EQ(reader.VerifyEndOfContainer(), WEAVE_NO_ERROR);

  PacketBuffer::Free(buffer);
}

TEST_F(PlatformAuthDelegateTest, CASEAuth_GenerateNodeSignatureInvalidSignedHash) {
  constexpr uint8_t kInvalidSignedHash[] = "invalid-signed-hash";
  PacketBuffer* buffer = PacketBuffer::New();
  BeginSessionContext context;
  TLVWriter writer;

  // Set an invalid signed hash.
  fake_weave_signer().set_signed_hash(kInvalidSignedHash, sizeof(kInvalidSignedHash));
  writer.Init(buffer);
  EXPECT_EQ(
      platform_auth_delegate().GenerateNodeSignature(
          context, kTestHash, sizeof(kTestHash) - 1, writer,
          TLV::ProfileTag(Profiles::kWeaveProfile_Security, Security::kTag_WeaveCASESignature)),
      ASN1_ERROR_INVALID_ENCODING);
  EXPECT_EQ(writer.Finalize(), WEAVE_NO_ERROR);

  // Set an error code when signing.
  fake_weave_signer().set_sign_hash_error(fuchsia::weave::ErrorCode::CRYPTO_ERROR);
  writer.Init(buffer);
  EXPECT_EQ(
      platform_auth_delegate().GenerateNodeSignature(
          context, kTestHash, sizeof(kTestHash) - 1, writer,
          TLV::ProfileTag(Profiles::kWeaveProfile_Security, Security::kTag_WeaveCASESignature)),
      WEAVE_NO_ERROR);
  EXPECT_EQ(writer.Finalize(), WEAVE_NO_ERROR);

  PacketBuffer::Free(buffer);
}

TEST_F(PlatformAuthDelegateTest, CASEAuth_BeginValidation) {
  BeginSessionContext msg_ctx;
  ValidationContext valid_ctx;
  WeaveCertificateSet certs;

  msg_ctx.SetIsInitiator(true);
  EXPECT_EQ(platform_auth_delegate().BeginValidation(msg_ctx, valid_ctx, certs), WEAVE_NO_ERROR);
  EXPECT_EQ(certs.CertCount, 2);
  EXPECT_EQ(certs.Certs[0].CertType, Weave::kCertType_AccessToken);
  EXPECT_EQ(strncmp((const char*)certs.Certs[0].SubjectDN.AttrValue.String.Value,
                    kTestDeviceCertName, certs.Certs[0].SubjectDN.AttrValue.String.Len),
            0);
  EXPECT_EQ(certs.Certs[1].CertType, Weave::kCertType_CA);
  EXPECT_EQ(valid_ctx.RequiredKeyUsages, Security::kKeyUsageFlag_DigitalSignature);
  EXPECT_EQ(valid_ctx.RequiredKeyPurposes, Security::kKeyPurposeFlag_ServerAuth);
  platform_auth_delegate().EndValidation(msg_ctx, valid_ctx, certs);
}

TEST_F(PlatformAuthDelegateTest, CASEAuth_BeginValidationInvalidServiceConfig) {
  constexpr uint8_t kTestInvalidServiceConfig[] = "invalid-service-config";
  BeginSessionContext msg_ctx;
  ValidationContext valid_ctx;
  WeaveCertificateSet certs;

  EXPECT_EQ(ConfigurationMgr().StoreServiceConfig(kTestInvalidServiceConfig,
                                                  sizeof(kTestInvalidServiceConfig)),
            WEAVE_NO_ERROR);
  EXPECT_EQ(platform_auth_delegate().BeginValidation(msg_ctx, valid_ctx, certs),
            WEAVE_ERROR_WRONG_TLV_TYPE);

  ConfigurationMgr().InitiateFactoryReset();
  EXPECT_EQ(platform_auth_delegate().BeginValidation(msg_ctx, valid_ctx, certs),
            WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND);
}

TEST_F(PlatformAuthDelegateTest, CASEAuth_HandleValidationResult) {
  BeginSessionContext msg_ctx;
  ValidationContext valid_ctx;
  WeaveCertificateSet certs;
  WeaveCertificateData signing_cert;
  WEAVE_ERROR result;

  valid_ctx.SigningCert = &signing_cert;

  // Do nothing if validation has already failed.
  result = ~WEAVE_NO_ERROR;
  EXPECT_EQ(platform_auth_delegate().HandleValidationResult(msg_ctx, valid_ctx, certs, result),
            WEAVE_NO_ERROR);
  EXPECT_EQ(result, ~WEAVE_NO_ERROR);

  // Verify behavior when the signing cert is a device certificate. Expect to
  // receive WEAVE_ERROR_WRONG_CERTIFICATE_SUBJECT if the weave ID and peer node
  // ID do not match.
  result = WEAVE_NO_ERROR;
  signing_cert.CertType = Weave::kCertType_Device;
  signing_cert.SubjectDN.AttrValue.WeaveId = 1U;
  msg_ctx.PeerNodeId = signing_cert.SubjectDN.AttrValue.WeaveId + 1;
  EXPECT_EQ(platform_auth_delegate().HandleValidationResult(msg_ctx, valid_ctx, certs, result),
            WEAVE_NO_ERROR);
  EXPECT_EQ(result, WEAVE_ERROR_WRONG_CERTIFICATE_SUBJECT);

  result = WEAVE_NO_ERROR;
  msg_ctx.PeerNodeId = signing_cert.SubjectDN.AttrValue.WeaveId;
  EXPECT_EQ(platform_auth_delegate().HandleValidationResult(msg_ctx, valid_ctx, certs, result),
            WEAVE_NO_ERROR);
  EXPECT_EQ(result, WEAVE_NO_ERROR);

  // Verify behavior when the signing cert is a service endpoint certificate.
  // Expect to receive WEAVE_ERROR_WRONG_CERTIFICATE_SUBJECT if the weave ID and
  // peer node ID do not match.
  result = WEAVE_NO_ERROR;
  signing_cert.CertType = Weave::kCertType_ServiceEndpoint;
  signing_cert.SubjectDN.AttrValue.WeaveId = 1U;
  msg_ctx.PeerNodeId = signing_cert.SubjectDN.AttrValue.WeaveId + 1;
  EXPECT_EQ(platform_auth_delegate().HandleValidationResult(msg_ctx, valid_ctx, certs, result),
            WEAVE_NO_ERROR);
  EXPECT_EQ(result, WEAVE_ERROR_WRONG_CERTIFICATE_SUBJECT);

  result = WEAVE_NO_ERROR;
  msg_ctx.PeerNodeId = signing_cert.SubjectDN.AttrValue.WeaveId;
  EXPECT_EQ(platform_auth_delegate().HandleValidationResult(msg_ctx, valid_ctx, certs, result),
            WEAVE_NO_ERROR);
  EXPECT_EQ(result, WEAVE_NO_ERROR);

  result = WEAVE_NO_ERROR;
  msg_ctx.SetIsInitiator(false);
  EXPECT_EQ(platform_auth_delegate().HandleValidationResult(msg_ctx, valid_ctx, certs, result),
            WEAVE_NO_ERROR);
  EXPECT_EQ(result, WEAVE_ERROR_CERT_USAGE_NOT_ALLOWED);

  // Verify behavior when the signing cert is an access token certificate.
  result = WEAVE_NO_ERROR;
  signing_cert.CertType = Weave::kCertType_AccessToken;
  msg_ctx.SetIsInitiator(false);
  EXPECT_EQ(platform_auth_delegate().HandleValidationResult(msg_ctx, valid_ctx, certs, result),
            WEAVE_NO_ERROR);
  EXPECT_EQ(result, WEAVE_NO_ERROR);

  result = WEAVE_NO_ERROR;
  msg_ctx.SetIsInitiator(true);
  EXPECT_EQ(platform_auth_delegate().HandleValidationResult(msg_ctx, valid_ctx, certs, result),
            WEAVE_NO_ERROR);
  EXPECT_EQ(result, WEAVE_ERROR_CERT_USAGE_NOT_ALLOWED);

  // Verify behavior when the signing cert is not a valid type.
  result = WEAVE_NO_ERROR;
  signing_cert.CertType = Weave::kCertType_General;
  EXPECT_EQ(platform_auth_delegate().HandleValidationResult(msg_ctx, valid_ctx, certs, result),
            WEAVE_NO_ERROR);
  EXPECT_EQ(result, WEAVE_ERROR_CERT_USAGE_NOT_ALLOWED);
}

TEST_F(PlatformAuthDelegateTest, CASEAuth_GenerateNodeSignatureWithPrivateKey) {
  BeginSessionContext context;
  TLVWriter writer;

  // Provide a test signing key for validation.
  configuration_delegate().set_signing_key(
      std::vector<uint8_t>(kTestPrivateKey, kTestPrivateKey + sizeof(kTestPrivateKey)));
  configuration_delegate().set_use_signing_key(true);

  PacketBuffer* buffer = PacketBuffer::New();
  writer.Init(buffer);
  EXPECT_EQ(
      platform_auth_delegate().GenerateNodeSignature(
          context, kTestHash, sizeof(kTestHash), writer,
          TLV::ProfileTag(Profiles::kWeaveProfile_Security, Security::kTag_WeaveCASESignature)),
      WEAVE_NO_ERROR);
  EXPECT_EQ(writer.Finalize(), WEAVE_NO_ERROR);

  TLVReader reader;
  reader.Init(buffer, WEAVE_SYSTEM_PACKETBUFFER_SIZE);
  reader.ImplicitProfileId = Profiles::kWeaveProfile_Security;

  TLV::TLVType type;
  EXPECT_EQ(
      reader.Next(TLV::kTLVType_Structure, TLV::ProfileTag(Profiles::kWeaveProfile_Security,
                                                           Security::kTag_WeaveCASESignature)),
      WEAVE_NO_ERROR);
  EXPECT_EQ(reader.EnterContainer(type), WEAVE_NO_ERROR);
  EXPECT_EQ(reader.Next(TLV::kTLVType_ByteString, TLV::ContextTag(Security::kTag_ECDSASignature_r)),
            WEAVE_NO_ERROR);
  EXPECT_EQ(reader.Next(TLV::kTLVType_ByteString, TLV::ContextTag(Security::kTag_ECDSASignature_s)),
            WEAVE_NO_ERROR);

  EXPECT_EQ(reader.ExitContainer(type), WEAVE_NO_ERROR);
  EXPECT_EQ(reader.VerifyEndOfContainer(), WEAVE_NO_ERROR);

  PacketBuffer::Free(buffer);
}

TEST_F(PlatformAuthDelegateTest, KeyExport_GetNodeCertSet) {
  WeaveCertificateSet cert_set;
  WeaveKeyExport key_export;

  // Check that we can populate the certificates into a set.
  EXPECT_EQ(platform_auth_delegate().GetNodeCertSet(&key_export, cert_set), WEAVE_NO_ERROR);
  EXPECT_EQ(cert_set.CertCount, 1U);

  // Check that the CommonName of the certificate matches our dummy account id.
  WeaveCertificateData* cert = cert_set.Certs;
  EXPECT_EQ(cert->SubjectDN.AttrOID, ASN1::kOID_AttributeType_CommonName);
  EXPECT_EQ(strncmp((const char*)cert->SubjectDN.AttrValue.String.Value, kTestDeviceCertName,
                    cert->SubjectDN.AttrValue.String.Len),
            0);

  // Clean up the certificates.
  EXPECT_EQ(platform_auth_delegate().ReleaseNodeCertSet(&key_export, cert_set), WEAVE_NO_ERROR);
}

TEST_F(PlatformAuthDelegateTest, KeyExport_GetNodeCertSetInvalidCert) {
  constexpr uint8_t kTestInvalidCert[] = "invalid-cert-data";
  WeaveCertificateSet cert_set;
  WeaveKeyExport key_export;

  // Populate some invalid certificate data.
  EXPECT_EQ(ConfigurationMgr().StoreManufacturerDeviceCertificate(kTestInvalidCert,
                                                                  sizeof(kTestInvalidCert)),
            WEAVE_NO_ERROR);

  // Check that we fail to populate the certificates into a set.
  EXPECT_NE(platform_auth_delegate().GetNodeCertSet(&key_export, cert_set), WEAVE_NO_ERROR);
}

TEST_F(PlatformAuthDelegateTest, KeyExport_HandleCertValidationResult) {
  constexpr uint8_t kAttrValue[] = "TEST";
  WeaveKeyExport key_export;
  ValidationContext valid_ctx;
  WeaveCertificateSet cert_set;
  WeaveCertificateData signing_cert;
  WeaveCertificateData valid_signing_cert;

  // Construct valid signing certificate.
  valid_ctx.SigningCert = &signing_cert;
  valid_ctx.SigningCert->CertFlags |= Security::kCertFlag_IsTrusted;
  valid_ctx.SigningCert->SubjectDN.AttrOID = ASN1::kOID_AttributeType_CommonName;
  valid_ctx.SigningCert->SubjectDN.AttrValue.String.Value = kAttrValue;
  valid_ctx.SigningCert->SubjectDN.AttrValue.String.Len = sizeof(kAttrValue);
  valid_ctx.SigningCert->IssuerDN = valid_ctx.SigningCert->SubjectDN;
  valid_signing_cert = signing_cert;

  valid_ctx.SigningCert->IssuerDN.IsEqual(valid_ctx.SigningCert->SubjectDN);

  // Approve if self-signed, trusted, matching SubjectDN + IssuerDN, CommonName.
  EXPECT_EQ(platform_auth_delegate().HandleCertValidationResult(&key_export, valid_ctx, cert_set,
                                                                WeaveKeyId::kClientRootKey),
            WEAVE_NO_ERROR);

  // Reject if requested key ID is not a client root key.
  EXPECT_EQ(platform_auth_delegate().HandleCertValidationResult(&key_export, valid_ctx, cert_set,
                                                                WeaveKeyId::kFabricRootKey),
            WEAVE_ERROR_UNAUTHORIZED_KEY_EXPORT_RESPONSE);

  // Reject if cert flags are not trusted.
  valid_ctx.SigningCert->CertFlags &= ~Security::kCertFlag_IsTrusted;
  EXPECT_EQ(platform_auth_delegate().HandleCertValidationResult(&key_export, valid_ctx, cert_set,
                                                                WeaveKeyId::kClientRootKey),
            WEAVE_ERROR_UNAUTHORIZED_KEY_EXPORT_RESPONSE);
  *valid_ctx.SigningCert = valid_signing_cert;

  // Reject if SubjectDN doesn't have the CommonName attribute.
  valid_ctx.SigningCert->SubjectDN.AttrOID = ASN1::kOID_AttributeType_WeaveDeviceId;
  EXPECT_EQ(platform_auth_delegate().HandleCertValidationResult(&key_export, valid_ctx, cert_set,
                                                                WeaveKeyId::kClientRootKey),
            WEAVE_ERROR_UNAUTHORIZED_KEY_EXPORT_RESPONSE);
  *valid_ctx.SigningCert = valid_signing_cert;

  // Reject if IssuerDN doesn't match SubjectDN.
  valid_ctx.SigningCert->IssuerDN.AttrOID = ~valid_ctx.SigningCert->SubjectDN.AttrOID;
  EXPECT_EQ(platform_auth_delegate().HandleCertValidationResult(&key_export, valid_ctx, cert_set,
                                                                WeaveKeyId::kClientRootKey),
            WEAVE_ERROR_UNAUTHORIZED_KEY_EXPORT_RESPONSE);
  *valid_ctx.SigningCert = valid_signing_cert;
}

// TODO(fxb/51892): Add tests for intermediate CA certs.

}  // namespace weave::adaptation::testing
