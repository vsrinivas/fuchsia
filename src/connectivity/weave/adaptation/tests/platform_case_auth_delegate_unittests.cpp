// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include "platform_case_auth_delegate.h"
#include "configuration_manager_delegate_impl.h"
// clang-format on

#include <fuchsia/weave/cpp/fidl.h>
#include <fuchsia/weave/cpp/fidl_test_base.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <Weave/Core/WeaveTLV.h>

#include "weave_test_fixture.h"

namespace nl::Weave::DeviceLayer::Internal {
namespace testing {

namespace {

using Profiles::kWeaveProfile_Security;
using Profiles::kWeaveProfile_ServiceProvisioning;
using Profiles::Security::kKeyPurposeFlag_ServerAuth;
using Profiles::Security::kKeyUsageFlag_DigitalSignature;
using Profiles::Security::kValidateFlag_IgnoreNotBefore;
using Profiles::Security::WeaveCertificateData;

using nl::Weave::Profiles::DeviceDescription::WeaveDeviceDescriptor;
using nl::Weave::Profiles::Security::kTag_CASECertificateInfo_EntityCertificate;
using nl::Weave::Profiles::Security::kTag_ECDSASignature;
using nl::Weave::Profiles::Security::kTag_ECDSASignature_r;
using nl::Weave::Profiles::Security::kTag_ECDSASignature_s;
using nl::Weave::Profiles::Security::kTag_WeaveCASECertificateInformation;
using nl::Weave::Profiles::Security::kTag_WeaveCASESignature;
using nl::Weave::TLV::kTLVType_ByteString;
using nl::Weave::TLV::kTLVType_Structure;

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
constexpr char kTestHash[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ123456";
constexpr uint8_t kTestSignedHash[] = {
    0x30, 0x3d, 0x02, 0x1d, 0x00, 0xdf, 0x70, 0x31, 0xb3, 0xbe, 0x63, 0x80, 0xc9, 0xd0, 0xd8, 0x67,
    0x70, 0xf5, 0xa6, 0x50, 0xf1, 0x18, 0xb4, 0x42, 0x1a, 0x5a, 0x72, 0x68, 0xd0, 0x13, 0x0c, 0xd6,
    0xda, 0x02, 0x1c, 0x59, 0xba, 0x2d, 0x92, 0xc0, 0x49, 0x94, 0x22, 0xbd, 0xb2, 0x46, 0x8c, 0x78,
    0x8c, 0x44, 0x59, 0xd9, 0xbc, 0x13, 0xe6, 0x3a, 0x93, 0x85, 0x5c, 0xe9, 0x04, 0x53, 0xe7};

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

class FakeWeaveSigner : public fuchsia::weave::testing::Signer_TestBase {
 public:
  FakeWeaveSigner()
      : send_error_code_(false),
        signed_hash_(kTestSignedHash),
        signed_hash_size_(sizeof(kTestSignedHash)) {}

  void NotImplemented_(const std::string& name) override { FAIL() << __func__; }

  void SignHash(std::vector<uint8_t> hash, SignHashCallback callback) override {
    std::vector<uint8_t> signed_hash(signed_hash_, signed_hash_ + signed_hash_size_);
    fuchsia::weave::Signer_SignHash_Response response(signed_hash);
    fuchsia::weave::Signer_SignHash_Result result;

    if (send_error_code_) {
      result.set_err(error_code_);
    } else {
      result.set_response(std::move(response));
    }
    callback(std::move(result));
  }

  fidl::InterfaceRequestHandler<fuchsia::weave::Signer> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    dispatcher_ = dispatcher;
    return [this, dispatcher](fidl::InterfaceRequest<fuchsia::weave::Signer> request) {
      binding_.Bind(std::move(request), dispatcher);
    };
  }

  void SetSignedHash(const uint8_t* signed_hash, size_t signed_hash_size) {
    signed_hash_ = signed_hash;
    signed_hash_size_ = signed_hash_size;
  }

  void SetErrorCode(fuchsia::weave::ErrorCode error_code) {
    send_error_code_ = true;
    error_code_ = error_code;
  }

 private:
  fidl::Binding<fuchsia::weave::Signer> binding_{this};
  async_dispatcher_t* dispatcher_;

  bool send_error_code_;
  fuchsia::weave::ErrorCode error_code_;
  const uint8_t* signed_hash_;
  size_t signed_hash_size_;
};

class ConfigurationManagerTestDelegateImpl : public ConfigurationManagerDelegateImpl {
 public:
  WEAVE_ERROR GetDeviceDescriptorTLV(uint8_t* buf, size_t buf_size, size_t& encoded_len) override {
    return WeaveDeviceDescriptor::EncodeTLV(descriptor_, buf, (uint32_t)buf_size,
                                            (uint32_t&)encoded_len);
  }

  void SetDeviceDescriptor(WeaveDeviceDescriptor descriptor) { descriptor_ = descriptor; }

 private:
  WeaveDeviceDescriptor descriptor_;
};

class PlatformCASEAuthDelegateTest : public WeaveTestFixture {
 public:
  PlatformCASEAuthDelegateTest() {
    context_provider_.service_directory_provider()->AddService(
        fake_weave_signer_.GetHandler(dispatcher()));
    platform_case_auth_delegate_ =
        std::make_unique<PlatformCASEAuthDelegate>(context_provider_.TakeContext());
  }

  void SetUp() {
    WeaveTestFixture::SetUp();
    WeaveTestFixture::RunFixtureLoop();

    ConfigurationMgrImpl().SetDelegate(std::make_unique<ConfigurationManagerTestDelegateImpl>());
    EXPECT_EQ(ConfigurationMgrImpl().GetDelegate()->Init(), WEAVE_NO_ERROR);
    // Initialize dummy certificate and service configuration.
    EXPECT_EQ(ConfigurationMgr().StoreManufacturerDeviceCertificate(kTestDeviceCert,
                                                                    sizeof(kTestDeviceCert)),
              WEAVE_NO_ERROR);
    EXPECT_EQ(ConfigurationMgr().StoreServiceConfig((const uint8_t*)kTestServiceConfig,
                                                    sizeof(kTestServiceConfig)),
              WEAVE_NO_ERROR);
  }

  void TearDown() {
    WeaveTestFixture::StopFixtureLoop();
    WeaveTestFixture::TearDown();
  }

 protected:
  std::unique_ptr<PlatformCASEAuthDelegate> platform_case_auth_delegate_;
  sys::testing::ComponentContextProvider context_provider_;

  FakeWeaveSigner fake_weave_signer_;
};

TEST_F(PlatformCASEAuthDelegateTest, EncodeNodePayload) {
  BeginSessionContext context;
  WeaveDeviceDescriptor descriptor;
  uint8_t payload_buf[WeaveDeviceDescriptor::kMaxPairingCodeLength + 1];
  uint16_t payload_len;

  // Set dummy descriptor to return when encoding node payload.
  ConfigurationManagerTestDelegateImpl* configuration_manager_delegate =
      static_cast<ConfigurationManagerTestDelegateImpl*>(ConfigurationMgrImpl().GetDelegate());
  configuration_manager_delegate->SetDeviceDescriptor(descriptor);

  // Acquire node payload and decoded contents to device descriptor.
  WeaveDeviceDescriptor decoded_descriptor;
  EXPECT_EQ(platform_case_auth_delegate_->EncodeNodePayload(context, payload_buf,
                                                            sizeof(payload_buf), payload_len),
            WEAVE_NO_ERROR);
  EXPECT_EQ(WeaveDeviceDescriptor::DecodeTLV(payload_buf, payload_len, decoded_descriptor),
            WEAVE_NO_ERROR);

  // Compare device descriptor from delegate and configuration manager.
  uint8_t descriptor_buf[WeaveDeviceDescriptor::kMaxPairingCodeLength + 1];
  uint32_t descriptor_buf_len;
  EXPECT_EQ(WeaveDeviceDescriptor::EncodeTLV(descriptor, descriptor_buf, sizeof(descriptor_buf),
                                             descriptor_buf_len),
            WEAVE_NO_ERROR);

  uint8_t decoded_descriptor_buf[WeaveDeviceDescriptor::kMaxPairingCodeLength + 1];
  uint32_t decoded_descriptor_buf_len;
  EXPECT_EQ(
      WeaveDeviceDescriptor::EncodeTLV(decoded_descriptor, decoded_descriptor_buf,
                                       sizeof(decoded_descriptor_buf), decoded_descriptor_buf_len),
      WEAVE_NO_ERROR);

  EXPECT_EQ(memcmp(descriptor_buf, decoded_descriptor_buf, descriptor_buf_len), 0);
}

TEST_F(PlatformCASEAuthDelegateTest, EncodeNodePayloadEncodingFailure) {
  BeginSessionContext context;
  WeaveDeviceDescriptor descriptor;
  uint8_t payload_buf[WeaveDeviceDescriptor::kMaxPairingCodeLength + 1];
  uint16_t payload_len = 0U;

  // Set dummy descriptor to return when encoding node payload.
  ConfigurationManagerTestDelegateImpl* configuration_manager_delegate =
      static_cast<ConfigurationManagerTestDelegateImpl*>(ConfigurationMgrImpl().GetDelegate());
  configuration_manager_delegate->SetDeviceDescriptor(descriptor);

  // Acquire node payload and decoded contents to device descriptor, but supply
  // insufficient space to encode the descriptor.
  uint64_t payload_len_stored = payload_len;
  EXPECT_EQ(platform_case_auth_delegate_->EncodeNodePayload(context, payload_buf, 0, payload_len),
            WEAVE_ERROR_BUFFER_TOO_SMALL);
  EXPECT_EQ(payload_len, payload_len_stored);
}

TEST_F(PlatformCASEAuthDelegateTest, EncodeNodeCertInfo) {
  constexpr size_t kMaxCerts = 4;
  constexpr size_t kCertDecodeBufferSize = 800;
  BeginSessionContext context;
  TLVWriter writer;

  PacketBuffer* buffer = PacketBuffer::New();
  writer.Init(buffer);

  EXPECT_EQ(platform_case_auth_delegate_->EncodeNodeCertInfo(context, writer), WEAVE_NO_ERROR);
  EXPECT_EQ(writer.Finalize(), WEAVE_NO_ERROR);

  TLVReader reader;
  reader.Init(buffer, WEAVE_SYSTEM_PACKETBUFFER_SIZE);

  // Verify that the structure of the packet buffer contains sane information
  // from the manufacturer certificate.
  reader.ImplicitProfileId = Profiles::kWeaveProfile_Security;
  EXPECT_EQ(reader.Next(kTLVType_Structure, TLV::ProfileTag(kWeaveProfile_Security,
                                                            kTag_WeaveCASECertificateInformation)),
            WEAVE_NO_ERROR);

  WeaveCertificateSet cert_set;
  Profiles::Security::WeaveCertificateData* cert;

  TLV::TLVType type;
  EXPECT_EQ(reader.EnterContainer(type), WEAVE_NO_ERROR);
  EXPECT_EQ(reader.Next(), WEAVE_NO_ERROR);
  EXPECT_EQ(reader.GetTag(), TLV::ContextTag(kTag_CASECertificateInfo_EntityCertificate));

  // Check that the CommonName of the certificate matches our dummy account id.
  EXPECT_EQ(cert_set.Init(kMaxCerts, kCertDecodeBufferSize), WEAVE_NO_ERROR);
  EXPECT_EQ(cert_set.LoadCert(reader, Profiles::Security::kDecodeFlag_GenerateTBSHash, cert),
            WEAVE_NO_ERROR);
  EXPECT_EQ(cert->SubjectDN.AttrOID, ASN1::kOID_AttributeType_CommonName);
  EXPECT_EQ(strncmp((const char*)cert->SubjectDN.AttrValue.String.Value, kTestDeviceCertName,
                    cert->SubjectDN.AttrValue.String.Len),
            0);

  EXPECT_EQ(reader.ExitContainer(type), WEAVE_NO_ERROR);
  EXPECT_EQ(reader.VerifyEndOfContainer(), WEAVE_NO_ERROR);

  PacketBuffer::Free(buffer);
}

TEST_F(PlatformCASEAuthDelegateTest, EncodeNodeCertInfoInvalidCert) {
  constexpr char kTestInvalidCert[] = "invalid-cert-data";
  BeginSessionContext context;
  TLVWriter writer;

  PacketBuffer* buffer = PacketBuffer::New();
  writer.Init(buffer);

  EXPECT_EQ(ConfigurationMgr().StoreManufacturerDeviceCertificate((const uint8_t*)kTestInvalidCert,
                                                                  sizeof(kTestInvalidCert)),
            WEAVE_NO_ERROR);
  EXPECT_EQ(platform_case_auth_delegate_->EncodeNodeCertInfo(context, writer),
            WEAVE_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(writer.Finalize(), WEAVE_NO_ERROR);

  // TODO(fxbug.dev/51891): Test when manufacturer certificate does not exist. This
  // requires initializing ConfigurationMgr from this test, or removing the lazy
  // init from the factory certificate read operation.

  PacketBuffer::Free(buffer);
}

TEST_F(PlatformCASEAuthDelegateTest, GenerateNodeSignature) {
  BeginSessionContext context;
  TLVWriter writer;

  PacketBuffer* buffer = PacketBuffer::New();
  writer.Init(buffer);

  EXPECT_EQ(platform_case_auth_delegate_->GenerateNodeSignature(
                context, (const uint8_t*)kTestHash, sizeof(kTestHash) - 1, writer,
                TLV::ProfileTag(kWeaveProfile_Security, kTag_WeaveCASESignature)),
            WEAVE_NO_ERROR);
  EXPECT_EQ(writer.Finalize(), WEAVE_NO_ERROR);

  TLVReader reader;
  reader.Init(buffer, WEAVE_SYSTEM_PACKETBUFFER_SIZE);
  reader.ImplicitProfileId = kWeaveProfile_Security;

  TLV::TLVType type;
  EXPECT_EQ(reader.Next(kTLVType_Structure,
                        TLV::ProfileTag(kWeaveProfile_Security, kTag_WeaveCASESignature)),
            WEAVE_NO_ERROR);
  EXPECT_EQ(reader.EnterContainer(type), WEAVE_NO_ERROR);
  EXPECT_EQ(reader.Next(kTLVType_ByteString, TLV::ContextTag(kTag_ECDSASignature_r)),
            WEAVE_NO_ERROR);
  EXPECT_EQ(reader.Next(kTLVType_ByteString, TLV::ContextTag(kTag_ECDSASignature_s)),
            WEAVE_NO_ERROR);

  EXPECT_EQ(reader.ExitContainer(type), WEAVE_NO_ERROR);
  EXPECT_EQ(reader.VerifyEndOfContainer(), WEAVE_NO_ERROR);

  PacketBuffer::Free(buffer);
}

TEST_F(PlatformCASEAuthDelegateTest, GenerateNodeSignatureInvalidSignatureOrHash) {
  constexpr char kInvalidHash[] = "invalid-hash";
  constexpr char kInvalidSignedHash[] = "invalid-signed-hash";
  BeginSessionContext context;
  TLVWriter writer;

  PacketBuffer* buffer = PacketBuffer::New();
  writer.Init(buffer);

  EXPECT_EQ(platform_case_auth_delegate_->GenerateNodeSignature(
                context, (const uint8_t*)kInvalidHash, sizeof(kInvalidHash) - 1, writer,
                TLV::ProfileTag(kWeaveProfile_Security, kTag_WeaveCASESignature)),
            WEAVE_NO_ERROR);
  EXPECT_EQ(writer.Finalize(), WEAVE_NO_ERROR);

  writer.Init(buffer);
  fake_weave_signer_.SetSignedHash((const uint8_t*)kInvalidSignedHash,
                                   sizeof(kInvalidSignedHash) - 1);
  EXPECT_EQ(platform_case_auth_delegate_->GenerateNodeSignature(
                context, (const uint8_t*)kTestHash, sizeof(kTestHash) - 1, writer,
                TLV::ProfileTag(kWeaveProfile_Security, kTag_WeaveCASESignature)),
            ASN1_ERROR_INVALID_ENCODING);
  EXPECT_EQ(writer.Finalize(), WEAVE_NO_ERROR);

  writer.Init(buffer);
  fake_weave_signer_.SetErrorCode(fuchsia::weave::ErrorCode::CRYPTO_ERROR);
  EXPECT_EQ(platform_case_auth_delegate_->GenerateNodeSignature(
                context, (const uint8_t*)kTestHash, sizeof(kTestHash) - 1, writer,
                TLV::ProfileTag(kWeaveProfile_Security, kTag_WeaveCASESignature)),
            WEAVE_NO_ERROR);
  EXPECT_EQ(writer.Finalize(), WEAVE_NO_ERROR);

  PacketBuffer::Free(buffer);
}

TEST_F(PlatformCASEAuthDelegateTest, BeginValidation) {
  BeginSessionContext msg_ctx;
  ValidationContext valid_ctx;
  WeaveCertificateSet certs;

  msg_ctx.SetIsInitiator(true);
  EXPECT_EQ(platform_case_auth_delegate_->BeginValidation(msg_ctx, valid_ctx, certs),
            WEAVE_NO_ERROR);
  EXPECT_EQ(certs.CertCount, 2);
  EXPECT_EQ(certs.Certs[0].CertType, kCertType_AccessToken);
  EXPECT_EQ(strncmp((const char*)certs.Certs[0].SubjectDN.AttrValue.String.Value,
                    kTestDeviceCertName, certs.Certs[0].SubjectDN.AttrValue.String.Len),
            0);
  EXPECT_EQ(certs.Certs[1].CertType, kCertType_CA);
  EXPECT_EQ(valid_ctx.RequiredKeyUsages, kKeyUsageFlag_DigitalSignature);
  EXPECT_EQ(valid_ctx.RequiredKeyPurposes, kKeyPurposeFlag_ServerAuth);
}

TEST_F(PlatformCASEAuthDelegateTest, BeginValidationInvalidServiceConfig) {
  constexpr char kTestInvalidServiceConfig[] = "invalid-service-config";
  BeginSessionContext msg_ctx;
  ValidationContext valid_ctx;
  WeaveCertificateSet certs;

  EXPECT_EQ(ConfigurationMgr().StoreServiceConfig((const uint8_t*)kTestInvalidServiceConfig,
                                                  sizeof(kTestInvalidServiceConfig)),
            WEAVE_NO_ERROR);
  EXPECT_EQ(platform_case_auth_delegate_->BeginValidation(msg_ctx, valid_ctx, certs),
            WEAVE_ERROR_WRONG_TLV_TYPE);

  ConfigurationMgr().InitiateFactoryReset();
  EXPECT_EQ(platform_case_auth_delegate_->BeginValidation(msg_ctx, valid_ctx, certs),
            WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND);
}

TEST_F(PlatformCASEAuthDelegateTest, HandleValidationResult) {
  BeginSessionContext msg_ctx;
  ValidationContext valid_ctx;
  WeaveCertificateSet certs;
  WeaveCertificateData signing_cert;
  WEAVE_ERROR result;

  valid_ctx.SigningCert = &signing_cert;

  // Do nothing if validation has already failed.
  result = ~WEAVE_NO_ERROR;
  EXPECT_EQ(platform_case_auth_delegate_->HandleValidationResult(msg_ctx, valid_ctx, certs, result),
            WEAVE_NO_ERROR);
  EXPECT_EQ(result, ~WEAVE_NO_ERROR);

  // Verify behavior when the signing cert is a device certificate. Expect to
  // receive WEAVE_ERROR_WRONG_CERTIFICATE_SUBJECT if the weave ID and peer node
  // ID do not match.
  result = WEAVE_NO_ERROR;
  signing_cert.CertType = kCertType_Device;
  signing_cert.SubjectDN.AttrValue.WeaveId = 1U;
  msg_ctx.PeerNodeId = signing_cert.SubjectDN.AttrValue.WeaveId + 1;
  EXPECT_EQ(platform_case_auth_delegate_->HandleValidationResult(msg_ctx, valid_ctx, certs, result),
            WEAVE_NO_ERROR);
  EXPECT_EQ(result, WEAVE_ERROR_WRONG_CERTIFICATE_SUBJECT);

  result = WEAVE_NO_ERROR;
  msg_ctx.PeerNodeId = signing_cert.SubjectDN.AttrValue.WeaveId;
  EXPECT_EQ(platform_case_auth_delegate_->HandleValidationResult(msg_ctx, valid_ctx, certs, result),
            WEAVE_NO_ERROR);
  EXPECT_EQ(result, WEAVE_NO_ERROR);

  // Verify behavior when the signing cert is a service endpoint certificate.
  // Expect to receive WEAVE_ERROR_WRONG_CERTIFICATE_SUBJECT if the weave ID and
  // peer node ID do not match.
  result = WEAVE_NO_ERROR;
  signing_cert.CertType = kCertType_ServiceEndpoint;
  signing_cert.SubjectDN.AttrValue.WeaveId = 1U;
  msg_ctx.PeerNodeId = signing_cert.SubjectDN.AttrValue.WeaveId + 1;
  EXPECT_EQ(platform_case_auth_delegate_->HandleValidationResult(msg_ctx, valid_ctx, certs, result),
            WEAVE_NO_ERROR);
  EXPECT_EQ(result, WEAVE_ERROR_WRONG_CERTIFICATE_SUBJECT);

  result = WEAVE_NO_ERROR;
  msg_ctx.PeerNodeId = signing_cert.SubjectDN.AttrValue.WeaveId;
  EXPECT_EQ(platform_case_auth_delegate_->HandleValidationResult(msg_ctx, valid_ctx, certs, result),
            WEAVE_NO_ERROR);
  EXPECT_EQ(result, WEAVE_NO_ERROR);

  result = WEAVE_NO_ERROR;
  msg_ctx.SetIsInitiator(false);
  EXPECT_EQ(platform_case_auth_delegate_->HandleValidationResult(msg_ctx, valid_ctx, certs, result),
            WEAVE_NO_ERROR);
  EXPECT_EQ(result, WEAVE_ERROR_CERT_USAGE_NOT_ALLOWED);

  // Verify behavior when the signing cert is an access token certificate.
  result = WEAVE_NO_ERROR;
  signing_cert.CertType = kCertType_AccessToken;
  msg_ctx.SetIsInitiator(false);
  EXPECT_EQ(platform_case_auth_delegate_->HandleValidationResult(msg_ctx, valid_ctx, certs, result),
            WEAVE_NO_ERROR);
  EXPECT_EQ(result, WEAVE_NO_ERROR);

  result = WEAVE_NO_ERROR;
  msg_ctx.SetIsInitiator(true);
  EXPECT_EQ(platform_case_auth_delegate_->HandleValidationResult(msg_ctx, valid_ctx, certs, result),
            WEAVE_NO_ERROR);
  EXPECT_EQ(result, WEAVE_ERROR_CERT_USAGE_NOT_ALLOWED);

  // Verify behavior when the signing cert is not a valid type.
  result = WEAVE_NO_ERROR;
  signing_cert.CertType = kCertType_General;
  EXPECT_EQ(platform_case_auth_delegate_->HandleValidationResult(msg_ctx, valid_ctx, certs, result),
            WEAVE_NO_ERROR);
  EXPECT_EQ(result, WEAVE_ERROR_CERT_USAGE_NOT_ALLOWED);
}

// TODO(fxbug.dev/51892): Add tests for intermediate CA certs.

}  // namespace testing
}  // namespace nl::Weave::DeviceLayer::Internal
