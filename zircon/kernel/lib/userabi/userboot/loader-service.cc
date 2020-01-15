// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "loader-service.h"

#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <lib/fidl/txn_header.h>

#include <cstring>

#include <ldmsg/ldmsg.h>

#include "bootfs.h"
#include "util.h"

void LoaderService::Config(const char* string, size_t len) {
  exclusive_ = false;
  if (string[len - 1] == '!') {
    --len;
    exclusive_ = true;
  }
  if (len >= sizeof(prefix_) - 1) {
    fail(log_->get(), "loader-service config string too long");
  }
  memcpy(prefix_, string, len);
  prefix_[len++] = '/';
  prefix_len_ = len;
}

zx::vmo LoaderService::TryLoadObject(const char* name, size_t len, bool use_prefix) {
  size_t prefix_len = use_prefix ? prefix_len_ : 0;
  char file[len + sizeof(kLoadObjectFilePrefix) + prefix_len + 1];
  memcpy(file, kLoadObjectFilePrefix, sizeof(kLoadObjectFilePrefix) - 1);
  memcpy(&file[sizeof(kLoadObjectFilePrefix) - 1], prefix_, prefix_len);
  memcpy(&file[sizeof(kLoadObjectFilePrefix) - 1 + prefix_len], name, len);
  file[sizeof(kLoadObjectFilePrefix) - 1 + prefix_len + len] = '\0';
  return zx::vmo(bootfs_open(log_->get(), "shared library", fs_, root_, file));
}

zx::vmo LoaderService::LoadObject(const char* name, size_t len) {
  zx::vmo vmo = TryLoadObject(name, len, true);
  if (!vmo && prefix_len_ > 0 && !exclusive_) {
    vmo = TryLoadObject(name, len, false);
  }
  if (!vmo) {
    fail(log_->get(), "cannot find shared library '%s'", name);
  }
  return vmo;
}

bool LoaderService::HandleRequest(const zx::channel& channel) {
  ldmsg_req_t req;
  zx::vmo reqhandle;

  uint32_t size;
  uint32_t hcount;
  zx_status_t status =
      channel.read(0, &req, reqhandle.reset_and_get_address(), sizeof(req), 1, &size, &hcount);

  // This is the normal error for the other end going away,
  // which happens when the process dies.
  if (status == ZX_ERR_PEER_CLOSED) {
    printl(log_->get(), "loader-service channel peer closed on read");
    return false;
  }

  check(log_->get(), status, "zx_channel_read on loader-service channel failed");

  const char* string;
  size_t string_len;
  status = ldmsg_req_decode(&req, size, &string, &string_len);
  if (status != ZX_OK) {
    fail(log_->get(), "loader-service request invalid");
  }

  ldmsg_rsp_t rsp;
  memset(&rsp, 0, sizeof(rsp));

  zx::vmo vmo;
  switch (req.header.ordinal) {
    case LDMSG_OP_DONE_OLD:
    case LDMSG_OP_DONE:
      printl(log_->get(), "loader-service received DONE request");
      return false;

    case LDMSG_OP_CONFIG_OLD:
    case LDMSG_OP_CONFIG:
      Config(string, string_len);
      break;

    case LDMSG_OP_LOAD_OBJECT_OLD:
    case LDMSG_OP_LOAD_OBJECT:
      vmo = LoadObject(string, string_len);
      break;

    case LDMSG_OP_CLONE_OLD:
    case LDMSG_OP_CLONE:
      rsp.rv = ZX_ERR_NOT_SUPPORTED;
      goto error_reply;

    default:
      fail(log_->get(), "loader-service received invalid opcode");
      break;
  }

  rsp.rv = ZX_OK;
  rsp.object = vmo ? FIDL_HANDLE_PRESENT : FIDL_HANDLE_ABSENT;
error_reply:
  fidl_init_txn_header(&rsp.header, req.header.txid, req.header.ordinal);

  if (vmo) {
    zx_handle_t handles[] = {vmo.release()};
    status = channel.write(0, &rsp, static_cast<uint32_t>(ldmsg_rsp_get_size(&rsp)), handles, 1);
  } else {
    status = channel.write(0, &rsp, static_cast<uint32_t>(ldmsg_rsp_get_size(&rsp)), nullptr, 0);
  }
  check(log_->get(), status, "zx_channel_write on loader-service channel failed");

  return true;
}

void LoaderService::Serve(zx::channel channel) {
  printl(log_->get(), "waiting for loader-service requests...");
  do {
    zx_signals_t signals;
    zx_status_t status = channel.wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                          zx::time::infinite(), &signals);
    if (status == ZX_ERR_BAD_STATE) {
      // This is the normal error for the other end going away,
      // which happens when the process dies.
      break;
    }
    check(log_->get(), status, "zx_object_wait_one failed on loader-service channel");
    if (signals & ZX_CHANNEL_PEER_CLOSED) {
      printl(log_->get(), "loader-service channel peer closed");
      break;
    }
    if (!(signals & ZX_CHANNEL_READABLE)) {
      fail(log_->get(), "unexpected signal state on loader-service channel");
    }
  } while (HandleRequest(channel));
}
