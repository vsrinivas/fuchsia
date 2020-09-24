// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_case_auth_delegate.h"

#include <fuchsia/weave/cpp/fidl.h>
#include <lib/fit/defer.h>

#include <Weave/Profiles/security/WeaveSig.h>
#include <sdk/lib/syslog/cpp/macros.h>

#include "src/connectivity/weave/adaptation/weave_device_platform_error.h"

namespace nl {
namespace Weave {
namespace DeviceLayer {
namespace Internal {

namespace {
using namespace ::nl::Weave::TLV;
using namespace ::nl::Weave::Profiles;
using namespace ::nl::Weave::Profiles::Security;

// TODO(fxbug.dev/51130): Allow build-time configuration of these values.
constexpr size_t kMaxCerts = 10;
constexpr size_t kMaxServiceConfigSize = 10000;
constexpr size_t kCertDecodeBufferSize = 5000;

}  // namespace

PlatformCASEAuthDelegate gPlatformCASEAuthDelegate;

// Implementation of Internal::InitCASEAuthDelegate as defined in the
// openweave-core adaptation layer.
WEAVE_ERROR InitCASEAuthDelegate() {
  new (&gPlatformCASEAuthDelegate)
      PlatformCASEAuthDelegate(sys::ComponentContext::CreateAndServeOutgoingDirectory());
  SecurityMgr.SetCASEAuthDelegate(&gPlatformCASEAuthDelegate);
  return WEAVE_NO_ERROR;
}

PlatformCASEAuthDelegate::PlatformCASEAuthDelegate() : PlatformCASEAuthDelegate(nullptr) {}

PlatformCASEAuthDelegate::PlatformCASEAuthDelegate(std::unique_ptr<sys::ComponentContext> context)
    : context_(std::move(context)) {}

WEAVE_ERROR PlatformCASEAuthDelegate::EncodeNodePayload(const BeginSessionContext& msg_ctx,
                                                        uint8_t* payload_buf,
                                                        uint16_t payload_buf_size,
                                                        uint16_t& payload_len) {
  WEAVE_ERROR error = WEAVE_NO_ERROR;
  size_t device_desc_len;

  error = ConfigurationMgr().GetDeviceDescriptorTLV(payload_buf, (size_t)payload_buf_size,
                                                    device_desc_len);
  if (error != WEAVE_NO_ERROR) {
    return error;
  }

  payload_len = device_desc_len;
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR PlatformCASEAuthDelegate::EncodeNodeCertInfo(const BeginSessionContext& msg_ctx,
                                                         TLVWriter& writer) {
  std::vector<uint8_t> device_cert;
  std::vector<uint8_t> device_ica_certs;
  size_t device_cert_size = 0;
  size_t device_ica_certs_size = 0;
  WEAVE_ERROR err = WEAVE_NO_ERROR;

  // Fetch the provided device certificate.
  err = ConfigurationMgr().GetDeviceCertificate(nullptr, 0, device_cert_size);
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  device_cert.resize(device_cert_size);
  err = ConfigurationMgr().GetDeviceCertificate(device_cert.data(), device_cert_size,
                                                device_cert_size);
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  // Fetch any intermediate CA certificates.
  err = ConfigurationMgr().GetDeviceIntermediateCACerts(nullptr, 0, device_ica_certs_size);
  if (err == WEAVE_NO_ERROR) {
    device_ica_certs.resize(device_ica_certs_size);
    err = ConfigurationMgr().GetDeviceIntermediateCACerts(
        device_ica_certs.data(), device_ica_certs_size, device_ica_certs_size);
    if (err != WEAVE_NO_ERROR) {
      return err;
    }
  } else if (err != WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND) {
    return err;
  }

  // Encode the certificate information into the provided TLVWriter.
  return Profiles::Security::CASE::EncodeCASECertInfo(
      writer, device_cert.data(), device_cert_size, device_ica_certs.data(), device_ica_certs_size);
}

WEAVE_ERROR PlatformCASEAuthDelegate::GenerateNodeSignature(const BeginSessionContext& msg_ctx,
                                                            const uint8_t* msg_hash,
                                                            uint8_t msg_hash_len, TLVWriter& writer,
                                                            uint64_t tag) {
  fuchsia::weave::Signer_SignHash_Result result;
  std::vector<uint8_t>* output;
  zx_status_t status;

  fuchsia::weave::SignerSyncPtr signer;
  if ((status = context_->svc()->Connect(signer.NewRequest())) != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to connect to signer: " << status;
    return status;
  }

  std::vector<uint8_t> hash(msg_hash, msg_hash + (msg_hash_len * sizeof(uint8_t)));
  if ((status = signer->SignHash(hash, &result)) != ZX_OK || !result.is_response()) {
    FX_LOGS(ERROR) << "Failed to sign hash: " << status;
    return status;
  }

  output = &result.response().signature;
  return ConvertECDSASignature_DERToWeave(output->data(), output->size(), writer, tag);
}

WEAVE_ERROR PlatformCASEAuthDelegate::BeginValidation(const BeginSessionContext& msg_ctx,
                                                      ValidationContext& valid_ctx,
                                                      WeaveCertificateSet& cert_set) {
  WEAVE_ERROR err = WEAVE_NO_ERROR;
  size_t service_config_len = 0;
  uint64_t now_ms;

  service_config_.clear();
  service_config_.resize(kMaxServiceConfigSize);

  err = cert_set.Init(kMaxCerts, kCertDecodeBufferSize);
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  auto release_cert_set = fit::defer([&] { cert_set.Release(); });

  // Read the service config data.
  err = ConfigurationMgr().GetServiceConfig(service_config_.data(), service_config_.size(),
                                            service_config_len);
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  // Load the list of trusted root certificates from the service config.
  err = LoadCertsFromServiceConfig(service_config_.data(), service_config_len, cert_set);
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  // Scan the list of trusted certs loaded from the service config. If the list
  // contains a general certificate with a CommonName subject, presume this is
  // the access token certificate.
  for (uint8_t cert_index = 0; cert_index < cert_set.CertCount; cert_index++) {
    WeaveCertificateData* cert = &cert_set.Certs[cert_index];
    if ((cert->CertFlags & kCertFlag_IsTrusted) != 0 && cert->CertType == kCertType_General &&
        cert->SubjectDN.AttrOID == ASN1::kOID_AttributeType_CommonName) {
      cert->CertType = kCertType_AccessToken;
    }
  }

  memset(&valid_ctx, 0, sizeof(valid_ctx));

  // Set the effective time for certificate validation. Use the current time if
  // the system's real time clock is synchronized, but otherwise use the
  // firmware build time and arrange to ignore the 'not before' date in the
  // peer's certificate.
  err = System::Layer::GetClock_RealTimeMS(now_ms);
  if (err == WEAVE_NO_ERROR) {
    // TODO(fxbug.dev/51890): The default implementation of GetClock_RealTimeMS only returns
    // not-synced if the value is before Jan 1, 2000. Use the UTC fidl instead
    // to confirm whether the clock source is from some external source.
    valid_ctx.EffectiveTime = SecondsSinceEpochToPackedCertTime((uint32_t)(now_ms / 1000));
  } else if (err == WEAVE_SYSTEM_ERROR_REAL_TIME_NOT_SYNCED) {
    // TODO(fxbug.dev/51890): Acquire the firmware build time, for now we set it to Jan 1, 2020
    // as sane default time.
    valid_ctx.EffectiveTime = SecondsSinceEpochToPackedCertTime(1577836800U);
    valid_ctx.ValidateFlags |= kValidateFlag_IgnoreNotBefore;
    FX_LOGS(WARNING) << "Real time clock not synchronized, using default time for cert validation.";
  }

  valid_ctx.RequiredKeyUsages = kKeyUsageFlag_DigitalSignature;
  valid_ctx.RequiredKeyPurposes =
      msg_ctx.IsInitiator() ? kKeyPurposeFlag_ServerAuth : kKeyPurposeFlag_ClientAuth;
  release_cert_set.cancel();
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR PlatformCASEAuthDelegate::HandleValidationResult(const BeginSessionContext& msg_ctx,
                                                             ValidationContext& valid_ctx,
                                                             WeaveCertificateSet& cert_set,
                                                             WEAVE_ERROR& valid_res) {
  // If an error was already detected in a previous stage, return it as-is.
  if (valid_res != WEAVE_NO_ERROR) {
    return WEAVE_NO_ERROR;
  }

  uint64_t cert_id = valid_ctx.SigningCert->SubjectDN.AttrValue.WeaveId;
  switch (valid_ctx.SigningCert->CertType) {
    // If the peer authenticated with a device certificate:
    case kCertType_Device:
      // Reject the peer if the certificate node ID and peer node ID do not match.
      if (cert_id != msg_ctx.PeerNodeId) {
        valid_res = WEAVE_ERROR_WRONG_CERTIFICATE_SUBJECT;
      }
      break;
    // If the peer authenticated with a service endpoint certificate:
    case kCertType_ServiceEndpoint:
      // Reject the peer if the certificate node ID and peer node ID do not match.
      if (cert_id != msg_ctx.PeerNodeId) {
        valid_res = WEAVE_ERROR_WRONG_CERTIFICATE_SUBJECT;
      }
      // Reject the peer if they are initiating the session. Service endpoint
      // certificates cannot be used to initiate sessions to other nodes, only
      // to respond.
      if (!msg_ctx.IsInitiator()) {
        valid_res = WEAVE_ERROR_CERT_USAGE_NOT_ALLOWED;
      }
      break;
    // If the peer authenticated with an access token certificate:
    case kCertType_AccessToken:
      // Reject the peer if they are the session responder. Access token
      // certificates can only be used to initiate sessions.
      if (msg_ctx.IsInitiator()) {
        valid_res = WEAVE_ERROR_CERT_USAGE_NOT_ALLOWED;
      }
      break;
    default:
      valid_res = WEAVE_ERROR_CERT_USAGE_NOT_ALLOWED;
  }
  return WEAVE_NO_ERROR;
}

void PlatformCASEAuthDelegate::EndValidation(const BeginSessionContext& msg_ctx,
                                             ValidationContext& valid_ctx,
                                             WeaveCertificateSet& cert_set) {
  service_config_.clear();
}

WEAVE_ERROR PlatformCASEAuthDelegate::LoadCertsFromServiceConfig(const uint8_t* service_config,
                                                                 uint16_t service_config_len,
                                                                 WeaveCertificateSet& cert_set) {
  WEAVE_ERROR err = WEAVE_NO_ERROR;
  TLVReader reader;
  TLVType top_level_container;

  reader.Init(service_config, service_config_len);
  reader.ImplicitProfileId = kWeaveProfile_ServiceProvisioning;

  err = reader.Next(kTLVType_Structure, ProfileTag(kWeaveProfile_ServiceProvisioning,
                                                   ServiceProvisioning::kTag_ServiceConfig));
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  err = reader.EnterContainer(top_level_container);
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  err = reader.Next(kTLVType_Array, ContextTag(ServiceProvisioning::kTag_ServiceConfig_CACerts));
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  err = cert_set.LoadCerts(reader, kDecodeFlag_IsTrusted);
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  return WEAVE_NO_ERROR;
}

}  // namespace Internal
}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl
