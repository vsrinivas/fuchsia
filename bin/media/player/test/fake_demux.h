// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/media/demux/demux.h"

namespace media {
namespace test {

class FakeDemux : public Demux {
 public:
  static std::shared_ptr<FakeDemux> Create();

  FakeDemux();

  ~FakeDemux() override {}

  const char* label() const override { return "FakeDemux"; }

  // Demux implementation.
  void Flush() override{};

  size_t stream_count() const override { return streams_.size(); }

  void RequestPacket() override {}

  void SetStatusCallback(StatusCallback callback) override {
    status_callback_ = callback;
  }

  void WhenInitialized(std::function<void(Result)> callback) override {
    callback(Result::kOk);
  }

  const std::vector<DemuxStream*>& streams() const override { return streams_; }

  void Seek(int64_t position, SeekCallback callback) override {}

 private:
  class DemuxStreamImpl : public DemuxStream {
   public:
    DemuxStreamImpl(size_t index,
                    std::unique_ptr<StreamType> stream_type,
                    TimelineRate pts_rate)
        : index_(index),
          stream_type_(std::move(stream_type)),
          pts_rate_(pts_rate) {}

    ~DemuxStreamImpl() override {}

    size_t index() const override { return index_; }

    std::unique_ptr<StreamType> stream_type() const override {
      return stream_type_->Clone();
    }

    TimelineRate pts_rate() const override { return pts_rate_; }

   private:
    size_t index_;
    std::unique_ptr<StreamType> stream_type_;
    TimelineRate pts_rate_;
  };

  StatusCallback status_callback_;
  std::vector<DemuxStream*> streams_;
  std::vector<std::unique_ptr<DemuxStreamImpl>> stream_impls_;
};

}  // namespace test
}  // namespace media
