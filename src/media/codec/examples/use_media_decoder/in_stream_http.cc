// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "in_stream_http.h"

#include <lib/media/test/one_shot_event.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <queue>
#include <tuple>

#include "util.h"

InStreamHttp::InStreamHttp(async::Loop* fidl_loop, thrd_t fidl_thread,
                           sys::ComponentContext* component_context, std::string url)
    : InStream(fidl_loop, fidl_thread, component_context), url_(url) {
  ZX_DEBUG_ASSERT(thrd_current() != fidl_thread_);
  ZX_DEBUG_ASSERT(!url_.empty());

  // We're not runnign on the fidl_thread_, so we need to post over to the
  // fidl_thread_ for any binding, sending, etc.
  http_loader_.set_error_handler(
      [](zx_status_t status) { Exit("http_loader_ failed - status: %lu", status); });
  component_context->svc()->Connect(http_loader_.NewRequest(fidl_dispatcher_));

  ResetToStartInternal(zx::deadline_after(zx::sec(30)));
}

InStreamHttp::~InStreamHttp() {
  ZX_DEBUG_ASSERT(thrd_current() != fidl_thread_);

  // By fencing anything we've previously posted to fidl_thread, we avoid
  // touching "this" too late.
  PostToFidlSerial([this] { http_loader_.Unbind(); });

  // After this call completes, we know the above post has run on fidl_thread_,
  // so no more code re. this instance will be running on fidl_thread_ (partly
  // because we Unbind()/reset() in the lambda above, and partly because we
  // never re-post from fidl_thread_).
  FencePostToFidlSerial();
}

zx_status_t InStreamHttp::ReadBytesInternal(uint32_t max_bytes_to_read, uint32_t* bytes_read_out,
                                            uint8_t* buffer_out, zx::time just_fail_deadline) {
  if (eos_position_known_ && cursor_position_ == eos_position_) {
    // Not possible to read more because there isn't any more.  Not a failure.
    *bytes_read_out = 0;
    return ZX_OK;
  }

  zx_signals_t pending{};
  zx_status_t status =
      socket_.wait_one(ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED, just_fail_deadline, &pending);
  if (status != ZX_OK) {
    Exit("socket_ wait failed - status: %d", status);
  }

  if (pending & ZX_SOCKET_READABLE) {
    size_t len = max_bytes_to_read;
    size_t actual;
    status = socket_.read(0, static_cast<void*>(buffer_out), len, &actual);
    if (status != ZX_OK) {
      Exit("socket_.read() failed - status: %d", status);
    }
    *bytes_read_out = actual;
    return ZX_OK;
  } else if (pending & ZX_SOCKET_PEER_CLOSED) {
    // Only handle this after ZX_SOCKET_READABLE, because we must assume this
    // means EOS and we don't want to miss any data that was sent before EOS.
    //
    // If both READABLE and PEER_CLOSED are set, we have to assume that more may
    // be readable, so we intentionally only handle PEER_CLOSED when PEER_CLOSED
    // && !READABLE.
    *bytes_read_out = 0;
    // InStream::ReadBytesShort() takes care of seting eos_position_known_ on
    // return from this method, so we don't need to do that here.
    return ZX_OK;
  } else {
    Exit("socket_ wait returned success but neither signal set?");
  }
  FX_NOTREACHED();
  return ZX_ERR_INTERNAL;
}

zx_status_t InStreamHttp::ResetToStartInternal(zx::time just_fail_deadline) {
  fuchsia::net::http::Request http_request{};
  // url_ is already UTF-8
  http_request.set_url(url_);

  fuchsia::net::http::Response http_response{};
  OneShotEvent have_response_event;
  http_loader_->Fetch(std::move(http_request), [&http_response, &have_response_event](
                                                   fuchsia::net::http::Response response_param) {
    http_response = std::move(response_param);
    have_response_event.Signal();
  });
  have_response_event.Wait(zx::deadline_after(zx::sec(30)));

  if (http_response.has_error()) {
    fprintf(stderr, "*response.error: %d\n", http_response.error());
  }

  // test only
  ZX_ASSERT_MSG(!http_response.has_error(), "http response has error");
  ZX_ASSERT_MSG(http_response.has_body(), "http response missing body");

  if (http_response.has_headers()) {
    for (auto& header : http_response.headers()) {
      // TODO(dustingreen): deal with chunked encoding, or switch to a new http
      // client impl that deals with de-chunking before we see the data. For now
      // we rely on the http server to not generate chunked encoding.
      ZX_ASSERT(!(std::string(header.name.begin(), header.name.end()) == "transfer-encoding" &&
                  std::string(header.value.begin(), header.value.end()) == "chunked"));
    }
  }

  socket_ = std::move(*http_response.mutable_body());
  cursor_position_ = 0;
  failure_seen_ = false;
  eos_position_known_ = false;
  eos_position_ = 0;

  return ZX_OK;
}
