// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "ncp_fidl.h"

#include <sstream>

namespace ot {
namespace Ncp {

static OT_DEFINE_ALIGNED_VAR(s_ncp_raw, sizeof(NcpFidl), uint64_t);

void otNcpInit(otInstance *a_instance) {
  NcpFidl *ncp_fidl = NULL;
  Instance *instance = reinterpret_cast<Instance *>(a_instance);
  ot_instance_ptr_ = a_instance;
  ncp_fidl = new (&s_ncp_raw) class NcpFidl(instance);
  if (ncp_fidl == NULL || ncp_fidl != NcpBase::GetNcpInstance()) {
    OT_ASSERT(false);
  }
}

NcpFidl *otNcpGetInstance() { return reinterpret_cast<NcpFidl *>(s_ncp_raw); }

NcpFidl::NcpFidl(Instance *a_instance) : NcpBase(a_instance) {
  mTxFrameBuffer.SetFrameAddedCallback(HandleFrameAddedToNcpBuffer, this);
  mTxFrameBuffer.SetFrameRemovedCallback(nullptr, this);
  otPlatLog(OT_LOG_LEVEL_INFO, OT_LOG_REGION_PLATFORM, "ncp-fidl: s_ncp_raw:%lx",
            reinterpret_cast<uint64_t>(s_ncp_raw));
}

void NcpFidl::Init() {}

// TODO (jiamingw): remove after ot-lib integration is stabilized
static void printHexArr(const uint8_t *buf, uint16_t buf_len, const char *str) {
#if 0
  if (buf_len) {
    printf("ot-plat: ncp-fidl: %s [", str);
    uint32_t i = 0;
    for (; i < buf_len - 1; i++) {
      printf("0x%x, ", buf[i]);
    }
    printf("0x%x]\n", buf[i]);
  }
#endif
}

void NcpFidl::HandleFidlReceiveDone(const uint8_t *buf, uint16_t buf_len) {
  printHexArr(buf, buf_len, "rx from client");
  NcpBase::HandleReceive(buf, buf_len);
  otPlatLog(OT_LOG_LEVEL_DEBG, OT_LOG_REGION_PLATFORM,
            "ncp-fidl: new msg from client rcvd & handled");
}

void NcpFidl::HandleFidlSendDone(void) {
  otPlatLog(OT_LOG_LEVEL_DEBG, OT_LOG_REGION_PLATFORM, "ncp-fidl: HandleFidlSendDone() called");
}

void NcpFidl::HandleFrameAddedToNcpBuffer(void *aContext, Spinel::Buffer::FrameTag aTag,
                                          Spinel::Buffer::Priority aPriority,
                                          Spinel::Buffer *aBuffer) {
  OT_UNUSED_VARIABLE(aBuffer);
  OT_UNUSED_VARIABLE(aTag);
  OT_UNUSED_VARIABLE(aPriority);
  platformCallbackPostNcpFidlInboundTask(ot_instance_ptr_);
}

void NcpFidl::HandleFrameAddedToNcpBuffer(void) {
  // Send back the frame
  OT_ASSERT(!mTxFrameBuffer.IsEmpty());
  if (auto ret = mTxFrameBuffer.OutFrameBegin() != OT_ERROR_NONE) {
    otPlatLog(OT_LOG_LEVEL_CRIT, OT_LOG_REGION_PLATFORM,
              "ncp-fidl: error calling mTxFrameBuffer.OutFrameBegin()");
    return;
  }

  uint16_t frame_length = mTxFrameBuffer.OutFrameGetLength();
  uint8_t buffer[SPINEL_FRAME_MAX_SIZE];
  uint16_t read_length = mTxFrameBuffer.OutFrameRead(frame_length, buffer);
  OT_ASSERT(frame_length == read_length);

  otPlatLog(OT_LOG_LEVEL_DEBG, OT_LOG_REGION_PLATFORM,
            "ncp-fidl: prep to send to client: frame_length:%u,read_length:%u", frame_length,
            read_length);
  printHexArr(buffer, read_length, "tx to client");
  platformCallbackSendOneFrameToClient(ot_instance_ptr_, buffer, frame_length);
  otError error = mTxFrameBuffer.OutFrameRemove();
  if (error != OT_ERROR_NONE) {
    otPlatLog(OT_LOG_LEVEL_CRIT, OT_LOG_REGION_PLATFORM, "ncp-fidl: error removing out fram");
  }
}

}  // namespace Ncp
}  // namespace ot
