// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_auth_delegate.h"

#include <fuchsia/weave/cpp/fidl.h>
#include <lib/fit/defer.h>

#include <Weave/Profiles/security/WeaveSig.h>
#include <sdk/lib/syslog/cpp/macros.h>

#include "src/connectivity/weave/adaptation/weave_device_platform_error.h"
#include "utils.h"

namespace nl::Weave::DeviceLayer::Internal {
namespace {
namespace Profiles = ::nl::Weave::Profiles;
namespace Security = ::nl::Weave::Profiles::Security;
namespace ServiceProvisioning = ::nl::Weave::Profiles::ServiceProvisioning;

using Security::WeaveCertificateData;

// TODO(fxbug.dev/51130): Allow build-time configuration of these values.
constexpr size_t kMaxCerts = 10;
constexpr size_t kMaxServiceConfigSize = 10000;
constexpr size_t kCertDecodeBufferSize = 5000;

}  // namespace

static PlatformAuthDelegate gPlatformCASEAuthDelegate;
static PlatformAuthDelegate gPlatformKeyExportDelegate;

// Implementation of Internal::InitCASEAuthDelegate as defined in the
// openweave-core adaptation layer.
WEAVE_ERROR InitCASEAuthDelegate() {
  new (&gPlatformCASEAuthDelegate) PlatformAuthDelegate();
  SecurityMgr.SetCASEAuthDelegate(&gPlatformCASEAuthDelegate);
  return WEAVE_NO_ERROR;
}

// Implementation of Internal::InitKeyExportDelegate.
WEAVE_ERROR InitKeyExportDelegate() {
  new (&gPlatformKeyExportDelegate) PlatformAuthDelegate();
  SecurityMgr.SetKeyExportDelegate(&gPlatformKeyExportDelegate);
  return WEAVE_NO_ERROR;
}

/// ==========================================================
/// nl::Weave::Profiles::Security::CASE::WeaveCASEAuthDelegate
/// ==========================================================

WEAVE_ERROR PlatformAuthDelegate::EncodeNodePayload(const BeginSessionContext& msg_ctx,
                                                    uint8_t* payload_buf, uint16_t payload_buf_size,
                                                    uint16_t& payload_len) {
  WEAVE_ERROR error = WEAVE_NO_ERROR;
  size_t device_desc_len;

  error = ConfigurationMgr().GetDeviceDescriptorTLV(
      payload_buf, static_cast<size_t>(payload_buf_size), device_desc_len);
  if (error != WEAVE_NO_ERROR) {
    return error;
  }

  payload_len = device_desc_len;
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR PlatformAuthDelegate::EncodeNodeCertInfo(const BeginSessionContext& msg_ctx,
                                                     TLVWriter& writer) {
  WEAVE_ERROR err = WEAVE_NO_ERROR;

  err = GetNodeCertificates(device_cert_, device_intermediate_certs_);
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  // Encode the certificate information into the provided TLVWriter.
  return Profiles::Security::CASE::EncodeCASECertInfo(
      writer, device_cert_.data(), device_cert_.size(), device_intermediate_certs_.data(),
      device_intermediate_certs_.size());
}

WEAVE_ERROR PlatformAuthDelegate::GenerateNodeSignature(const BeginSessionContext& msg_ctx,
                                                        const uint8_t* msg_hash,
                                                        uint8_t msg_hash_len, TLVWriter& writer,
                                                        uint64_t tag) {
  return GenerateNodeSignature(msg_hash, msg_hash_len, writer, tag);
}

WEAVE_ERROR PlatformAuthDelegate::BeginValidation(const BeginSessionContext& msg_ctx,
                                                  ValidationContext& valid_ctx,
                                                  WeaveCertificateSet& cert_set) {
  return BeginCertValidation(valid_ctx, cert_set, msg_ctx.IsInitiator());
}

WEAVE_ERROR PlatformAuthDelegate::HandleValidationResult(const BeginSessionContext& msg_ctx,
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

void PlatformAuthDelegate::EndValidation(const BeginSessionContext& msg_ctx,
                                         ValidationContext& valid_ctx,
                                         WeaveCertificateSet& cert_set) {
  cert_set.Release();
  service_config_.clear();
}

///=================================================================
/// nl::Weave::Profiles::Security::KeyExport::WeaveKeyExportDelegate
///=================================================================

WEAVE_ERROR PlatformAuthDelegate::GetNodeCertSet(WeaveKeyExport* key_export,
                                                 WeaveCertificateSet& cert_set) {
  WEAVE_ERROR err = WEAVE_NO_ERROR;
  WeaveCertificateData* node_cert = nullptr;

  if (key_export->IsInitiator()) {
    return WEAVE_ERROR_INVALID_ARGUMENT;
  }

  // Acquire the node certificates.
  err = GetNodeCertificates(device_cert_, device_intermediate_certs_);
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  // Initialize the certificate set to populate it.
  err = cert_set.Init(kMaxCerts, kCertDecodeBufferSize);
  if (err != WEAVE_NO_ERROR) {
    FX_LOGS(ERROR) << "Failed to initialize certificate set: " << ErrorStr(err);
    return err;
  }

  // Auto-release the certificate set on failures.
  auto release_cert_set = fit::defer([&] { cert_set.Release(); });

  // Load node certificate into the certificate set.
  err = cert_set.LoadCert(device_cert_.data(), device_cert_.size(), 0, node_cert);
  if (err != WEAVE_NO_ERROR) {
    FX_LOGS(ERROR) << "Failed to load device certificate data: " << ErrorStr(err);
    return err;
  }

  // If device intermediate certs were available, load into certificate set.
  if (!device_intermediate_certs_.empty()) {
    err =
        cert_set.LoadCerts(device_intermediate_certs_.data(), device_intermediate_certs_.size(), 0);
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "Failed to load intermediate certificate data: " << ErrorStr(err);
      return err;
    }
  }

  release_cert_set.cancel();
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR PlatformAuthDelegate::ReleaseNodeCertSet(WeaveKeyExport* key_export,
                                                     WeaveCertificateSet& cert_set) {
  cert_set.Release();
  device_cert_.clear();
  device_intermediate_certs_.clear();

  return WEAVE_NO_ERROR;
}

WEAVE_ERROR PlatformAuthDelegate::GenerateNodeSignature(WeaveKeyExport* key_export,
                                                        const uint8_t* msg_hash,
                                                        uint8_t msg_hash_len, TLVWriter& writer) {
  return GenerateNodeSignature(
      msg_hash, msg_hash_len, writer,
      TLV::ContextTag(Profiles::Security::kTag_WeaveSignature_ECDSASignatureData));
}

WEAVE_ERROR PlatformAuthDelegate::BeginCertValidation(WeaveKeyExport* key_export,
                                                      ValidationContext& valid_ctx,
                                                      WeaveCertificateSet& cert_set) {
  return BeginCertValidation(valid_ctx, cert_set, key_export->IsInitiator());
}

WEAVE_ERROR PlatformAuthDelegate::HandleCertValidationResult(WeaveKeyExport* key_export,
                                                             ValidationContext& valid_ctx,
                                                             WeaveCertificateSet& cert_set,
                                                             uint32_t requested_key_id) {
  WeaveCertificateData* peer_cert;
  WEAVE_ERROR err = WEAVE_NO_ERROR;

  if (key_export->IsInitiator()) {
    FX_LOGS(ERROR) << "Invalid initiator state for key export.";
    return WEAVE_ERROR_INVALID_ARGUMENT;
  }

  // Only permit key export for a trusted, self-signed certificate.
  peer_cert = valid_ctx.SigningCert;
  if ((requested_key_id == WeaveKeyId::kClientRootKey) &&
      (peer_cert->CertFlags & Profiles::Security::kCertFlag_IsTrusted) &&
      peer_cert->IssuerDN.IsEqual(peer_cert->SubjectDN) &&
      (peer_cert->SubjectDN.AttrOID == ASN1::kOID_AttributeType_CommonName)) {
    err = WEAVE_NO_ERROR;
  } else {
    err = WEAVE_ERROR_UNAUTHORIZED_KEY_EXPORT_RESPONSE;
  }

  return err;
}

WEAVE_ERROR PlatformAuthDelegate::EndCertValidation(WeaveKeyExport* key_export,
                                                    ValidationContext& valid_ctx,
                                                    WeaveCertificateSet& cert_set) {
  if (key_export->IsInitiator()) {
    FX_LOGS(ERROR) << "Invalid initiator state for key export.";
    return WEAVE_ERROR_INVALID_ARGUMENT;
  }

  cert_set.Release();
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR PlatformAuthDelegate::ValidateUnsignedKeyExportMessage(WeaveKeyExport* key_export,
                                                                   uint32_t requested_key_id) {
  return WEAVE_ERROR_UNAUTHORIZED_KEY_EXPORT_REQUEST;
}

///====================================
/// Common Authentication Functionality
///====================================

WEAVE_ERROR PlatformAuthDelegate::GetNodeCertificates(
    std::vector<uint8_t>& device_cert, std::vector<uint8_t>& device_intermediate_certs) {
  size_t device_cert_size = 0;
  size_t device_intermediate_certs_size = 0;
  WEAVE_ERROR err = WEAVE_NO_ERROR;

  // Fetch the provided device certificate.
  err = ConfigurationMgr().GetDeviceCertificate(nullptr, 0, device_cert_size);
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  device_cert.resize(device_cert_size);
  err = ConfigurationMgr().GetDeviceCertificate(device_cert.data(), device_cert.size(),
                                                device_cert_size);
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  // Fetch any intermediate CA certificates.
  err = ConfigurationMgr().GetDeviceIntermediateCACerts(nullptr, 0, device_intermediate_certs_size);
  if (err == WEAVE_NO_ERROR) {
    device_intermediate_certs.resize(device_intermediate_certs_size);
    err = ConfigurationMgr().GetDeviceIntermediateCACerts(device_intermediate_certs.data(),
                                                          device_intermediate_certs.size(),
                                                          device_intermediate_certs_size);
    if (err != WEAVE_NO_ERROR) {
      return err;
    }
  } else if (err != WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND) {
    return err;
  } else {
    device_intermediate_certs.resize(0);
  }

  return WEAVE_NO_ERROR;
}

WEAVE_ERROR PlatformAuthDelegate::GenerateNodeSignature(const uint8_t* msg_hash,
                                                        uint8_t msg_hash_len, TLVWriter& writer,
                                                        uint64_t tag) {
  fuchsia::weave::Signer_SignHash_Result result;
  std::vector<uint8_t>* output;
  zx_status_t status;
  std::vector<uint8_t> signing_key;

  // Using a private key directly is intended only for test purposes.
  status = ConfigurationMgrImpl().GetPrivateKeyForSigning(&signing_key);
  if (status == ZX_OK) {
    status = Security::GenerateAndEncodeWeaveECDSASignature(writer, tag, msg_hash, msg_hash_len,
                                                            signing_key.data(), signing_key.size());
    secure_memset(signing_key.data(), 0, signing_key.size());
    signing_key.clear();
    return status;
  }

  // If private key is not present, continue with the signer, else return the error
  // encountered in reading the private key.
  if (status != ZX_ERR_NOT_FOUND) {
    FX_LOGS(ERROR) << "Failed reading private key:  " << zx_status_get_string(status);
    return status;
  }

  fuchsia::weave::SignerSyncPtr signer;
  if ((status = PlatformMgrImpl().GetComponentContextForProcess()->svc()->Connect(
           signer.NewRequest())) != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to connect to signer: " << status;
    return status;
  }

  std::vector<uint8_t> hash(msg_hash, msg_hash + (msg_hash_len * sizeof(uint8_t)));
  if ((status = signer->SignHash(hash, &result)) != ZX_OK || !result.is_response()) {
    FX_LOGS(ERROR) << "Failed to sign hash: " << status;
    return status;
  }

  output = &result.response().signature;
  return Security::ConvertECDSASignature_DERToWeave(output->data(), output->size(), writer, tag);
}

WEAVE_ERROR PlatformAuthDelegate::BeginCertValidation(ValidationContext& valid_ctx,
                                                      WeaveCertificateSet& cert_set,
                                                      bool is_initiator) {
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
    if ((cert->CertFlags & Security::kCertFlag_IsTrusted) != 0 &&
        cert->CertType == kCertType_General &&
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
    valid_ctx.EffectiveTime =
        Security::SecondsSinceEpochToPackedCertTime(static_cast<uint32_t>(now_ms / 1000));
  } else if (err == WEAVE_SYSTEM_ERROR_REAL_TIME_NOT_SYNCED) {
    // TODO(fxbug.dev/51890): Acquire the firmware build time, for now we set it to Jan 1, 2020
    // as reasonable default time.
    valid_ctx.EffectiveTime = Security::SecondsSinceEpochToPackedCertTime(1577836800U);
    valid_ctx.ValidateFlags |= Security::kValidateFlag_IgnoreNotBefore;
    FX_LOGS(WARNING) << "Real time clock not synchronized, using default time for cert validation.";
  }

  valid_ctx.RequiredKeyUsages = Security::kKeyUsageFlag_DigitalSignature;
  valid_ctx.RequiredKeyPurposes =
      is_initiator ? Security::kKeyPurposeFlag_ServerAuth : Security::kKeyPurposeFlag_ClientAuth;
  release_cert_set.cancel();
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR PlatformAuthDelegate::LoadCertsFromServiceConfig(const uint8_t* service_config,
                                                             uint16_t service_config_len,
                                                             WeaveCertificateSet& cert_set) {
  WEAVE_ERROR err = WEAVE_NO_ERROR;
  TLV::TLVReader reader;
  TLV::TLVType top_level_container;

  reader.Init(service_config, service_config_len);
  reader.ImplicitProfileId = Profiles::kWeaveProfile_ServiceProvisioning;

  err = reader.Next(TLV::kTLVType_Structure,
                    TLV::ProfileTag(Profiles::kWeaveProfile_ServiceProvisioning,
                                    ServiceProvisioning::kTag_ServiceConfig));
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  err = reader.EnterContainer(top_level_container);
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  err = reader.Next(TLV::kTLVType_Array,
                    TLV::ContextTag(ServiceProvisioning::kTag_ServiceConfig_CACerts));
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  err = cert_set.LoadCerts(reader, Security::kDecodeFlag_IsTrusted);
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  return WEAVE_NO_ERROR;
}

}  // namespace nl::Weave::DeviceLayer::Internal
