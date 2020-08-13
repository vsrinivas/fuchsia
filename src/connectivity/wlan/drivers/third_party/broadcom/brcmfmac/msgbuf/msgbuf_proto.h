// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_MSGBUF_MSGBUF_PROTO_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_MSGBUF_MSGBUF_PROTO_H_

#include <zircon/types.h>

#include <memory>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/core.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/device.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/msgbuf_interfaces.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/netbuf.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/proto.h"

namespace wlan {
namespace brcmfmac {

class MsgbufRingHandler;

// This class implements the brcmfmc MSGBUF protocol for communicating with the firmware running
// on a Broadcom PHY.  It implements the C-style interface as defined by brcmf_proto and used by the
// higher-level cfg80211 logic.
class MsgbufProto {
 public:
  MsgbufProto();
  ~MsgbufProto();

  // Static factory function for MsgbufProto instances.
  static zx_status_t Create(Device* device, DmaBufferProviderInterface* dma_buffer_provider,
                            DmaRingProviderInterface* dma_ring_provider,
                            InterruptProviderInterface* interrupt_provider,
                            std::unique_ptr<MsgbufProto>* out_msgbuf);

  // Proto interface implementation.
  zx_status_t HdrPull(bool do_fws, brcmf_netbuf* netbuf, brcmf_if** ifp);
  zx_status_t QueryDcmd(int ifidx, uint cmd, void* buf, uint len, bcme_status_t* fwerr);
  zx_status_t SetDcmd(int ifidx, uint cmd, void* buf, uint len, bcme_status_t* fwerr);
  zx_status_t TxQueueData(int ifidx, std::unique_ptr<Netbuf> netbuf);
  void ConfigureAddrMode(int ifidx, proto_addr_mode addr_mode);
  void DeletePeer(int ifidx, uint8_t peer[ETH_ALEN]);
  void AddTdlsPeer(int ifidx, uint8_t peer[ETH_ALEN]);
  void RxReorder(brcmf_netbuf* netbuf);

  // Get the brcmf_proto C interface for this MSGBUF protocol instance.
  brcmf_proto* GetProto();

 private:
  // Utility class to handle MsgbufRingHandler events.
  class RingEventHandler;

  Device* device_ = nullptr;
  std::unique_ptr<brcmf_proto> proto_;
  std::unique_ptr<RingEventHandler> ring_event_handler_;
  std::unique_ptr<MsgbufRingHandler> ring_handler_;
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_MSGBUF_MSGBUF_PROTO_H_
