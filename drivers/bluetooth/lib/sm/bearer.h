// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/async/cpp/task.h>

#include "garnet/drivers/bluetooth/lib/hci/connection.h"
#include "garnet/drivers/bluetooth/lib/l2cap/channel.h"
#include "garnet/drivers/bluetooth/lib/l2cap/scoped_channel.h"
#include "garnet/drivers/bluetooth/lib/l2cap/sdu.h"
#include "garnet/drivers/bluetooth/lib/sm/packet.h"
#include "garnet/drivers/bluetooth/lib/sm/smp.h"
#include "garnet/drivers/bluetooth/lib/sm/status.h"

#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace btlib {
namespace sm {

// Represents the SMP data bearer operating over the fixed SMP L2CAP channel.
// Bearer encapsulates the pairing algorithms described in Vol 3, Part H, 2.3.5
// as distinct sub-procedures that are expected to be driven externally. The
// client is responsible for initiating the right sub-procedure at the right
// time.
//
// A SMP bearer can be instantiated over both LE and BR/EDR transports.
//
// THREAD SAFETY
//
// This class is not thread safe and is meant to be accessed on the thread it
// was created on. All callbacks will be run by the default dispatcher of a
// Bearer's creation thread.
class Bearer final {
 public:
  // Callback used to communicate the result of the "Pairing Feature Exchange"
  // sub-procedure (i.e. Phase 1). This can be called when pairing is either
  // remote or locally initated, with the following parameters:
  //
  //   - |initiator|: True if the local device is in the "initiator" role.
  //   - |secure_connections|: True if LE Secure Connections pairing should be
  //     used. Otherwise, LE Legacy pairing should be used.
  //   - |method|: Indicates the key generation model used for Phase 2.
  //   - |encryption_key_size|: The negotiated encryption key size.
  //   - |local_key_distribution|: The keys that we must distribute to the peer.
  //   - |remote_key_distribution|: The keys that will be distributed to us by
  //     the peer.
  //   - |preq| and |pres|: The SMP "Pairing Request" and "Pairing Response"
  //     command payloads that have been exchanged between the devices. These
  //     values are used to generate "Mconfirm" and "Sconfirm" values used in LE
  //     Legacy Pairing Phase 2 (see Vol 3, Part H, 2.3.5.5). These values
  //     should be ignored if |secure_connections| is true.
  //
  // When the local device is the master, the feature exchange is either
  // initiated directly via InitiateFeatureExchange() or automatically as a
  // response to a "Security Request" received from the slave.
  //
  // When the local device is the slave, the feature exchange is initiated by
  // the master or locally by calling SecurityRequest().
  //
  // TODO(armansito): Support locally initiated "Security Request".
  //
  // The Pairing Feature Exchange procedures will fail if no feature exchange
  // callback is assigned.
  struct PairingFeatures {
    PairingFeatures();
    PairingFeatures(bool initiator, bool sc, PairingMethod method,
                    uint8_t enc_key_size, KeyDistGenField local_kd,
                    KeyDistGenField remote_kd);

    bool initiator;
    bool secure_connections;
    PairingMethod method;
    uint8_t encryption_key_size;
    KeyDistGenField local_key_distribution;
    KeyDistGenField remote_key_distribution;
  };
  using FeatureExchangeCallback = fbl::Function<void(
      const PairingFeatures& features, const common::ByteBuffer& preq,
      const common::ByteBuffer& pres)>;

  // Initializes this Bearer with the following parameters:
  //   - |chan|: The L2CAP SMP fixed channel.
  //   - |role|: The local connection role.
  //   - |secure_connections_supported|: True if the local device supports LE
  //     Secure Connections pairing.
  //   - |io_capability|: The local I/O capability.
  Bearer(fbl::RefPtr<l2cap::Channel> chan, hci::Connection::Role role,
         bool secure_connections_supported, IOCapability io_capability,
         StatusCallback error_callback,
         FeatureExchangeCallback feature_exchange_callback);
  ~Bearer() = default;

  // Sets whether or not OOB authentication data is available. False by default.
  void set_oob_available(bool value) { oob_available_ = value; }

  // Sets whether MITM protection is required. False by default.
  void set_mitm_required(bool value) { mitm_required_ = value; }

  // Returns true if pairing has been initiated.
  bool pairing_started() const { return timeout_task_.is_pending(); }

  // Initiates "Pairing Feature Exchange" with the local device as the
  // initiator (Vol 3, Part H, 2.3). A successful exchange will be indicated by
  // calling via feature exchange callback and failure via the error callback.
  //
  // Returns false if the procedure cannot be initiated because:
  //   - This or another sub-procedure is in progress.
  //   - The local device is the slave in the connection.
  //
  // This method can be called on both LE and BR/EDR.
  bool InitiateFeatureExchange();

  // Stops the pairing timer. The pairing timer is started when a Pairing
  // Request or Security Request is sent or received and must be explicitly
  // stopped once all required keys have been distributed.
  //
  // A L2CAP link error will be signaled if the timer expires within
  // kPairingTimeout seconds (see smp.h).
  void StopTimer();

  // Ends the current pairing procedure with the given failure |ecode|.
  void Abort(ErrorCode ecode);

 private:
  // Cleans up all pairing state and and invokes the error calback.
  void OnFailure(Status status);

  // Called when the SMP pairing timer expires.
  void OnPairingTimeout();

  // Called for SMP commands that are sent by the peer.
  void OnPairingFailed(const PacketReader& reader);
  void OnPairingResponse(const PacketReader& reader);

  // Called to send SMP commands to the peer.
  void SendPairingFailed(ErrorCode ecode);

  // l2cap::Channel callbacks:
  void OnChannelClosed();
  void OnRxBFrame(const l2cap::SDU& sdu);

  l2cap::ScopedChannel chan_;
  hci::Connection::Role role_;
  bool oob_available_;
  bool mitm_required_;
  bool sc_supported_;
  IOCapability io_capability_;

  uint8_t mtu_;
  StatusCallback error_callback_;
  FeatureExchangeCallback feature_exchange_callback_;

  // The most recently sent Pairing Request command. We store it here to be
  // reported in the feature exchange callback.
  // NOTE: The SMP specification frequently refers to this as "preq" which is
  // the convention we use here (see Vol 3, Part H, 2.2.3 for example).
  common::StaticByteBuffer<sizeof(Header) + sizeof(PairingRequestParams)> preq_;

  // Task used to drive the "SMP Timeout" (Vol 3, Part H, 3.4). The timer is
  // started when pairing is initiated.
  async::TaskClosureMethod<Bearer, &Bearer::OnPairingTimeout> timeout_task_{
      this};

  // True if a pairing feature exchange has been initiated and waiting for a
  // response.
  bool feature_exchange_pending_;

  fxl::WeakPtrFactory<Bearer> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Bearer);
};

}  // namespace sm
}  // namespace btlib
