// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "mac_frame.h"

#include "lib/wlan/fidl/wlan_mlme.fidl-common.h"
#include "lib/wlan/fidl/wlan_mlme_ext.fidl-common.h"

#include <ddk/protocol/wlan.h>
#include <zircon/types.h>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#define WLAN_DECL_VIRT_FUNC_HANDLE(methodName, args...) \
    virtual zx_status_t methodName(args) { return ZX_OK; }

#define WLAN_DECL_FUNC_HANDLE_MLME(methodName, mlmeMsgType) \
    WLAN_DECL_VIRT_FUNC_HANDLE(methodName, const mlmeMsgType&)

#define WLAN_DECL_FUNC_INTERNAL_HANDLE_MLME(methodName, mlmeMsgType)                    \
    zx_status_t HandleMlmeFrameInternal(const Method& method, const mlmeMsgType& msg) { \
        return methodName(msg);                                                         \
    }

#define WLAN_DECL_FUNC_HANDLE_MGMT(mgmtFrameType)                                      \
    WLAN_DECL_VIRT_FUNC_HANDLE(Handle##mgmtFrameType, const MgmtFrame<mgmtFrameType>&, \
                               const wlan_rx_info_t&)

#define WLAN_DECL_FUNC_INTERNAL_HANDLE_MGMT(mgmtFrameType)                     \
    zx_status_t HandleMgmtFrameInternal(const MgmtFrame<mgmtFrameType>& frame, \
                                        const wlan_rx_info_t& info) {          \
        return Handle##mgmtFrameType(frame, info);                             \
    }

#define WLAN_DECL_VIRT_FUNC_HANDLE_DATA(methodName, BodyType)                  \
    WLAN_DECL_VIRT_FUNC_HANDLE(Handle##methodName, const DataFrame<BodyType>&, \
                               const wlan_rx_info_t&)

#define WLAN_DECL_FUNC_INTERNAL_HANDLE_DATA(methodName, BodyType)         \
    zx_status_t HandleDataFrameInternal(const DataFrame<BodyType>& frame, \
                                        const wlan_rx_info_t& rxinfo) {   \
        return Handle##methodName(frame, rxinfo);                         \
    }

namespace wlan {

// FrameHandler provides frame handling capabilities. It is not thread safe.
// By default the FrameHandler will ignore every frame. If the component wishes to handle a
// specific frame it must override the corresponding method, for example HandleBeacon(...). The
// FrameHandler also offers to provide custom logic for dropping frames in a centralized manner
// by overriding ShouldDropXYZ(...). Using these methods for dropping results in two benefits,
// (1) frame filtering logic is in one single place rather than being spread out through
// various HandleXYZ(...) methods, and
// (2) dropped frames will not be forwarded to forwarding targets.
// Children are targets which frames are automatically forwarded to.
// Frames are only forwarded to the children if they were not dropped by the parent, and
// the parent did not fail processing the frame itself.
// Frame processing errors returned by children are logged but have no effect onto their
// siblings or the parent.
class FrameHandler : public fbl::RefCounted<FrameHandler> {
   public:
    FrameHandler() {}
    virtual ~FrameHandler() = default;

    template <typename... Args> zx_status_t HandleFrame(Args&&... args) {
        auto status = HandleFrameInternal(std::forward<Args>(args)...);
        // Do not forward frame if it was dropped.
        if (status == ZX_ERR_STOP) { return ZX_OK; }
        // Do not forward frame if processing failed.
        if (status != ZX_OK) { return status; }

        // TODO(hahnr): Extract forwarding logic into dedicated component.
        // Forward frame to all children.
        uint16_t i = 0;
        for (auto& handler : children_) {
            status = handler->HandleFrame(std::forward<Args>(args)...);
            // Log when a child failed processing a frame, but proceed.
            if (status != ZX_OK) {
                debugfhandler("child %u failed handling frame: %d\n", i, status);
            }
            i++;
        }
        return ZX_OK;
    }

    void AddChildHandler(fbl::RefPtr<FrameHandler> ptr) { children_.push_back(ptr); }

    void RemoveChildHandler(fbl::RefPtr<FrameHandler> ptr) {
        children_.erase(
            std::remove_if(children_.begin(), children_.end(),
                           [ptr](fbl::RefPtr<FrameHandler>& entry) { return entry == ptr; }),
            children_.end());
    }

   protected:
    // Ethernet frame handlers.
    virtual bool ShouldDropEthFrame(const BaseFrame<EthernetII>& frame) { return false; }
    WLAN_DECL_VIRT_FUNC_HANDLE(HandleEthFrame, const BaseFrame<EthernetII>& frame)

    // Service Message handlers.
    virtual bool ShouldDropMlmeMessage(const Method& method) { return false; }
    WLAN_DECL_FUNC_HANDLE_MLME(HandleMlmeResetReq, ResetRequest)
    WLAN_DECL_FUNC_HANDLE_MLME(HandleMlmeScanReq, ScanRequest)
    WLAN_DECL_FUNC_HANDLE_MLME(HandleMlmeJoinReq, JoinRequest)
    WLAN_DECL_FUNC_HANDLE_MLME(HandleMlmeAuthReq, AuthenticateRequest)
    WLAN_DECL_FUNC_HANDLE_MLME(HandleMlmeDeauthReq, DeauthenticateRequest)
    WLAN_DECL_FUNC_HANDLE_MLME(HandleMlmeAssocReq, AssociateRequest)
    WLAN_DECL_FUNC_HANDLE_MLME(HandleMlmeEapolReq, EapolRequest)
    WLAN_DECL_FUNC_HANDLE_MLME(HandleMlmeSetKeysReq, SetKeysRequest)
    WLAN_DECL_FUNC_HANDLE_MLME(HandleMlmeStartReq, StartRequest)
    WLAN_DECL_FUNC_HANDLE_MLME(HandleMlmeStopReq, StopRequest)

    // Data frame handlers.
    virtual bool ShouldDropDataFrame(const DataFrameHeader& hdr) { return false; }
    WLAN_DECL_VIRT_FUNC_HANDLE_DATA(NullDataFrame, NilHeader)
    WLAN_DECL_VIRT_FUNC_HANDLE_DATA(DataFrame, LlcHeader)

    // Management frame handlers.
    virtual bool ShouldDropMgmtFrame(const MgmtFrameHeader& hdr) { return false; }
    WLAN_DECL_FUNC_HANDLE_MGMT(Beacon)
    WLAN_DECL_FUNC_HANDLE_MGMT(ProbeResponse)
    WLAN_DECL_FUNC_HANDLE_MGMT(Authentication)
    WLAN_DECL_FUNC_HANDLE_MGMT(Deauthentication)
    WLAN_DECL_FUNC_HANDLE_MGMT(AssociationRequest)
    WLAN_DECL_FUNC_HANDLE_MGMT(AssociationResponse)
    WLAN_DECL_FUNC_HANDLE_MGMT(Disassociation)
    WLAN_DECL_FUNC_HANDLE_MGMT(AddBaRequestFrame)

   private:
    // Internal Service Message handlers.
    template <typename Message>
    zx_status_t HandleFrameInternal(const Method& method, const Message& msg) {
        if (ShouldDropMlmeMessage(method)) { return ZX_ERR_STOP; }
        return HandleMlmeFrameInternal(method, msg);
    }
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MLME(HandleMlmeResetReq, ResetRequest)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MLME(HandleMlmeScanReq, ScanRequest)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MLME(HandleMlmeJoinReq, JoinRequest)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MLME(HandleMlmeAuthReq, AuthenticateRequest)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MLME(HandleMlmeDeauthReq, DeauthenticateRequest)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MLME(HandleMlmeAssocReq, AssociateRequest)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MLME(HandleMlmeEapolReq, EapolRequest)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MLME(HandleMlmeSetKeysReq, SetKeysRequest)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MLME(HandleMlmeStartReq, StartRequest)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MLME(HandleMlmeStopReq, StopRequest)

    // Internal Management frame handlers.
    template <typename Body>
    zx_status_t HandleFrameInternal(const MgmtFrame<Body>& frame, const wlan_rx_info_t& info) {
        if (ShouldDropMgmtFrame(*frame.hdr)) { return ZX_ERR_STOP; }
        return HandleMgmtFrameInternal(frame, info);
    }
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MGMT(Beacon)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MGMT(ProbeResponse)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MGMT(Authentication)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MGMT(Deauthentication)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MGMT(AssociationRequest)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MGMT(AssociationResponse)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MGMT(Disassociation)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MGMT(AddBaRequestFrame)

    // Internal Ethernet frame handlers.
    zx_status_t HandleFrameInternal(const BaseFrame<EthernetII>& frame) {
        if (ShouldDropEthFrame(frame)) { return ZX_ERR_STOP; }
        return HandleEthFrame(frame);
    }

    // Internal Data frame handlers.
    template <typename Body>
    zx_status_t HandleFrameInternal(const DataFrame<Body>& frame, const wlan_rx_info_t& info) {
        if (ShouldDropDataFrame(*frame.hdr)) { return ZX_ERR_STOP; }
        return HandleDataFrameInternal(frame, info);
    }
    WLAN_DECL_FUNC_INTERNAL_HANDLE_DATA(NullDataFrame, NilHeader)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_DATA(DataFrame, LlcHeader)

    std::vector<fbl::RefPtr<FrameHandler>> children_;
};

}  // namespace wlan
