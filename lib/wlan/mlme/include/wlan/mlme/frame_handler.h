// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/mac_frame.h>

#include <wlan_mlme/cpp/fidl.h>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <wlan/protocol/mac.h>
#include <zircon/types.h>

#define WLAN_DECL_VIRT_FUNC_HANDLE(methodName, args...) \
    virtual zx_status_t methodName(args) { return ZX_OK; }

#define WLAN_DECL_FUNC_HANDLE_MLME(methodName, mlmeMsgType) \
    WLAN_DECL_VIRT_FUNC_HANDLE(methodName, const wlan_mlme::mlmeMsgType&)

#define WLAN_DECL_FUNC_INTERNAL_HANDLE_MLME(methodName, mlmeMsgType)                    \
    zx_status_t HandleMlmeFrameInternal(uint32_t ordinal, const wlan_mlme::mlmeMsgType& msg) { \
        return methodName(msg);                                                         \
    }

#define WLAN_DECL_FUNC_HANDLE_MGMT(mgmtFrameType)                                               \
    WLAN_DECL_VIRT_FUNC_HANDLE(Handle##mgmtFrameType, const ImmutableMgmtFrame<mgmtFrameType>&, \
                               const wlan_rx_info_t&)

#define WLAN_DECL_FUNC_INTERNAL_HANDLE_MGMT(mgmtFrameType)                              \
    zx_status_t HandleMgmtFrameInternal(const ImmutableMgmtFrame<mgmtFrameType>& frame, \
                                        const wlan_rx_info_t& info) {                   \
        return Handle##mgmtFrameType(frame, info);                                      \
    }

#define WLAN_DECL_FUNC_HANDLE_CTRL(ctrlFrameType)                                               \
    WLAN_DECL_VIRT_FUNC_HANDLE(Handle##ctrlFrameType, const ImmutableCtrlFrame<ctrlFrameType>&, \
                               const wlan_rx_info_t&)

#define WLAN_DECL_FUNC_INTERNAL_HANDLE_CTRL(ctrlFrameType)                              \
    zx_status_t HandleCtrlFrameInternal(const ImmutableCtrlFrame<ctrlFrameType>& frame, \
                                        const wlan_rx_info_t& info) {                   \
        return Handle##ctrlFrameType(frame, info);                                      \
    }

#define WLAN_DECL_VIRT_FUNC_HANDLE_DATA(methodName, BodyType)                           \
    WLAN_DECL_VIRT_FUNC_HANDLE(Handle##methodName, const ImmutableDataFrame<BodyType>&, \
                               const wlan_rx_info_t&)

#define WLAN_DECL_FUNC_INTERNAL_HANDLE_DATA(methodName, BodyType)                  \
    zx_status_t HandleDataFrameInternal(const ImmutableDataFrame<BodyType>& frame, \
                                        const wlan_rx_info_t& rxinfo) {            \
        return Handle##methodName(frame, rxinfo);                                  \
    }

namespace wlan {

// FrameHandler provides frame handling capabilities. It is not thread safe.
// By default the FrameHandler will ignore every frame. If the component wishes to handle a
// specific frame it must override the corresponding method, for example
// HandleBeacon(MgmtFrame<Beacon>).
// The FrameHandler invokes multiple callbacks per frame with each callback providing a more
// specific frame type. Hence, the callback invoked last provides the fully resolved type of the
// frame, while an earlier callback might only know whether the current frame is a management or
// data frame. For example, for every received Beacon frame, the FrameHandler invokes the following
// callbacks: HandleAnyFrame() > HandleMgmtFrame(MgmtFrameHeader) >
// HandleBeaconFrame(MgmtFrame<Beacon>).
// This chain of handlers can be broken by returning ZX_ERR_STOP. This effectively results in a
// simple way to enrich components with sophisticated frame dropping logic. If a handler returns
// ZX_ERR_STOP the frame is not processed any further, for example, a client can drop all Beacon
// frames by returning ZX_ERR_STOP inside HandleMgmtFrame(MgmtFrameHeader) when the header indicates
// that the received frame is a Beacon.
// A FrameHandler also provides two ways of forwarding frames to other components. Either, a
// component is registered as a child of a FrameHandler, by invoking AddChildHandler(FrameHandler*),
// which will forward all incoming frames to the child, or by invoking
// ForwardCurrentFrameTo(FrameHandler*) which will only forward the current frame to the given
// handler (Note: the current frame can be forwarded to only one handler at a time).
// Use the latter option when dealing with dynamic frame targets, such as a forwarding the current
// frame to a specific client. Use the first option when the frame targets are fixed and the amount
// of targets is rather small.
class FrameHandler : public fbl::RefCounted<FrameHandler> {
   public:
    FrameHandler() {}
    virtual ~FrameHandler() = default;

    template <typename... Args> zx_status_t HandleFrame(Args&&... args) {
        auto status = HandleAnyFrame();
        // Do not forward frame if it was dropped.
        if (status == ZX_ERR_STOP) { return ZX_OK; }
        // Do not forward frame if processing failed.
        if (status != ZX_OK) { return status; }

        status = HandleFrameInternal(std::forward<Args>(args)...);
        if (status == ZX_ERR_STOP) { return ZX_OK; }
        if (status != ZX_OK) { return status; }

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

        // If there is a dynamic target registered, forward frame.
        if (dynamic_target_ != nullptr) {
            status = dynamic_target_->HandleFrame(std::forward<Args>(args)...);
            if (status != ZX_OK) {
                debugfhandler("dynamic target failed handling frame: %d\n", status);
            }
            dynamic_target_ = nullptr;
        }
        return ZX_OK;
    }

    void ForwardCurrentFrameTo(FrameHandler* handler) {
        ZX_DEBUG_ASSERT(handler != nullptr);
        ZX_DEBUG_ASSERT(dynamic_target_ == nullptr);
        dynamic_target_ = handler;
    }

    void AddChildHandler(fbl::RefPtr<FrameHandler> ptr) { children_.push_back(ptr); }

    void RemoveChildHandler(fbl::RefPtr<FrameHandler> ptr) {
        children_.erase(
            std::remove_if(children_.begin(), children_.end(),
                           [ptr](fbl::RefPtr<FrameHandler>& entry) { return entry == ptr; }),
            children_.end());
    }

   protected:
    virtual zx_status_t HandleAnyFrame() { return ZX_OK; }

    // Ethernet frame handlers.
    virtual zx_status_t HandleEthFrame(const ImmutableBaseFrame<EthernetII>& frame) {
        return ZX_OK;
    }

    // Service Message handlers.
    virtual zx_status_t HandleMlmeMessage(uint32_t ordinal) { return ZX_OK; }
    WLAN_DECL_FUNC_HANDLE_MLME(HandleMlmeResetReq, ResetRequest)
    WLAN_DECL_FUNC_HANDLE_MLME(HandleMlmeScanReq, ScanRequest)
    WLAN_DECL_FUNC_HANDLE_MLME(HandleMlmeJoinReq, JoinRequest)
    WLAN_DECL_FUNC_HANDLE_MLME(HandleMlmeAuthReq, AuthenticateRequest)
    WLAN_DECL_FUNC_HANDLE_MLME(HandleMlmeAuthResp, AuthenticateResponse)
    WLAN_DECL_FUNC_HANDLE_MLME(HandleMlmeDeauthReq, DeauthenticateRequest)
    WLAN_DECL_FUNC_HANDLE_MLME(HandleMlmeAssocReq, AssociateRequest)
    WLAN_DECL_FUNC_HANDLE_MLME(HandleMlmeAssocResp, AssociateResponse)
    WLAN_DECL_FUNC_HANDLE_MLME(HandleMlmeEapolReq, EapolRequest)
    WLAN_DECL_FUNC_HANDLE_MLME(HandleMlmeSetKeysReq, SetKeysRequest)
    WLAN_DECL_FUNC_HANDLE_MLME(HandleMlmeStartReq, StartRequest)
    WLAN_DECL_FUNC_HANDLE_MLME(HandleMlmeStopReq, StopRequest)

    // Data frame handlers.
    virtual zx_status_t HandleDataFrame(const DataFrameHeader& hdr) { return ZX_OK; }
    WLAN_DECL_VIRT_FUNC_HANDLE_DATA(NullDataFrame, NilHeader)
    // TODO(hahnr): Rename to something more specific since there are two HandleDataFrame methods
    // now.
    WLAN_DECL_VIRT_FUNC_HANDLE_DATA(DataFrame, LlcHeader)

    // Management frame handlers.
    virtual zx_status_t HandleMgmtFrame(const MgmtFrameHeader& hdr) { return ZX_OK; }
    WLAN_DECL_FUNC_HANDLE_MGMT(Beacon)
    WLAN_DECL_FUNC_HANDLE_MGMT(ProbeResponse)
    WLAN_DECL_FUNC_HANDLE_MGMT(ProbeRequest)
    WLAN_DECL_FUNC_HANDLE_MGMT(Authentication)
    WLAN_DECL_FUNC_HANDLE_MGMT(Deauthentication)
    WLAN_DECL_FUNC_HANDLE_MGMT(AssociationRequest)
    WLAN_DECL_FUNC_HANDLE_MGMT(AssociationResponse)
    WLAN_DECL_FUNC_HANDLE_MGMT(Disassociation)
    WLAN_DECL_FUNC_HANDLE_MGMT(AddBaRequestFrame)
    WLAN_DECL_FUNC_HANDLE_MGMT(AddBaResponseFrame)

    // Control frame handlers.
    virtual zx_status_t HandleCtrlFrame(const FrameControl& fc) { return ZX_OK; }
    WLAN_DECL_FUNC_HANDLE_CTRL(PsPollFrame)

   private:
    // Internal Service Message handlers.
    template <typename Message>
    zx_status_t HandleFrameInternal(uint32_t ordinal, const Message& msg) {
        auto status = HandleMlmeMessage(ordinal);
        if (status != ZX_OK) { return status; }

        return HandleMlmeFrameInternal(ordinal, msg);
    }
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MLME(HandleMlmeResetReq, ResetRequest)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MLME(HandleMlmeScanReq, ScanRequest)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MLME(HandleMlmeJoinReq, JoinRequest)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MLME(HandleMlmeAuthReq, AuthenticateRequest)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MLME(HandleMlmeAuthResp, AuthenticateResponse)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MLME(HandleMlmeDeauthReq, DeauthenticateRequest)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MLME(HandleMlmeAssocReq, AssociateRequest)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MLME(HandleMlmeAssocResp, AssociateResponse)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MLME(HandleMlmeEapolReq, EapolRequest)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MLME(HandleMlmeSetKeysReq, SetKeysRequest)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MLME(HandleMlmeStartReq, StartRequest)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MLME(HandleMlmeStopReq, StopRequest)

    // Internal Management frame handlers.
    template <typename Body>
    zx_status_t HandleFrameInternal(const ImmutableMgmtFrame<Body>& frame,
                                    const wlan_rx_info_t& info) {
        auto status = HandleMgmtFrame(*frame.hdr());
        if (status != ZX_OK) { return status; }

        return HandleMgmtFrameInternal(frame, info);
    }
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MGMT(Beacon)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MGMT(ProbeResponse)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MGMT(ProbeRequest)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MGMT(Authentication)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MGMT(Deauthentication)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MGMT(AssociationRequest)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MGMT(AssociationResponse)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MGMT(Disassociation)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MGMT(AddBaRequestFrame)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_MGMT(AddBaResponseFrame)

    // Internal Ethernet frame handlers.
    zx_status_t HandleFrameInternal(const ImmutableFrame<EthernetII, NilHeader>& frame) {
        return HandleEthFrame(frame);
    }

    // Internal Data frame handlers.
    template <typename Body>
    zx_status_t HandleFrameInternal(const ImmutableFrame<DataFrameHeader, Body>& frame,
                                    const wlan_rx_info_t& info) {
        auto status = HandleDataFrame(*frame.hdr());
        if (status != ZX_OK) { return status; }

        return HandleDataFrameInternal(frame, info);
    }
    WLAN_DECL_FUNC_INTERNAL_HANDLE_DATA(NullDataFrame, NilHeader)
    WLAN_DECL_FUNC_INTERNAL_HANDLE_DATA(DataFrame, LlcHeader)

    // Internal Control frame handlers.
    // Note: Null Data frames hold no body and thus also match this method. As a result, this case
    // is caught.
    template <typename Header>
    typename std::enable_if<!std::is_same<Header, DataFrameHeader>::value, zx_status_t>::type
    HandleFrameInternal(const ImmutableCtrlFrame<Header>& frame, const wlan_rx_info_t& info) {
        auto status = HandleCtrlFrame(frame.hdr()->fc);
        if (status != ZX_OK) { return status; }

        return HandleCtrlFrameInternal(frame, info);
    }
    WLAN_DECL_FUNC_INTERNAL_HANDLE_CTRL(PsPollFrame)

    // Frame target which will only receive the current frame. Will be reset after each frame.
    // TODO(hahnr): This is still not exactly what I fancy but good enough for now.
    // Ideally, this will turn into its own component which is also better testable and replaceable.
    // However, most of my prototyping hit compiler limitations.
    FrameHandler* dynamic_target_ = nullptr;
    // List of all children which all incoming frames should get forwarded to.
    std::vector<fbl::RefPtr<FrameHandler>> children_;
};

}  // namespace wlan
