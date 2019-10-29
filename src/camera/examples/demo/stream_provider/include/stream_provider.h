// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_EXAMPLES_DEMO_STREAM_PROVIDER_INCLUDE_STREAM_PROVIDER_H_
#define SRC_CAMERA_EXAMPLES_DEMO_STREAM_PROVIDER_INCLUDE_STREAM_PROVIDER_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <zircon/types.h>

#include <memory>
#include <string>
#include <tuple>

// The StreamProvider class allows the caller to connect to a fuchsia::camera2::Stream client
// endpoint from a variety of sources.
class StreamProvider {
 public:
  enum class Source {
    ISP,
    CONTROLLER,
    MANAGER,
    NUM_SOURCES,
  };
  virtual ~StreamProvider() = default;

  // Creates a provider from the given source
  // Args:
  //   |source|: the desired source of the stream
  // Returns:
  //   A StreamProvider instance, or nullptr on error
  static std::unique_ptr<StreamProvider> Create(Source source);

  // Gets the friendly name of stream source specified during creation of the provider
  // Returns:
  //   The friendly name of the source.
  virtual std::string GetName() = 0;

  // Creates a Stream instance.
  // This method reflects the corresponding API in CameraManager, however here the caller is not
  // able to participate in buffer format negotiation.
  // Args:
  //   |request|: a request for a Stream interface
  // Returns:
  //   A tuple containing the following values:
  //   [0]: ZX_OK if connection succeeded, otherwise an error code
  //   [1]: the format of the created stream
  //   [2]: the buffers backing the created stream
  //   [3]: true iff the consumer should rotate the stream in order to appear correct
  virtual std::tuple<zx_status_t, fuchsia::sysmem::ImageFormat_2,
                     fuchsia::sysmem::BufferCollectionInfo_2, bool>
  ConnectToStream(fidl::InterfaceRequest<fuchsia::camera2::Stream> request) = 0;

 protected:
  static std::tuple<zx_status_t, fuchsia::sysmem::ImageFormat_2,
                    fuchsia::sysmem::BufferCollectionInfo_2, bool>
  MakeErrorReturn(zx_status_t status) {
    fuchsia::sysmem::ImageFormat_2 format{};
    fuchsia::sysmem::BufferCollectionInfo_2 buffers{};
    return {status, std::move(format), std::move(buffers), false};
  }
};

#endif  // SRC_CAMERA_EXAMPLES_DEMO_STREAM_PROVIDER_INCLUDE_STREAM_PROVIDER_H_
