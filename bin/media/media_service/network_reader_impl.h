// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "lib/media/fidl/seeking_reader.fidl.h"
#include "garnet/bin/media/media_service/media_service_impl.h"
#include "garnet/bin/media/util/incident.h"
#include "lib/network/fidl/url_loader.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace media {

// Fidl agent that reads from an HTTP service.
class NetworkReaderImpl : public MediaServiceImpl::Product<SeekingReader>,
                          public SeekingReader {
 public:
  static std::shared_ptr<NetworkReaderImpl> Create(
      const fidl::String& url,
      fidl::InterfaceRequest<SeekingReader> request,
      MediaServiceImpl* owner);

  ~NetworkReaderImpl() override;

  // SeekingReader implementation.
  void Describe(const DescribeCallback& callback) override;

  void ReadAt(uint64_t position, const ReadAtCallback& callback) override;

 private:
  static const char* kContentLengthHeaderName;
  static const char* kAcceptRangesHeaderName;
  static const char* kAcceptRangesHeaderBytesValue;
  static const char* kRangeHeaderName;
  static constexpr uint32_t kStatusOk = 200u;
  static constexpr uint32_t kStatusPartialContent = 206u;
  static constexpr uint32_t kStatusNotFound = 404u;

  NetworkReaderImpl(const fidl::String& url,
                    fidl::InterfaceRequest<SeekingReader> request,
                    MediaServiceImpl* owner);

  std::string url_;
  network::URLLoaderPtr url_loader_;
  MediaResult result_ = MediaResult::OK;
  uint64_t size_ = kUnknownSize;
  bool can_seek_ = false;
  Incident ready_;
};

}  // namespace media
