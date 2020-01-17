// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/msgbuf_proto.h"

#include <zircon/errors.h>
#include <zircon/syscalls.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"

namespace wlan {
namespace brcmfmac {
namespace {

// Create a brcmf_proto instance with the appropriate trampolines.
std::unique_ptr<brcmf_proto> CreateProto(MsgbufProto* msgbuf) {
  auto proto = std::make_unique<brcmf_proto>();

  proto->hdrpull = [](brcmf_pub* drvr, bool do_fws, brcmf_netbuf* netbuf, brcmf_if** ifp) {
    return reinterpret_cast<MsgbufProto*>(drvr->proto->pd)->HdrPull(do_fws, netbuf, ifp);
  };
  proto->query_dcmd = [](brcmf_pub* drvr, int ifidx, uint cmd, void* buf, uint len,
                         int32_t* fwerr) {
    return reinterpret_cast<MsgbufProto*>(drvr->proto->pd)->QueryDcmd(ifidx, cmd, buf, len, fwerr);
  };
  proto->set_dcmd = [](brcmf_pub* drvr, int ifidx, uint cmd, void* buf, uint len, int32_t* fwerr) {
    return reinterpret_cast<MsgbufProto*>(drvr->proto->pd)->SetDcmd(ifidx, cmd, buf, len, fwerr);
  };
  proto->tx_queue_data = [](brcmf_pub* drvr, int ifidx, std::unique_ptr<Netbuf> netbuf) {
    return reinterpret_cast<MsgbufProto*>(drvr->proto->pd)->TxQueueData(ifidx, std::move(netbuf));
  };
  proto->configure_addr_mode = [](brcmf_pub* drvr, int ifidx, proto_addr_mode addr_mode) {
    return reinterpret_cast<MsgbufProto*>(drvr->proto->pd)->ConfigureAddrMode(ifidx, addr_mode);
  };
  proto->delete_peer = [](brcmf_pub* drvr, int ifidx, uint8_t peer[ETH_ALEN]) {
    return reinterpret_cast<MsgbufProto*>(drvr->proto->pd)->DeletePeer(ifidx, peer);
  };
  proto->add_tdls_peer = [](brcmf_pub* drvr, int ifidx, uint8_t peer[ETH_ALEN]) {
    return reinterpret_cast<MsgbufProto*>(drvr->proto->pd)->AddTdlsPeer(ifidx, peer);
  };
  proto->rxreorder = [](brcmf_if* ifp, brcmf_netbuf* netbuf) {
    return reinterpret_cast<MsgbufProto*>(ifp->drvr->proto->pd)->RxReorder(netbuf);
  };

  proto->pd = msgbuf;
  return proto;
}

}  // namespace

MsgbufProto::MsgbufProto() = default;

MsgbufProto::~MsgbufProto() = default;

// static
zx_status_t MsgbufProto::Create(Device* device, DmaBufferProviderInterface* dma_buffer_provider,
                                DmaRingProviderInterface* dma_ring_provider,
                                InterruptProviderInterface* interrupt_provider,
                                std::unique_ptr<MsgbufProto>* out_msgbuf) {
  auto msgbuf = std::make_unique<MsgbufProto>();

  auto proto = CreateProto(msgbuf.get());

  msgbuf->proto_ = std::move(proto);
  *out_msgbuf = std::move(msgbuf);
  return ZX_OK;
}

zx_status_t MsgbufProto::HdrPull(bool do_fws, brcmf_netbuf* netbuf, brcmf_if** ifp) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MsgbufProto::QueryDcmd(int ifidx, uint cmd, void* buf, uint len, int32_t* fwerr) {
  BRCMF_ERR("MsgbufProto::QueryDcmd unimplemented\n");
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MsgbufProto::SetDcmd(int ifidx, uint cmd, void* buf, uint len, int32_t* fwerr) {
  return QueryDcmd(ifidx, cmd, buf, len, fwerr);
}

zx_status_t MsgbufProto::TxQueueData(int ifidx, std::unique_ptr<Netbuf> netbuf) {
  BRCMF_ERR("MsgbufProto::TxQueueData unimplemented\n");
  netbuf->Return(ZX_ERR_NOT_SUPPORTED);
  return ZX_ERR_NOT_SUPPORTED;
}

void MsgbufProto::ConfigureAddrMode(int ifidx, proto_addr_mode addr_mode) {
  BRCMF_ERR("MsgbufProto::ConfigureAddrMode unimplemented\n");
}

void MsgbufProto::DeletePeer(int ifidx, uint8_t peer[ETH_ALEN]) {
  BRCMF_ERR("MsgbufProto::DeletePeer unimplemented\n");
}

void MsgbufProto::AddTdlsPeer(int ifidx, uint8_t peer[ETH_ALEN]) {
  BRCMF_ERR("MsgbufProto::AddTdlsPeer unimplemented\n");
}

void MsgbufProto::RxReorder(brcmf_netbuf* netbuf) {}

brcmf_proto* MsgbufProto::GetProto() { return proto_.get(); }

}  // namespace brcmfmac
}  // namespace wlan
