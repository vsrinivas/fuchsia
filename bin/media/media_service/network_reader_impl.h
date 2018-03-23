// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "garnet/bin/media/media_service/media_component_factory.h"
#include "garnet/bin/media/util/incident.h"
#include "lib/fidl/cpp/binding.h"
#include <fuchsia/cpp/media.h>
#include <fuchsia/cpp/network.h>

namespace media {

// Fidl agent that reads from an HTTP service.
class NetworkReaderImpl : public MediaComponentFactory::Product<SeekingReader>,
                          public SeekingReader {
 public:
  static std::shared_ptr<NetworkReaderImpl> Create(
      fidl::StringPtr url,
      fidl::InterfaceRequest<SeekingReader> request,
      MediaComponentFactory* owner);

  ~NetworkReaderImpl() override;

  // SeekingReader implementation.
  void Describe(DescribeCallback callback) override;

  void ReadAt(uint64_t position, ReadAtCallback callback) override;

 private:
  static const char* kContentLengthHeaderName;
  static const char* kAcceptRangesHeaderName;
  static const char* kAcceptRangesHeaderBytesValue;
  static const char* kRangeHeaderName;
  static constexpr uint32_t kStatusOk = 200u;
  static constexpr uint32_t kStatusPartialContent = 206u;
  static constexpr uint32_t kStatusNotFound = 404u;

  NetworkReaderImpl(fidl::StringPtr url,
                    fidl::InterfaceRequest<SeekingReader> request,
                    MediaComponentFactory* owner);

  std::string url_;
  network::URLLoaderPtr url_loader_;
  MediaResult result_ = MediaResult::OK;
  uint64_t size_ = kUnknownSize;
  bool can_seek_ = false;
  Incident ready_;
};

}  // namespace media
