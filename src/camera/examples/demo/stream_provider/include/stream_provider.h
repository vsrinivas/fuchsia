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

// The StreamProvider class allows the caller to connect to a fuchsia::camera2::Stream client
// endpoint from a variety of sources.
class StreamProvider {
 public:
  enum class Source {
    ISP,
    CONTROLLER,
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

  // Creates a Stream instance, routing events to the given event_handler.
  // This method reflects the corresponding API in CameraManager, however here the caller is not
  // able to participate in buffer format negotiation. The caller must ensure that event_handler
  // remain valid for the lifetime of the returned Stream.
  // Args:
  //   |event_handler|: a caller-provided implementation of Stream event handlers
  //   |format_out|: output parameter that describes the format of the created stream
  //   |buffers_out|: output parameter that describes the buffers backing the created stream
  //   |should_rotate_out|: output parameter that indicates whether the stream should be rotated in
  //                        order to appear correct
  // Returns:
  //   A Stream instance, or nullptr on error.
  virtual std::unique_ptr<fuchsia::camera2::Stream> ConnectToStream(
      fuchsia::camera2::Stream_EventSender* event_handler,
      fuchsia::sysmem::ImageFormat_2* format_out,
      fuchsia::sysmem::BufferCollectionInfo_2* buffers_out, bool* should_rotate_out) = 0;
};

#endif  // SRC_CAMERA_EXAMPLES_DEMO_STREAM_PROVIDER_INCLUDE_STREAM_PROVIDER_H_
