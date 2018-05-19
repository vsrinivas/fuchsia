// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bearer.h"

#include <lib/async/default.h>

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

Bearer::PairingFeatures::PairingFeatures() {
  std::memset(this, 0, sizeof(*this));
}

Bearer::PairingFeatures::PairingFeatures(bool initiator, bool sc,
                                         PairingMethod method,
                                         uint8_t enc_key_size,
                                         KeyDistGenField local_kd,
                                         KeyDistGenField remote_kd)
    : initiator(initiator),
      secure_connections(sc),
      method(method),
      encryption_key_size(enc_key_size),
      local_key_distribution(local_kd),
      remote_key_distribution(remote_kd) {}

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
  // TODO(armansito): Check that no other procedure is pending.
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

  // TODO(armansito): Set the IdKey bit when we support Privacy.
  // TODO(armansito): Set the SignKey bit when we support Security Mode 2.
  // TODO(armansito): Set the EncKey bit for BR/EDR when we support Secure
  // Connections.
  // TODO(armansito): Set the LinkKey bit when we support Secure Connections.
  if (chan_->link_type() == hci::Connection::LinkType::kLE && !sc_supported_) {
    payload->initiator_key_dist_gen = KeyDistGen::kEncKey;
    payload->responder_key_dist_gen = KeyDistGen::kEncKey;
  }

  // Cache the pairing request. This will be used as the |preq| parameter for
  // crypto functions later (e.g. during confirm value generation in legacy
  // pairing).
  pdu->Copy(&preq_);

  // Start pairing timer.
  FXL_DCHECK(!timeout_task_.is_pending());
  timeout_task_.PostDelayed(async_get_default(), zx::sec(kPairingTimeout));

  feature_exchange_pending_ = true;
  chan_->Send(std::move(pdu));

  return true;
}

void Bearer::StopTimer() {
  if (timeout_task_.is_pending()) {
    timeout_task_.Cancel();
  }
}

void Bearer::Abort(ErrorCode ecode) {
  // TODO(armansito): Check the states of other procedures once we have them.
  if (!pairing_started()) {
    FXL_VLOG(1) << "sm: Pairing not started!";
    return;
  }

  StopTimer();

  // TODO(armansito): Clear other procedure states here.
  feature_exchange_pending_ = false;
  SendPairingFailed(ecode);

  Status status(ecode);
  FXL_LOG(ERROR) << "sm: Pairing aborted " << status.ToString();
  error_callback_(status);
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

  const auto& params = reader.payload<PairingResponseParams>();

  // Select the smaller of the initiator and responder max. encryption key size
  // values (Vol 3, Part H, 2.3.4).
  uint8_t encr_key_size =
      std::min(kMaxEncryptionKeySize, params.max_encryption_key_size);
  if (encr_key_size < kMinEncryptionKeySize) {
    FXL_VLOG(1) << "sm: Encryption key size too small! (" << encr_key_size
                << ")";
    Abort(ErrorCode::kEncryptionKeySize);
    return;
  }

  bool sc = sc_supported_ && (params.auth_req & AuthReq::kSC);
  bool mitm = mitm_required_ || (params.auth_req & AuthReq::kMITM);
  bool peer_oob = params.oob_data_flag == OOBDataFlag::kPresent;
  PairingMethod method = util::SelectPairingMethod(
      sc, oob_available_, peer_oob, mitm, io_capability_, params.io_capability);

  feature_exchange_pending_ = false;
  feature_exchange_callback_(
      PairingFeatures(true /* initiator */, sc, method, encr_key_size,
                      params.initiator_key_dist_gen,
                      params.responder_key_dist_gen),
      preq_, reader.data());
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
      case kPairingResponse:
        OnPairingResponse(reader);
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
