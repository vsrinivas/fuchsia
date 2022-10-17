// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/chunked-compression/multithreaded-chunked-compressor.h"

#include <lib/stdcompat/span.h>
#include <lib/zx/status.h>
#include <string.h>
#include <zircon/errors.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <zstd/zstd.h>

#include "src/lib/chunked-compression/chunked-archive.h"
#include "src/lib/chunked-compression/compression-params.h"
#include "src/lib/chunked-compression/status.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"

namespace chunked_compression {
namespace {

size_t CalculateLastFrameSize(size_t frame_size, size_t data_size) {
  size_t remainder = data_size % frame_size;
  return remainder == 0 ? frame_size : remainder;
}

// Multi-producer multi-consumer task queue.
template <typename T>
class TaskQueue {
 public:
  // Terminates the queue and signals to all threads waiting in |TakeTask| that the queue has been
  // stopped. Any tasks added to the queue after it has been terminated won't be handled.
  void Terminate() {
    std::scoped_lock lock(mutex_);
    terminated_ = true;
    condition_.notify_all();
  }

  void AddTask(T value) {
    std::scoped_lock lock(mutex_);
    queue_.push_back(value);
    condition_.notify_one();
  }

  // Returns the next task in the queue. If there are no tasks in the queue then this method wait
  // for a task to be added. Returns |std::nullopt| if the |TaskQueue| has been terminated.
  //
  // Thread-safety analysis doesn't work with unique_lock.
  std::optional<T> TakeTask() FXL_NO_THREAD_SAFETY_ANALYSIS {
    std::unique_lock lock(mutex_);
    for (;;) {
      if (terminated_) {
        return std::nullopt;
      }
      if (!queue_.empty()) {
        auto task = std::move(queue_.front());
        queue_.pop_front();
        return task;
      }
      condition_.wait(lock);
    }
  }

 private:
  std::mutex mutex_;
  std::condition_variable condition_;
  bool terminated_ FXL_GUARDED_BY(mutex_) = false;
  std::deque<T> queue_ FXL_GUARDED_BY(mutex_);
};

struct CompressFrameResponse {
  zx::result<std::vector<uint8_t>> compressed_data;
  size_t frame_id;
};

struct CompressFrameRequest {
  cpp20::span<const uint8_t> data;
  size_t frame_id;
  const CompressionParams* params;
  TaskQueue<CompressFrameResponse>* response_queue;
};

zx::result<std::vector<uint8_t>> CompressFrame(const CompressionParams& params,
                                               cpp20::span<const uint8_t> data, ZSTD_CCtx* ctx) {
  if (ZSTD_isError(
          ZSTD_CCtx_setParameter(ctx, ZSTD_c_compressionLevel, params.compression_level))) {
    return zx::error(ZX_ERR_INTERNAL);
  }
  if (params.frame_checksum) {
    if (ZSTD_isError(ZSTD_CCtx_setParameter(ctx, ZSTD_c_checksumFlag, 1))) {
      return zx::error(ZX_ERR_INTERNAL);
    }
  }
  std::vector<uint8_t> output(ZSTD_compressBound(data.size()));
  size_t compressed_size =
      ZSTD_compress2(ctx, output.data(), output.size(), data.data(), data.size());
  if (ZSTD_isError(compressed_size)) {
    return zx::error(ZX_ERR_INTERNAL);
  }
  output.resize(compressed_size);
  return zx::ok(std::move(output));
}

void StartWorker(TaskQueue<CompressFrameRequest>* queue) {
  std::unique_ptr<ZSTD_CCtx, decltype(&ZSTD_freeCCtx)> ctx(ZSTD_createCCtx(), ZSTD_freeCCtx);
  for (;;) {
    auto request = queue->TakeTask();
    if (!request.has_value()) {
      // TaskQueue terminated, stop the worker.
      return;
    }
    request->response_queue->AddTask({
        .compressed_data = CompressFrame(*request->params, request->data, ctx.get()),
        .frame_id = request->frame_id,
    });
    ZSTD_CCtx_reset(ctx.get(), ZSTD_reset_session_and_parameters);
  }
}

}  // namespace

class MultithreadedChunkedCompressor::MultithreadedChunkedCompressorImpl {
 public:
  explicit MultithreadedChunkedCompressorImpl(size_t thread_count) {
    for (size_t i = 0; i < thread_count; ++i) {
      worker_threads_.emplace_back([this]() { StartWorker(&this->work_queue_); });
    }
  }

  ~MultithreadedChunkedCompressorImpl() {
    work_queue_.Terminate();
    for (auto& thread : worker_threads_) {
      thread.join();
    }
  }

  zx::result<std::vector<uint8_t>> Compress(const CompressionParams& params,
                                            cpp20::span<const uint8_t> input) {
    if (!params.IsValid()) {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    if (input.empty()) {
      return zx::ok(std::vector<uint8_t>());
    }
    TaskQueue<CompressFrameResponse> compression_responses;
    size_t frame_count = HeaderWriter::NumFramesForDataSize(input.size(), params.chunk_size);
    size_t last_frame_size = CalculateLastFrameSize(params.chunk_size, input.size());
    for (size_t frame = 0; frame < frame_count; ++frame) {
      auto frame_data =
          input.subspan(frame * params.chunk_size,
                        frame + 1 == frame_count ? last_frame_size : params.chunk_size);
      work_queue_.AddTask({
          .data = frame_data,
          .frame_id = frame,
          .params = &params,
          .response_queue = &compression_responses,
      });
    }

    std::vector<std::vector<uint8_t>> frames(frame_count);
    size_t compressed_data_size = 0;
    for (size_t frame = 0; frame < frame_count; ++frame) {
      auto response = compression_responses.TakeTask();
      if (!response.has_value()) {
        // Nothing should terminate the response queue.
        return zx::error(ZX_ERR_INTERNAL);
      }
      if (response->compressed_data.is_error()) {
        return response->compressed_data.take_error();
      }
      compressed_data_size += response->compressed_data->size();
      frames[response->frame_id] = *std::move(response->compressed_data);
    }

    size_t metadata_size = HeaderWriter::MetadataSizeForNumFrames(frame_count);
    std::vector<uint8_t> output(metadata_size + compressed_data_size);
    HeaderWriter header_writer;
    if (Status status =
            HeaderWriter::Create(output.data(), metadata_size, frame_count, &header_writer);
        status != kStatusOk) {
      return zx::error(status);
    }

    size_t compressed_offset = metadata_size;
    for (size_t frame = 0; frame < frame_count; ++frame) {
      std::vector<uint8_t>& compressed_frame = frames[frame];
      SeekTableEntry entry{
          .decompressed_offset = frame * params.chunk_size,
          .decompressed_size = frame + 1 == frame_count ? last_frame_size : params.chunk_size,
          .compressed_offset = compressed_offset,
          .compressed_size = compressed_frame.size(),
      };
      header_writer.AddEntry(entry);
      memcpy(output.data() + compressed_offset, compressed_frame.data(), compressed_frame.size());
      compressed_offset += compressed_frame.size();
    }

    if (Status status = header_writer.Finalize(); status != kStatusOk) {
      return zx::error(status);
    }
    return zx::ok(std::move(output));
  }

 private:
  TaskQueue<CompressFrameRequest> work_queue_;
  std::vector<std::thread> worker_threads_;
};

MultithreadedChunkedCompressor::MultithreadedChunkedCompressor(size_t thread_count)
    : impl_(std::make_unique<MultithreadedChunkedCompressor::MultithreadedChunkedCompressorImpl>(
          thread_count)) {}

MultithreadedChunkedCompressor::~MultithreadedChunkedCompressor() = default;

zx::result<std::vector<uint8_t>> MultithreadedChunkedCompressor::Compress(
    const CompressionParams& params, cpp20::span<const uint8_t> input) {
  return impl_->Compress(params, input);
}

}  // namespace chunked_compression
