// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/async-socket.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/socket.h>
#include <zircon/status.h>

namespace fuzzing {
namespace {

struct TransferParams {
  ExecutorPtr executor;
  const char* label;
  zx::socket socket;
  Input input;
  zx_signals_t ready;
  zx_signals_t done;
};

template <typename Transfer>
ZxPromise<Input> AsyncSocketTransfer(TransferParams&& params, Transfer transfer) {
  FX_DCHECK(params.executor);
  if (params.input.size() == 0) {
    return fpromise::make_promise([input = std::move(params.input)]() mutable -> ZxResult<Input> {
      return fpromise::ok(std::move(input));
    });
  }
  return fpromise::make_promise([executor = params.executor, label = params.label,
                                 socket = std::move(params.socket), input = std::move(params.input),
                                 ready = params.ready, done = params.done,
                                 transfer = std::move(transfer), offset = size_t(0),
                                 awaiting =
                                     ZxFuture<>()](Context& context) mutable -> ZxResult<Input> {
    while (true) {
      size_t actual = 0;
      auto status = transfer(socket, input.data() + offset, input.size() - offset, &actual);
      if (status == ZX_OK) {
        offset += actual;
        FX_DCHECK(offset <= input.size());
      } else if (status != ZX_ERR_SHOULD_WAIT) {
        FX_LOGS(WARNING) << "Failed to " << label << " socket: " << zx_status_get_string(status);
        return fpromise::error(status);
      }
      if (offset == input.size()) {
        return fpromise::ok(std::move(input));
      }
      if (!awaiting) {
        awaiting = executor->MakePromiseWaitHandle(zx::unowned_handle(socket.get()), ready | done)
                       .and_then([ready](const zx_packet_signal_t& packet) -> ZxResult<> {
                         if (packet.observed & ready) {
                           return fpromise::ok();
                         }
                         return fpromise::error(ZX_ERR_PEER_CLOSED);
                       });
      }
      if (!awaiting(context)) {
        return fpromise::pending();
      }
      if (awaiting.is_error()) {
        auto status = awaiting.error();
        FX_LOGS(WARNING) << "Failed to " << label << " socket: " << zx_status_get_string(status);
        return fpromise::error(status);
      }
      awaiting = nullptr;
    }
  });
}

}  // namespace

ZxPromise<Input> AsyncSocketRead(const ExecutorPtr& executor, FidlInput&& fidl_input) {
  TransferParams params = {
      .executor = executor,
      .label = "read from",
      .socket = std::move(fidl_input.socket),
      .input = Input(fidl_input.size),
      .ready = ZX_SOCKET_READABLE,
      .done = ZX_SOCKET_PEER_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED,
  };
  return AsyncSocketTransfer(
      std::move(params), [](const zx::socket& socket, uint8_t* buf, size_t len, size_t* actual) {
        return socket.read(0, buf, len, actual);
      });
}

ZxPromise<Artifact> AsyncSocketRead(const ExecutorPtr& executor, FidlArtifact&& fidl_artifact) {
  FuzzResult fuzz_result;
  FidlInput fidl_input;
  std::tie(fuzz_result, fidl_input) = std::move(fidl_artifact);
  return AsyncSocketRead(executor, std::move(fidl_input)).and_then([fuzz_result](Input& input) {
    return fpromise::ok(Artifact(fuzz_result, std::move(input)));
  });
}

FidlInput AsyncSocketWrite(const ExecutorPtr& executor, Input&& input) {
  FidlInput fidl_input;
  fidl_input.size = input.size();
  zx::socket socket;
  auto status = zx::socket::create(ZX_SOCKET_STREAM, &socket, &fidl_input.socket);
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
  fidl_input.socket.set_disposition(ZX_SOCKET_DISPOSITION_WRITE_DISABLED, 0);
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
  TransferParams params = {
      .executor = executor,
      .label = "write to",
      .socket = std::move(socket),
      .input = std::move(input),
      .ready = ZX_SOCKET_WRITABLE,
      .done = ZX_SOCKET_PEER_CLOSED,
  };
  auto task = AsyncSocketTransfer(std::move(params), [](const zx::socket& socket, uint8_t* buf,
                                                        size_t len, size_t* actual) {
                return socket.write(0, buf, len, actual);
              }).and_then([](Input& input) -> ZxResult<> { return fpromise::ok(); });
  executor->schedule_task(std::move(task));
  return fidl_input;
}

FidlArtifact AsyncSocketWrite(const ExecutorPtr& executor, Artifact&& artifact) {
  auto fidl_input = AsyncSocketWrite(executor, artifact.take_input());
  return MakeFidlArtifact(artifact.fuzz_result(), std::move(fidl_input));
}

}  // namespace fuzzing
