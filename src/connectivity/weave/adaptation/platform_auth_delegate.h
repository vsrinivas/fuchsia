// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_PLATFORM_AUTH_DELEGATE_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_PLATFORM_AUTH_DELEGATE_H_

// clang-format off
#pragma GCC diagnostic push
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/Core/WeaveTLV.h>
#include <Weave/Profiles/WeaveProfiles.h>
#include <Weave/Profiles/security/WeaveSecurity.h>
#include <Weave/Profiles/security/WeaveCert.h>
#include <Weave/Profiles/security/WeaveCASE.h>
#include <Weave/Profiles/security/WeaveKeyExport.h>
#include <Weave/Profiles/service-provisioning/ServiceProvisioning.h>
#include <Weave/Support/NestCerts.h>
#include <Weave/Support/ASN1.h>
#include <Weave/Support/TimeUtils.h>
#pragma GCC diagnostic pop
// clang-format on

namespace nl {
namespace Weave {
namespace DeviceLayer {
namespace Internal {

using nl::Weave::Profiles::Security::ValidationContext;
using nl::Weave::Profiles::Security::WeaveCertificateSet;
using nl::Weave::Profiles::Security::CASE::BeginSessionContext;
using nl::Weave::Profiles::Security::KeyExport::WeaveKeyExportDelegate;

class PlatformAuthDelegate final : public WeaveCASEAuthDelegate, public WeaveKeyExportDelegate {
 public:
  PlatformAuthDelegate() = default;
  ~PlatformAuthDelegate() = default;

  // nl::Weave::Profiles::Security::CASE::WeaveCASEAuthDelegate implementation
  WEAVE_ERROR EncodeNodePayload(const BeginSessionContext& msg_ctx, uint8_t* payload_buf,
                                uint16_t payload_buf_size, uint16_t& payload_len) override;
  WEAVE_ERROR EncodeNodeCertInfo(const BeginSessionContext& msg_ctx, TLVWriter& writer) override;
  WEAVE_ERROR GenerateNodeSignature(const BeginSessionContext& msg_ctx, const uint8_t* msg_hash,
                                    uint8_t msg_hash_len, TLVWriter& writer, uint64_t tag) override;
  WEAVE_ERROR BeginValidation(const BeginSessionContext& msg_ctx, ValidationContext& valid_ctx,
                              WeaveCertificateSet& cert_set) override;
  WEAVE_ERROR HandleValidationResult(const BeginSessionContext& msg_ctx,
                                     ValidationContext& valid_ctx, WeaveCertificateSet& cert_set,
                                     WEAVE_ERROR& valid_res) override;
  void EndValidation(const BeginSessionContext& msg_ctx, ValidationContext& valid_ctx,
                     WeaveCertificateSet& cert_set) override;

  // nl::Weave::Profiles::Security::KeyExport::WeaveKeyExportDelegate
  WEAVE_ERROR GetNodeCertSet(WeaveKeyExport* key_export, WeaveCertificateSet& cert_set) override;
  WEAVE_ERROR ReleaseNodeCertSet(WeaveKeyExport* key_export,
                                 WeaveCertificateSet& cert_set) override;
  WEAVE_ERROR GenerateNodeSignature(WeaveKeyExport* key_export, const uint8_t* msg_hash,
                                    uint8_t msg_hash_len, TLVWriter& writer) override;
  WEAVE_ERROR BeginCertValidation(WeaveKeyExport* key_export, ValidationContext& valid_ctx,
                                  WeaveCertificateSet& cert_set) override;
  WEAVE_ERROR HandleCertValidationResult(WeaveKeyExport* key_export, ValidationContext& valid_ctx,
                                         WeaveCertificateSet& cert_set,
                                         uint32_t requested_key_id) override;
  WEAVE_ERROR EndCertValidation(WeaveKeyExport* key_export, ValidationContext& valid_ctx,
                                WeaveCertificateSet& cert_set) override;
  WEAVE_ERROR ValidateUnsignedKeyExportMessage(WeaveKeyExport* key_export,
                                               uint32_t requested_key_id) override;

 private:
  WEAVE_ERROR GetNodeCertificates(std::vector<uint8_t>& device_cert,
                                  std::vector<uint8_t>& device_intermediate_certs);

  WEAVE_ERROR GenerateNodeSignature(const uint8_t* msg_hash, uint8_t msg_hash_len,
                                    TLVWriter& writer, uint64_t tag);

  WEAVE_ERROR BeginCertValidation(ValidationContext& valid_ctx, WeaveCertificateSet& cert_set,
                                  bool is_initiator);

  WEAVE_ERROR LoadCertsFromServiceConfig(const uint8_t* service_config, uint16_t service_config_len,
                                         WeaveCertificateSet& cert_set);

  std::vector<uint8_t> device_cert_;
  std::vector<uint8_t> device_intermediate_certs_;
  std::vector<uint8_t> service_config_;
};

// Initializes an implementation of the KeyExportDelegate and assigns it to the
// SecurityMgr instance. SecurityMgr should be initialized before invocation.
WEAVE_ERROR InitKeyExportDelegate();

}  // namespace Internal
}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_PLATFORM_AUTH_DELEGATE_H_
