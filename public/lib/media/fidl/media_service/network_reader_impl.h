// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_SERVICES_FACTORY_NETWORK_READER_IMPL_H_
#define APPS_MEDIA_SERVICES_FACTORY_NETWORK_READER_IMPL_H_

#include <memory>

#include "apps/media/interfaces/seeking_reader.mojom.h"
#include "apps/media/services/media_service/media_service_impl.h"
#include "apps/media/services/common/incident.h"
#include "mojo/public/cpp/bindings/binding.h"

namespace mojo {
namespace media {

// Mojo agent that decodes a stream.
class NetworkReaderImpl : public MediaServiceImpl::Product<SeekingReader>,
                          public SeekingReader {
 public:
  static std::shared_ptr<NetworkReaderImpl> Create(
      const String& url,
      InterfaceRequest<SeekingReader> request,
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

  // Calls ReadResponseBody.
  static void ReadResponseBodyStatic(void* self, MojoResult result);

  NetworkReaderImpl(const String& url,
                    InterfaceRequest<SeekingReader> request,
                    MediaServiceImpl* owner);

  std::string url_;
  URLLoaderPtr url_loader_;
  MediaResult result_ = MediaResult::OK;
  uint64_t size_ = kUnknownSize;
  bool can_seek_ = false;
  Incident ready_;
};

}  // namespace media
}  // namespace mojo

#endif  // APPS_MEDIA_SERVICES_FACTORY_NETWORK_READER_IMPL_H_
