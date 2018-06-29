// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <queue>

#include "garnet/drivers/bluetooth/lib/common/optional.h"
#include "garnet/drivers/bluetooth/lib/hci/link_key.h"
#include "garnet/drivers/bluetooth/lib/l2cap/channel.h"
#include "garnet/drivers/bluetooth/lib/sm/bearer.h"
#include "garnet/drivers/bluetooth/lib/sm/status.h"
#include "garnet/drivers/bluetooth/lib/sm/types.h"
#include "lib/fxl/macros.h"

namespace btlib {
namespace sm {

// Represents the pairing state of a connected peer. The peer device must be a
// LE or a BR/EDR/LE device.
class PairingState final {
 public:
  explicit PairingState(IOCapability io_capability);
  ~PairingState();

  // Event triggerred when a new LE Long Term Key is obtained for this
  // connection.
  using LELTKCallback = fit::function<void(const LTK& ltk)>;
  void set_le_ltk_callback(LELTKCallback callback) {
    le_ltk_callback_ = std::move(callback);
  }

  // TODO(armansito): Add events for received keys.
  // TODO(armansito): Add PairingDelegate events.

  // Register a LE link. This method cannot be called on the same PairingState
  // instance more than once.
  void RegisterLE(fxl::WeakPtr<hci::Connection> link,
                  fbl::RefPtr<l2cap::Channel> smp);

  // Attempt to raise the security level of a the connection to the desired
  // |level| and notify the result in |callback|.
  //
  // If the desired security properties are already satisfied, this procedure
  // will succeed immediately.
  //
  // If a pairing procedure has already been initiated (either by us or the
  // peer), the request will be queued and |callback| will be notified when the
  // procedure completes. If the resulting security level does not satisfy
  // |level|, pairing will be re-initiated.
  //
  // If no pairing is in progress then the local device will initiate pairing.
  //
  // If pairing fails |callback| will be called with a |status| that represents
  // the error.
  using PairingCallback =
      fit::function<void(Status status, const SecurityProperties& sec_props)>;
  void UpdateSecurity(SecurityLevel level, PairingCallback callback);

 private:
  static constexpr size_t kPairingRequestSize =
      sizeof(Header) + sizeof(PairingRequestParams);

  // Represents the state for LE legacy pairing.
  struct LegacyState final {
    LegacyState();

    bool InPhase1() const;
    bool InPhase2() const;
    bool InPhase3() const;
    bool IsComplete() const;

    // True if all keys that are expected from the remote have been received.
    bool RequestedKeysObtained() const;

    bool ShouldReceiveLTK() const;  // True if peer should send the LTK
    bool ShouldSendLTK() const;     // True if we should send the LTK

    // True if LTK will be exchanged and the link is yet to be encrypted with
    // the LTK.
    bool WaitingForEncryptionWithLTK() const;

    // The pairing features obtained during Phase 1. If invalid, we're in
    // Phase 1. Otherwise, we're in Phase 2.
    common::Optional<PairingFeatures> features;

    // True if the link has been encrypted with the STK. This means that we're
    // in Phase 3. Otherwise we're in Phase 1 or 2.
    bool stk_encrypted;

    // True if the link has been encrypted with the LTK. If the LTK should be
    // exchanged, then pairing is considered complete when the link is
    // encrypted with the LTK.
    bool ltk_encrypted;

    // The remote keys that have been obtained so far.
    KeyDistGenField obtained_remote_keys;

    // Data used to generate STK and confirm values in Phase 2.
    bool has_tk;
    bool has_peer_confirm;
    bool has_peer_rand;
    bool sent_local_confirm;
    bool sent_local_rand;
    common::UInt128 tk;
    common::UInt128 local_confirm;
    common::UInt128 peer_confirm;
    common::UInt128 local_rand;
    common::UInt128 peer_rand;
    common::StaticByteBuffer<kPairingRequestSize> preq;
    common::StaticByteBuffer<kPairingRequestSize> pres;

    // Data from the peer tracked during the Phase 3. Parts of LTK are received
    // in separate events.
    bool has_ltk;
    common::UInt128 ltk_bytes;  // LTK without ediv/rand

    // Keys obtained during pairing.
    common::Optional<hci::LinkKey> ltk;  // LTK with ediv/rand
  };

  // Represents a pending request to update the security level.
  struct PendingRequest {
    PendingRequest(SecurityLevel level, PairingCallback callback);
    PendingRequest(PendingRequest&&) = default;
    PendingRequest& operator=(PendingRequest&&) = default;

    SecurityLevel level;
    PairingCallback callback;
  };

  // Aborts an ongoing legacy pairing procedure.
  void AbortLegacyPairing(ErrorCode error_code);

  // Begin Phase 1 of LE legacy pairing with the given |level|.
  void BeginLegacyPairingPhase1(SecurityLevel level);

  // Begin Phase 2 of LE legacy pairing. This is called after LE pairing
  // features have been exchanged and results (asynchronously) in the generation
  // and encryption of a link using the STK. This follows (roughly) the
  // following steps:
  //    1. Asynchronously obtain the TK.
  //    2. Generate the local confirm/rand values.
  //    3. If initiator, start the exchange, otherwise wait for the peer to send
  //    its confirm value.
  void BeginLegacyPairingPhase2(const common::ByteBuffer& preq,
                                const common::ByteBuffer& pres);
  void LegacySendConfirmValue();
  void LegacySendRandomValue();

  // Called when the link is encrypted with the STK at the end of Legacy
  // Pairing Phase 2.
  void EndLegacyPairingPhase2();

  // Completes the legacy pairing process by cleaning up pairing state, updates
  // the current security level, and notifies parties that have requested
  // security.
  void CompleteLegacyPairing();

  // Called when pairing features have been exchanged over the LE transport.
  void OnLEPairingFeatures(const PairingFeatures& features,
                           const common::ByteBuffer& preq,
                           const common::ByteBuffer& pres);

  // Called when pairing fails or is aborted over the LE transport.
  void OnLEPairingFailed(Status status);

  // Called when pairing confirm and random values are received.
  void OnLEPairingConfirm(const common::UInt128& confirm);
  void OnLEPairingRandom(const common::UInt128& random);

  // Called when information about the LE legacy LTK is received.
  void OnLELongTermKey(const common::UInt128& ltk);
  void OnLEMasterIdentification(uint16_t ediv, uint64_t random);

  // Called when the encryption state of the LE link changes.
  void OnLEEncryptionChange(hci::Status status, bool enabled);

  // Returns pointers to the initiator and responder addresses. This can be
  // called after pairing Phase 1.
  void LEPairingAddresses(common::DeviceAddress** out_initiator,
                          common::DeviceAddress** out_responder);

  // Callbacks used to notify obtained keys during pairing.
  LELTKCallback le_ltk_callback_;

  // TODO(armansito): Make it possible to change I/O capabilities.
  IOCapability ioc_;

  // Data for the currently registered LE-U link, if any. This data is valid
  // only if |le_smp_| is not nullptr.
  fxl::WeakPtr<hci::Connection> le_link_;
  std::unique_ptr<Bearer> le_smp_;       // SMP data bearer for the LE-U link.
  common::DeviceAddress le_local_addr_;  // Local address used while connecting.
  common::DeviceAddress le_peer_addr_;   // Peer address used while connecting.
  SecurityProperties le_sec_;  // Current security properties of the LE-U link.

  // The state of the LE legacy pairing procedure, if any.
  std::unique_ptr<LegacyState> legacy_state_;

  // The pending security requests added via UpdateSecurity().
  std::queue<PendingRequest> request_queue_;

  // TODO(armansito): Support SMP over ACL-U for LE Secure Connections.

  FXL_DISALLOW_COPY_AND_ASSIGN(PairingState);
};

}  // namespace sm
}  // namespace btlib
