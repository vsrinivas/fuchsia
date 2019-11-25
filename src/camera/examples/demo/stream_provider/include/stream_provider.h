// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_EXAMPLES_DEMO_STREAM_PROVIDER_INCLUDE_STREAM_PROVIDER_H_
#define SRC_CAMERA_EXAMPLES_DEMO_STREAM_PROVIDER_INCLUDE_STREAM_PROVIDER_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/fit/result.h>
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
  //   |index|: the provider-dependent stream index to request
  // On success, returns a tuple containing the following values:
  //   [0]: the format of the created stream
  //   [1]: the buffers backing the created stream
  //   [2]: true iff the consumer should rotate the stream in order to appear correct
  // On failure, returns ZX_ERR_OUT_OF_RANGE if the specified stream index is not supported by this
  // provider, otherwise a propagated error.
  virtual fit::result<
      std::tuple<fuchsia::sysmem::ImageFormat_2, fuchsia::sysmem::BufferCollectionInfo_2, bool>,
      zx_status_t>
  ConnectToStream(fidl::InterfaceRequest<fuchsia::camera2::Stream> request, uint32_t index = 0) = 0;
};

#endif  // SRC_CAMERA_EXAMPLES_DEMO_STREAM_PROVIDER_INCLUDE_STREAM_PROVIDER_H_
