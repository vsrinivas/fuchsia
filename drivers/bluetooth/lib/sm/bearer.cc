// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bearer.h"

#include <lib/async/default.h>
#include <zircon/status.h>

#include "garnet/drivers/bluetooth/lib/common/slab_allocator.h"

#include "lib/fxl/strings/string_printf.h"

#include "util.h"

namespace btlib {

using common::ByteBuffer;
using common::HostError;

namespace sm {
namespace {

common::MutableByteBufferPtr NewPDU(size_t param_size) {
  auto pdu = common::NewSlabBuffer(sizeof(Header) + param_size);
  if (!pdu) {
    FXL_VLOG(1) << "att: Out of memory";
  }

  return pdu;
}

}  // namespace

Bearer::Bearer(fbl::RefPtr<l2cap::Channel> chan, hci::Connection::Role role,
               bool sc_supported, IOCapability io_capability,
               StatusCallback error_callback,
               FeatureExchangeCallback feature_exchange_callback)
    : chan_(std::move(chan)),
      role_(role),
      oob_available_(false),
      mitm_required_(false),
      sc_supported_(sc_supported),
      io_capability_(io_capability),
      error_callback_(std::move(error_callback)),
      feature_exchange_callback_(std::move(feature_exchange_callback)),
      feature_exchange_pending_(false),
      weak_ptr_factory_(this) {
  FXL_DCHECK(chan_);
  FXL_DCHECK(error_callback_);
  FXL_DCHECK(feature_exchange_callback_);
  FXL_DCHECK(async_get_default()) << "sm: Default dispatcher required!";

  if (chan_->link_type() == hci::Connection::LinkType::kLE) {
    FXL_DCHECK(chan_->id() == l2cap::kLESMPChannelId);
    mtu_ = kLEMTU;
  } else if (chan_->link_type() == hci::Connection::LinkType::kACL) {
    FXL_DCHECK(chan_->id() == l2cap::kSMPChannelId);
    mtu_ = kBREDRMTU;
  } else {
    FXL_NOTREACHED() << "sm: Unsupported link type!";
  }

  auto self = weak_ptr_factory_.GetWeakPtr();
  chan_->Activate([self](const auto& sdu) { self->OnRxBFrame(sdu); },
                  [self] { self->OnChannelClosed(); }, async_get_default());
}

bool Bearer::InitiateFeatureExchange() {
  // TODO(armansito): It should be possible to re-initiate pairing with
  // different parameters even when it's in progress.
  if (pairing_started() || feature_exchange_pending_) {
    FXL_VLOG(1) << "sm: Feature exchange already pending!";
    return false;
  }

  if (role_ == hci::Connection::Role::kSlave) {
    FXL_VLOG(1) << "sm: Slave cannot initiate a feature exchange!";
    return false;
  }

  auto pdu = NewPDU(sizeof(PairingRequestParams));
  if (!pdu) {
    return false;
  }

  // Always request bonding.
  AuthReqField auth_req = AuthReq::kBondingFlag;
  if (sc_supported_) {
    auth_req |= AuthReq::kSC;
  }
  if (mitm_required_) {
    auth_req |= AuthReq::kMITM;
  }

  // TODO(armansito): Set the "keypress", and "CT2" flags when they
  // are supported.

  PacketWriter writer(kPairingRequest, pdu.get());
  auto* payload = writer.mutable_payload<PairingRequestParams>();
  payload->io_capability = io_capability_;
  payload->oob_data_flag =
      oob_available_ ? OOBDataFlag::kPresent : OOBDataFlag::kNotPresent;
  payload->auth_req = auth_req;
  payload->max_encryption_key_size = kMaxEncryptionKeySize;

  // TODO(armansito): Set more bits here when we support more things. Make sure
  // that the correct bits are set based on |sc_supported_| and the link type
  // (we currently don't support SC and support SMP on LE links only).
  payload->initiator_key_dist_gen = KeyDistGen::kEncKey;
  payload->responder_key_dist_gen = KeyDistGen::kEncKey;

  // Cache the pairing request. This will be used as the |preq| parameter for
  // crypto functions later (e.g. during confirm value generation in legacy
  // pairing).
  pdu->Copy(&pairing_payload_buffer_);

  // Start pairing timer.
  FXL_DCHECK(!timeout_task_.is_pending());
  timeout_task_.PostDelayed(async_get_default(), zx::sec(kPairingTimeout));

  feature_exchange_pending_ = true;
  chan_->Send(std::move(pdu));

  return true;
}

bool Bearer::SendConfirmValue(const common::UInt128& confirm) {
  if (!pairing_started()) {
    FXL_VLOG(1) << "sm: Not pairing!";
    return false;
  }

  // Only allowed on the LE transport.
  if (chan_->link_type() != hci::Connection::LinkType::kLE) {
    return false;
  }

  auto pdu = NewPDU(sizeof(PairingConfirmValue));
  if (!pdu) {
    FXL_LOG(ERROR) << "sm: Out of memory!";
    Abort(ErrorCode::kUnspecifiedReason);
    return false;
  }

  PacketWriter writer(kPairingConfirm, pdu.get());
  *writer.mutable_payload<PairingConfirmValue>() = confirm;
  chan_->Send(std::move(pdu));

  return true;
}

bool Bearer::SendRandomValue(const common::UInt128& random) {
  if (!pairing_started()) {
    FXL_VLOG(1) << "sm: Not pairing!";
    return false;
  }

  // Only allowed on the LE transport.
  if (chan_->link_type() != hci::Connection::LinkType::kLE) {
    return false;
  }

  auto pdu = NewPDU(sizeof(PairingRandomValue));
  if (!pdu) {
    FXL_LOG(ERROR) << "sm: Out of memory!";
    Abort(ErrorCode::kUnspecifiedReason);
    return false;
  }

  PacketWriter writer(kPairingRandom, pdu.get());
  *writer.mutable_payload<PairingRandomValue>() = random;
  chan_->Send(std::move(pdu));

  return true;
}

void Bearer::StopTimer() {
  if (timeout_task_.is_pending()) {
    zx_status_t status = timeout_task_.Cancel();
    if (status != ZX_OK) {
      FXL_VLOG(2) << "smp: Failed to stop timer: "
                  << zx_status_get_string(status);
    }
  }
}

void Bearer::Abort(ErrorCode ecode) {
  // TODO(armansito): Check the states of other procedures once we have them.
  if (!pairing_started()) {
    FXL_VLOG(1) << "sm: Pairing not started! Nothing to abort.";
    return;
  }

  FXL_LOG(ERROR) << "sm: Abort pairing";

  StopTimer();
  SendPairingFailed(ecode);
  OnFailure(Status(ecode));
}

void Bearer::OnFailure(Status status) {
  FXL_LOG(ERROR) << "sm: Pairing failed: " << status.ToString();

  // TODO(armansito): Clear other procedure states here.
  feature_exchange_pending_ = false;
  error_callback_(status);
}

void Bearer::OnPairingTimeout() {
  // Pairing is no longer allowed on this bearer. Disconnect the link.
  FXL_LOG(ERROR) << "sm: Pairing timed out! Disconnecting link.";
  chan_->SignalLinkError();

  OnFailure(Status(HostError::kTimedOut));
}

ErrorCode Bearer::ResolveFeatures(bool local_initiator,
                                  const PairingRequestParams& preq,
                                  const PairingResponseParams& pres,
                                  PairingFeatures* out_features) {
  FXL_DCHECK(pairing_started());
  FXL_DCHECK(feature_exchange_pending_);

  // Select the smaller of the initiator and responder max. encryption key size
  // values (Vol 3, Part H, 2.3.4).
  uint8_t enc_key_size =
      std::min(preq.max_encryption_key_size, pres.max_encryption_key_size);
  if (enc_key_size < kMinEncryptionKeySize) {
    FXL_VLOG(1) << "sm: Encryption key size too small! (" << enc_key_size
                << ")";
    return ErrorCode::kEncryptionKeySize;
  }

  bool sc = (preq.auth_req & AuthReq::kSC) && (pres.auth_req & AuthReq::kSC);
  bool mitm =
      (preq.auth_req & AuthReq::kMITM) || (pres.auth_req & AuthReq::kMITM);
  bool init_oob = preq.oob_data_flag == OOBDataFlag::kPresent;
  bool rsp_oob = pres.oob_data_flag == OOBDataFlag::kPresent;

  PairingMethod method = util::SelectPairingMethod(
      sc, init_oob, rsp_oob, mitm, preq.io_capability, pres.io_capability);

  // If MITM protection is required but the pairing method cannot provide MITM,
  // then reject the pairing.
  if (mitm && method == PairingMethod::kJustWorks) {
    return ErrorCode::kAuthenticationRequirements;
  }

  // The "Pairing Response" command (i.e. |pres|) determines the keys that shall
  // be distributed. The keys that will be distributed by us and the peer
  // depends on whichever one initiated the feature exchange by sending a
  // "Pairing Request" command.
  KeyDistGenField local_keys, remote_keys;
  if (local_initiator) {
    local_keys = pres.initiator_key_dist_gen;
    remote_keys = pres.responder_key_dist_gen;
  } else {
    local_keys = pres.responder_key_dist_gen;
    remote_keys = pres.initiator_key_dist_gen;
  }

  *out_features = PairingFeatures(local_initiator, sc, method, enc_key_size,
                                  local_keys, remote_keys);

  return ErrorCode::kNoError;
}

void Bearer::OnPairingFailed(const PacketReader& reader) {
  if (!pairing_started()) {
    FXL_VLOG(1) << "sm: Received \"Pairing Failed\" while not pairing!";
    return;
  }

  Status status(HostError::kFailed);

  if (reader.payload_size() == sizeof(ErrorCode)) {
    status = Status(reader.payload<ErrorCode>());
  } else {
    FXL_VLOG(1) << "sm: Malformed \"Pairing Failed\" payload";
  }

  StopTimer();
  OnFailure(status);
}

void Bearer::OnPairingRequest(const PacketReader& reader) {
  if (reader.payload_size() != sizeof(PairingRequestParams)) {
    FXL_VLOG(1) << "sm: Malformed \"Pairing Request\" payload";
    SendPairingFailed(ErrorCode::kInvalidParameters);
    return;
  }

  // Reject the command if we are the master.
  if (role_ == hci::Connection::Role::kMaster) {
    SendPairingFailed(ErrorCode::kCommandNotSupported);
    return;
  }

  // We shouldn't be in this state when pairing is initiated by the remote.
  FXL_DCHECK(!feature_exchange_pending_);
  feature_exchange_pending_ = true;

  const auto& params = reader.payload<PairingRequestParams>();
  auto pdu = NewPDU(sizeof(PairingResponseParams));
  if (!pdu) {
    FXL_LOG(ERROR) << "sm: Out of memory!";
    SendPairingFailed(ErrorCode::kUnspecifiedReason);
    return;
  }

  // "Upon reception of the Pairing Request command, the Security Manager Timer
  // shall be reset and started" (Vol 3, Part H, 3.4).
  if (pairing_started()) {
    StopTimer();
  }

  // Start pairing timer.
  FXL_DCHECK(!timeout_task_.is_pending());
  timeout_task_.PostDelayed(async_get_default(), zx::sec(kPairingTimeout));

  // Always request bonding.
  AuthReqField auth_req = AuthReq::kBondingFlag;
  if (sc_supported_) {
    auth_req |= AuthReq::kSC;
  }
  if (mitm_required_) {
    auth_req |= AuthReq::kMITM;
  }

  // TODO(armansito): Set the "keypress", and "CT2" flags when they
  // are supported.

  PacketWriter writer(kPairingResponse, pdu.get());
  auto* payload = writer.mutable_payload<PairingResponseParams>();
  payload->io_capability = io_capability_;
  payload->oob_data_flag =
      oob_available_ ? OOBDataFlag::kPresent : OOBDataFlag::kNotPresent;
  payload->auth_req = auth_req;
  payload->max_encryption_key_size = kMaxEncryptionKeySize;

  // TODO(armansito): Set more bits here when we support more things. Make sure
  // that the correct bits are set based on |sc_supported_| and the link type
  // (we currently don't support SC and support SMP on LE links only).
  KeyDistGenField local_keys = KeyDistGen::kEncKey;
  KeyDistGenField remote_keys = KeyDistGen::kEncKey;

  // The keys that will be exchanged is the intersection of what the initiator
  // requests and we support.
  payload->initiator_key_dist_gen = remote_keys & params.initiator_key_dist_gen;
  payload->responder_key_dist_gen = local_keys & params.responder_key_dist_gen;

  PairingFeatures features;
  ErrorCode ecode =
      ResolveFeatures(false /* local_initiator */, params, *payload, &features);
  feature_exchange_pending_ = false;
  if (ecode != ErrorCode::kNoError) {
    Abort(ecode);
    return;
  }

  // Copy the pairing response so that it's available after moving |pdu|. (We
  // want to make sure that we send the pairing response before calling
  // |feature_exchange_callback_| which may trigger other SMP transactions.
  //
  // This will be used as the |pres| parameter for crypto functions later (e.g.
  // during confirm value generation in legacy pairing).
  pdu->Copy(&pairing_payload_buffer_);
  chan_->Send(std::move(pdu));

  feature_exchange_callback_(features, reader.data(), pairing_payload_buffer_);
}

void Bearer::OnPairingResponse(const PacketReader& reader) {
  if (reader.payload_size() != sizeof(PairingResponseParams)) {
    FXL_VLOG(1) << "sm: Malformed \"Pairing Response\" payload";
    Abort(ErrorCode::kInvalidParameters);
    return;
  }

  // Reject the command if we are the slave.
  if (role_ == hci::Connection::Role::kSlave) {
    Abort(ErrorCode::kCommandNotSupported);
    return;
  }

  if (!feature_exchange_pending_) {
    FXL_VLOG(1) << "sm: Ignoring unexpected \"Pairing Response\" packet";
    return;
  }

  PairingFeatures features;
  ErrorCode ecode = ResolveFeatures(
      true /* local_initiator */,
      pairing_payload_buffer_.view(sizeof(Code)).As<PairingRequestParams>(),
      reader.payload<PairingResponseParams>(), &features);
  feature_exchange_pending_ = false;

  if (ecode != ErrorCode::kNoError) {
    Abort(ecode);
    return;
  }

  feature_exchange_callback_(features, pairing_payload_buffer_, reader.data());
}

void Bearer::OnPairingConfirm(const PacketReader& reader) {
  // Ignore the command if not pairing.
  if (!pairing_started()) {
    FXL_VLOG(1) << "sm: Dropped unexpected \"confirm value\"";
    return;
  }

  // Only allowed on the LE transport.
  if (chan_->link_type() != hci::Connection::LinkType::kLE) {
    FXL_VLOG(1) << "sm: \"Confirm value\" over BR/EDR not supported!";
    Abort(ErrorCode::kCommandNotSupported);
    return;
  }

  if (reader.payload_size() != sizeof(PairingConfirmValue)) {
    FXL_VLOG(1) << "sm: Malformed \"Pairing Confirm\" payload";
    Abort(ErrorCode::kInvalidParameters);
    return;
  }

  FXL_DCHECK(confirm_value_callback_);
  confirm_value_callback_(reader.payload<PairingConfirmValue>());
}

void Bearer::OnPairingRandom(const PacketReader& reader) {
  // Ignore the command if not pairing.
  if (!pairing_started()) {
    FXL_VLOG(1) << "sm: Dropped unexpected \"random value\"";
    return;
  }

  // Only allowed on the LE transport.
  if (chan_->link_type() != hci::Connection::LinkType::kLE) {
    FXL_VLOG(1) << "sm: \"Random value\" over BR/EDR not supported!";
    Abort(ErrorCode::kCommandNotSupported);
    return;
  }

  if (reader.payload_size() != sizeof(PairingRandomValue)) {
    FXL_VLOG(1) << "sm: Malformed \"Pairing Randomm\" payload";
    Abort(ErrorCode::kInvalidParameters);
    return;
  }

  FXL_DCHECK(random_value_callback_);
  random_value_callback_(reader.payload<PairingRandomValue>());
}

void Bearer::SendPairingFailed(ErrorCode ecode) {
  auto pdu = NewPDU(sizeof(ErrorCode));
  PacketWriter writer(kPairingFailed, pdu.get());
  *writer.mutable_payload<PairingFailedParams>() = ecode;
  chan_->Send(std::move(pdu));
}

void Bearer::OnChannelClosed() {
  FXL_VLOG(1) << "sm: Channel closed";

  if (pairing_started()) {
    OnFailure(Status(HostError::kLinkDisconnected));
  }
}

void Bearer::OnRxBFrame(const l2cap::SDU& sdu) {
  uint8_t length = sdu.length();
  if (length < sizeof(Code)) {
    FXL_VLOG(1) << "sm: PDU too short!";
    Abort(ErrorCode::kInvalidParameters);
    return;
  }

  if (length > mtu_) {
    FXL_VLOG(1) << "sm: PDU exceeds MTU!";
    Abort(ErrorCode::kInvalidParameters);
    return;
  }

  // The following will read the entire PDU in a single call.
  l2cap::SDU::Reader l2cap_reader(&sdu);
  l2cap_reader.ReadNext(length, [this, length](const ByteBuffer& sm_pdu) {
    FXL_DCHECK(sm_pdu.size() == length);
    PacketReader reader(&sm_pdu);

    switch (reader.code()) {
      case kPairingFailed:
        OnPairingFailed(reader);
        break;
      case kPairingRequest:
        OnPairingRequest(reader);
        break;
      case kPairingResponse:
        OnPairingResponse(reader);
        break;
      case kPairingConfirm:
        OnPairingConfirm(reader);
        break;
      case kPairingRandom:
        OnPairingRandom(reader);
        break;
      default:
        FXL_VLOG(2) << fxl::StringPrintf("sm: Unsupported command: 0x%02x",
                                         reader.code());

        auto ecode = ErrorCode::kCommandNotSupported;
        if (pairing_started()) {
          Abort(ecode);
        } else {
          SendPairingFailed(ecode);
        }
        break;
    }
  });
}

}  // namespace sm
}  // namespace btlib
