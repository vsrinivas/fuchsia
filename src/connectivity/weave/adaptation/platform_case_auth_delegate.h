// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_PLATFORM_CASE_AUTH_DELEGATE_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_PLATFORM_CASE_AUTH_DELEGATE_H_

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/Core/WeaveTLV.h>
#include <Weave/Profiles/WeaveProfiles.h>
#include <Weave/Profiles/security/WeaveSecurity.h>
#include <Weave/Profiles/security/WeaveCert.h>
#include <Weave/Profiles/security/WeaveCASE.h>
#include <Weave/Profiles/service-provisioning/ServiceProvisioning.h>
#include <Weave/Support/NestCerts.h>
#include <Weave/Support/ASN1.h>
#include <Weave/Support/TimeUtils.h>
// clang-format on

namespace nl {
namespace Weave {
namespace DeviceLayer {
namespace Internal {

using nl::Weave::Profiles::Security::ValidationContext;
using nl::Weave::Profiles::Security::WeaveCertificateSet;
using nl::Weave::Profiles::Security::CASE::BeginSessionContext;

class PlatformCASEAuthDelegate final : public WeaveCASEAuthDelegate {
 public:
  PlatformCASEAuthDelegate() = default;
  ~PlatformCASEAuthDelegate() = default;

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

 private:
  static WEAVE_ERROR LoadCertsFromServiceConfig(const uint8_t* service_config,
                                                uint16_t service_config_len,
                                                WeaveCertificateSet& cert_set);

  std::vector<uint8_t> service_config_;
};

}  // namespace Internal
}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_PLATFORM_CASE_AUTH_DELEGATE_H_
