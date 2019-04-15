// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_MESH_MESH_MLME_H_
#define SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_MESH_MESH_MLME_H_

#include <wlan/common/buffer_reader.h>
#include <wlan/common/parse_mac_header.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/mac_header_writer.h>
#include <wlan/mlme/mesh/deduplicator.h>
#include <wlan/mlme/mesh/hwmp.h>
#include <wlan/mlme/mesh/path_table.h>
#include <wlan/mlme/mlme.h>
#include <wlan/mlme/rust_utils.h>

namespace wlan {

template <typename T>
class MlmeMsg;

class MeshMlme : public Mlme {
 public:
  explicit MeshMlme(DeviceInterface* device);

  // Mlme interface methods.
  zx_status_t Init() override;
  zx_status_t HandleMlmeMsg(const BaseMlmeMsg& msg) override;
  zx_status_t HandleFramePacket(fbl::unique_ptr<Packet> pkt) override;
  zx_status_t HandleTimeout(const ObjectId id) override;

 private:
  const common::MacAddr& self_addr() const {
    return device_->GetState()->address();
  }

  ::fuchsia::wlan::mlme::StartResultCodes Start(
      const MlmeMsg<::fuchsia::wlan::mlme::StartRequest>& req);
  ::fuchsia::wlan::mlme::StopResultCodes Stop();
  void SendPeeringOpen(
      const MlmeMsg<::fuchsia::wlan::mlme::MeshPeeringOpenAction>& req);
  void SendPeeringConfirm(
      const MlmeMsg<::fuchsia::wlan::mlme::MeshPeeringConfirmAction>& req);
  void ConfigurePeering(
      const MlmeMsg<::fuchsia::wlan::mlme::MeshPeeringParams>& params);

  void SendDataFrame(fbl::unique_ptr<Packet> packet);
  void SendMgmtFrame(fbl::unique_ptr<Packet> packet);
  void SendMgmtFrames(PacketQueue packets);

  void HandleEthTx(EthFrame&& frame);

  zx_status_t HandleAnyWlanFrame(fbl::unique_ptr<Packet> pkt);
  zx_status_t HandleAnyMgmtFrame(MgmtFrame<>&& frame);
  zx_status_t HandleActionFrame(const MgmtFrameHeader& mgmt, BufferReader* r);
  zx_status_t HandleSelfProtectedAction(const common::MacAddr& src_addr,
                                        BufferReader* r);
  zx_status_t HandleMpmOpenAction(const common::MacAddr& src_addr,
                                  BufferReader* r);
  zx_status_t HandleMpmConfirmAction(const common::MacAddr& src_addr,
                                     BufferReader* r);
  void HandleMeshAction(const MgmtFrameHeader& mgmt, BufferReader* r);

  const MeshPath* QueryPathTable(const common::MacAddr& mesh_dest);
  void TriggerPathDiscovery(const common::MacAddr& target);

  void HandleDataFrame(fbl::unique_ptr<Packet> packet);
  bool ShouldDeliverData(const common::ParsedDataFrameHeader& header);
  void DeliverData(const common::ParsedMeshDataHeader& header,
                   Span<uint8_t> wlan_frame, size_t payload_offset);
  std::optional<common::MacAddr> GetNextHopForForwarding(
      const common::ParsedMeshDataHeader& header);
  void ForwardData(const common::ParsedMeshDataHeader& header,
                   fbl::unique_ptr<Packet> packet,
                   const common::MacAddr& next_hop);

  MacHeaderWriter CreateMacHeaderWriter();

  struct MeshState {
    HwmpState hwmp;
    PathTable path_table;
    DeDuplicator deduplicator;

    explicit MeshState(fbl::unique_ptr<Timer> timer);
  };

  DeviceInterface* const device_;
  SequenceManager seq_mgr_;
  uint32_t mesh_seq_ = 0;
  std::optional<MeshState> state_;
};

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_MESH_MESH_MLME_H_
