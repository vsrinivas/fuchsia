// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pairing_state.h"

#include "lib/fxl/random/rand.h"

#include "util.h"

namespace btlib {

using common::ByteBuffer;
using common::DeviceAddress;
using common::HostError;
using common::UInt128;

namespace sm {

PairingState::LegacyState::LegacyState()
    : stk_encrypted(false),
      obtained_remote_keys(0u),
      has_tk(false),
      has_peer_confirm(false),
      has_peer_rand(false),
      sent_local_confirm(false),
      sent_local_rand(false) {}

bool PairingState::LegacyState::InPhase1() const {
  return !features && !stk_encrypted;
}

bool PairingState::LegacyState::InPhase2() const {
  return features && !stk_encrypted;
}

bool PairingState::LegacyState::InPhase3() const {
  return features && stk_encrypted && !RequestedKeysObtained();
}

bool PairingState::LegacyState::IsComplete() const {
  return features && stk_encrypted && RequestedKeysObtained();
}

bool PairingState::LegacyState::RequestedKeysObtained() const {
  // Return true if we expect no keys from the remote.
  return !features->remote_key_distribution ||
         (features->remote_key_distribution == obtained_remote_keys);
}

PairingState::PendingRequest::PendingRequest(SecurityLevel level,
                                             PairingCallback callback)
    : level(level), callback(std::move(callback)) {}

PairingState::PairingState(IOCapability io_capability) : ioc_(io_capability) {}

void PairingState::RegisterLE(fbl::RefPtr<l2cap::Channel> smp,
                              hci::Connection::Role role,
                              const DeviceAddress& local_addr,
                              const DeviceAddress& peer_addr) {
  FXL_DCHECK(!legacy_state_);
  FXL_DCHECK(!le_smp_);
  FXL_DCHECK(local_addr.type() != DeviceAddress::Type::kBREDR);
  FXL_DCHECK(local_addr.type() != DeviceAddress::Type::kLEAnonymous);
  FXL_DCHECK(peer_addr.type() != DeviceAddress::Type::kBREDR);
  FXL_DCHECK(peer_addr.type() != DeviceAddress::Type::kLEAnonymous);

  le_sec_ = SecurityProperties();
  le_local_addr_ = local_addr;
  le_peer_addr_ = peer_addr;

  // TODO(armansito): Enable SC when we support it.
  le_smp_ = std::make_unique<Bearer>(
      std::move(smp), role, false /* sc */, ioc_,
      fit::bind_member(this, &PairingState::OnLEPairingFailed),
      fit::bind_member(this, &PairingState::OnLEPairingFeatures));
  le_smp_->set_confirm_value_callback(
      fit::bind_member(this, &PairingState::OnLEPairingConfirm));
  le_smp_->set_random_value_callback(
      fit::bind_member(this, &PairingState::OnLEPairingRandom));
}

void PairingState::UpdateSecurity(SecurityLevel level,
                                  PairingCallback callback) {
  // TODO(armansito): Once we support SMP over BR/EDR and Secure Connections it
  // should be possible to initiate pairing/security updates over both
  // transports. We only support pairing over LE for now.
  if (!le_smp_) {
    FXL_VLOG(2) << "sm: LE SMP bearer required for pairing!";
    callback(Status(HostError::kFailed), SecurityProperties());
    return;
  }

  // If pairing is in progress then we queue the request.
  if (legacy_state_) {
    FXL_VLOG(2) << "sm: LE legacy pairing in progress; request queued";
    FXL_DCHECK(le_smp_->pairing_started());
    request_queue_.emplace(level, std::move(callback));
    return;
  }

  if (level <= le_sec_.level()) {
    callback(Status(), le_sec_);
    return;
  }

  // TODO(armansito): Support initiating a security upgrade as slave (Bearer
  // needs to support the SMP Security Request).
  if (le_smp_->role() == hci::Connection::Role::kSlave) {
    callback(Status(HostError::kNotSupported), SecurityProperties());
    return;
  }

  if (level == SecurityLevel::kAuthenticated) {
    le_smp_->set_mitm_required(true);
  }

  request_queue_.emplace(level, std::move(callback));

  // Start Phase 1.
  legacy_state_ = std::make_unique<LegacyState>();
  le_smp_->InitiateFeatureExchange();
}

void PairingState::AbortLegacyPairing(ErrorCode error_code) {
  FXL_DCHECK(le_smp_);
  FXL_DCHECK(legacy_state_);
  FXL_DCHECK(le_smp_->pairing_started());

  legacy_state_ = nullptr;
  le_smp_->Abort(error_code);

  // "Abort" should trigger OnLEPairingFailed.
}

void PairingState::BeginLegacyPairingPhase2(const ByteBuffer& preq,
                                            const ByteBuffer& pres) {
  FXL_DCHECK(legacy_state_);
  FXL_DCHECK(legacy_state_->InPhase2());
  FXL_DCHECK(!legacy_state_->features->secure_connections);
  FXL_DCHECK(!legacy_state_->has_tk);
  FXL_DCHECK(!legacy_state_->has_peer_confirm);
  FXL_DCHECK(!legacy_state_->has_peer_rand);
  FXL_DCHECK(!legacy_state_->sent_local_confirm);
  FXL_DCHECK(!legacy_state_->sent_local_rand);

  // TODO(armansito): For now we only support Just Works and without involving
  // the user. Fix this so that:
  //   1. At a minimum a user confirmation is involved for Just Works.
  //   2. Other pairing methods are also supported.
  if (legacy_state_->features->method != PairingMethod::kJustWorks) {
    FXL_LOG(WARNING) << "sm: Only \"Just Works\" pairing is supported for now!";
    AbortLegacyPairing(ErrorCode::kUnspecifiedReason);
    return;
  }

  FXL_DCHECK(preq.size() == legacy_state_->preq.size());
  FXL_DCHECK(pres.size() == legacy_state_->pres.size());

  preq.Copy(&legacy_state_->preq);
  pres.Copy(&legacy_state_->pres);

  // TODO(armansito): Obtain the TK asynchronously, involving the
  // PairingDelegate.
  legacy_state_->tk.fill(0);
  legacy_state_->has_tk = true;

  // We have the TK, so we can generate the confirm value.
  DeviceAddress *ia, *ra;
  LEPairingAddresses(&ia, &ra);
  fxl::RandBytes(legacy_state_->local_rand.data(),
                 legacy_state_->local_rand.size());
  util::C1(legacy_state_->tk, legacy_state_->local_rand, preq, pres, *ia, *ra,
           &legacy_state_->local_confirm);

  // If we are the initiator then we just generated the "Mconfirm" value. We
  // start the exchange by sending this value to the peer. Otherwise this is the
  // "Sconfirm" value and we'll send it when the peer sends us its Mconfirm.
  if (legacy_state_->features->initiator) {
    LegacySendConfirmValue();
  }
}

void PairingState::LegacySendConfirmValue() {
  FXL_DCHECK(le_smp_);
  FXL_DCHECK(legacy_state_);
  FXL_DCHECK(legacy_state_->InPhase2());
  FXL_DCHECK(!legacy_state_->sent_local_confirm);

  legacy_state_->sent_local_confirm = true;
  le_smp_->SendConfirmValue(legacy_state_->local_confirm);
}

void PairingState::LegacySendRandomValue() {
  FXL_DCHECK(le_smp_);
  FXL_DCHECK(legacy_state_);
  FXL_DCHECK(legacy_state_->InPhase2());
  FXL_DCHECK(!legacy_state_->sent_local_rand);

  legacy_state_->sent_local_rand = true;
  le_smp_->SendRandomValue(legacy_state_->local_rand);
}

void PairingState::OnLEPairingFeatures(const PairingFeatures& features,
                                       const ByteBuffer& preq,
                                       const ByteBuffer& pres) {
  FXL_VLOG(2) << "sm: Obtained LE Pairing features";

  if (!features.initiator) {
    if (legacy_state_) {
      FXL_DCHECK(legacy_state_->features);

      // Reject if the peer sent a new pairing request while pairing is already
      // in progress.
      AbortLegacyPairing(ErrorCode::kUnspecifiedReason);
      return;
    }

    legacy_state_ = std::make_unique<LegacyState>();
  }

  FXL_DCHECK(legacy_state_);
  legacy_state_->features = features;
  BeginLegacyPairingPhase2(preq, pres);
}

void PairingState::OnLEPairingFailed(Status status) {
  FXL_LOG(ERROR) << "sm: LE Pairing failed: " << status.ToString();

  // TODO(armansito): implement "waiting interval" to prevent repeated attempts
  // as described in Vol 3, Part H, 2.3.6.

  auto requests = std::move(request_queue_);
  while (!requests.empty()) {
    requests.front().callback(status, le_sec_);
    requests.pop();
  }
}

void PairingState::OnLEPairingConfirm(const UInt128& confirm) {
  // TODO(armansito): Have separate subroutines to handle this event for legacy
  // and secure connections.
  if (!legacy_state_) {
    FXL_VLOG(1) << "sm: Ignoring confirm value received while not pairing";
    return;
  }

  if (!legacy_state_->InPhase2()) {
    FXL_LOG(ERROR)
        << "sm: Abort pairing due to confirm value received outside phase 2";
    AbortLegacyPairing(ErrorCode::kUnspecifiedReason);
    return;
  }

  // TODO(armansito): For now Phase 2 implies that we have a temporary key.
  // Remove this assertion when TK is obtained asynchronously. In that scenario,
  // if the TK hasn't yet been obtained and we are the initiator, then abort
  // pairing.
  FXL_DCHECK(legacy_state_->has_tk);

  // Abort pairing if we received a second confirm value from the peer. The
  // specification defines a certain order for the phase 2 commands.
  if (legacy_state_->has_peer_confirm) {
    FXL_LOG(ERROR) << "sm: Already received confirm value! Aborting";
    AbortLegacyPairing(ErrorCode::kUnspecifiedReason);
    return;
  }

  // The confirm value shouldn't be sent after the random value. (See Vol 3,
  // Part H, 2.3.5.5 and Appendix C.2.1.1 for the specific order of events.
  if (legacy_state_->has_peer_rand) {
    FXL_LOG(ERROR)
        << "sm: \"Pairing Confirm\" expected before \"Pairing Random\"";
    AbortLegacyPairing(ErrorCode::kUnspecifiedReason);
    return;
  }

  legacy_state_->peer_confirm = confirm;
  legacy_state_->has_peer_confirm = true;

  if (legacy_state_->features->initiator) {
    // We are the master and have previously sent Mconfirm and just received
    // Sconfirm. We now send Mrand for the slave to compare.
    FXL_DCHECK(le_smp_->role() == hci::Connection::Role::kMaster);
    LegacySendRandomValue();
  } else {
    // We are the slave and have just received Mconfirm. We now send Sconfirm to
    // the master.
    FXL_DCHECK(le_smp_->role() == hci::Connection::Role::kSlave);
    LegacySendConfirmValue();
  }
}

void PairingState::OnLEPairingRandom(const UInt128& random) {
  // TODO(armansito): Have separate subroutines to handle this event for legacy
  // and secure connections.
  if (!legacy_state_) {
    FXL_VLOG(1) << "sm: Ignoring confirm value received while not pairing";
    return;
  }

  if (!legacy_state_->InPhase2()) {
    FXL_LOG(ERROR)
        << "sm: Abort pairing due to confirm value received outside phase 2";
    AbortLegacyPairing(ErrorCode::kUnspecifiedReason);
    return;
  }

  // TODO(armansito): For now Phase 2 implies that we have a temporary key.
  // Remove this assertion when TK is obtained asynchronously. In that scenario,
  // if the TK hasn't yet been obtained and we are the initiator, then abort
  // pairing.
  FXL_DCHECK(legacy_state_->has_tk);

  // Abort pairing if we received a second random value from the peer. The
  // specification defines a certain order for the phase 2 commands.
  if (legacy_state_->has_peer_rand) {
    FXL_LOG(ERROR) << "sm: Already received confirm value! Aborting";
    AbortLegacyPairing(ErrorCode::kUnspecifiedReason);
    return;
  }

  // The random value shouldn't be sent before the confirm value. (See Vol 3,
  // Part H, 2.3.5.5 and Appendix C.2.1.1 for the specific order of events.
  if (!legacy_state_->has_peer_confirm) {
    FXL_LOG(ERROR) << "sm: \"Pairing Rand\" expected after \"Pairing Confirm\"";
    AbortLegacyPairing(ErrorCode::kUnspecifiedReason);
    return;
  }

  // Check that the order of the SMP commands is correct.
  if (legacy_state_->features->initiator) {
    FXL_DCHECK(le_smp_->role() == hci::Connection::Role::kMaster);

    // The master distributes both values before the slave sends Srandom.
    if (!legacy_state_->sent_local_rand || !legacy_state_->sent_local_confirm) {
      FXL_LOG(ERROR) << "sm: \"Pairing Random\" received in wrong order!";
      AbortLegacyPairing(ErrorCode::kUnspecifiedReason);
      return;
    }
  } else {
    // We cannot have sent the Srand without receiving Mrand first.
    FXL_DCHECK(!legacy_state_->sent_local_rand);

    // We need to send Sconfirm before the master sends Mrand.
    if (!legacy_state_->sent_local_confirm) {
      FXL_LOG(ERROR) << "sm: \"Pairing Random\" received in wrong order!";
      AbortLegacyPairing(ErrorCode::kUnspecifiedReason);
      return;
    }
  }

  legacy_state_->peer_rand = random;
  legacy_state_->has_peer_rand = true;

  // We have both confirm and rand values from the peer. Generate it locally and
  // compare.
  DeviceAddress *ia, *ra;
  LEPairingAddresses(&ia, &ra);
  UInt128 peer_confirm;
  util::C1(legacy_state_->tk, legacy_state_->peer_rand, legacy_state_->preq,
           legacy_state_->pres, *ia, *ra, &peer_confirm);
  if (peer_confirm != legacy_state_->peer_confirm) {
    FXL_LOG(ERROR) << "sm: " << (legacy_state_->features->initiator ? "S" : "M")
                   << "confirm value does not match!";
    AbortLegacyPairing(ErrorCode::kConfirmValueFailed);
    return;
  }

  if (legacy_state_->features->initiator) {
    // TODO(armansito): Generate STK and start encryption.
  } else {
    FXL_DCHECK(le_smp_->role() == hci::Connection::Role::kSlave);

    // Send Srand and wait for the master to encrypt the link with the STK.
    LegacySendRandomValue();
  }
}

void PairingState::LEPairingAddresses(DeviceAddress** out_initiator,
                                      DeviceAddress** out_responder) {
  FXL_DCHECK(legacy_state_);
  FXL_DCHECK(legacy_state_->features);

  if (legacy_state_->features->initiator) {
    *out_initiator = &le_local_addr_;
    *out_responder = &le_peer_addr_;
  } else {
    *out_initiator = &le_peer_addr_;
    *out_responder = &le_local_addr_;
  }
}

}  // namespace sm
}  // namespace btlib
