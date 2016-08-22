// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_MEDIA_FRAMEWORK_PARTS_DEMUX_H_
#define SERVICES_MEDIA_FRAMEWORK_PARTS_DEMUX_H_

#include <memory>
#include <vector>

#include "services/media/framework/metadata.h"
#include "services/media/framework/models/active_multistream_source.h"
#include "services/media/framework/packet.h"
#include "services/media/framework/parts/reader.h"
#include "services/media/framework/result.h"
#include "services/media/framework/types/stream_type.h"

namespace mojo {
namespace media {

// Abstract base class for sources that parse input from a reader and
// produce one or more output streams.
class Demux : public ActiveMultistreamSource {
 public:
  using SeekCallback = std::function<void()>;
  using StatusCallback =
      std::function<void(const std::unique_ptr<Metadata>& metadata,
                         const std::string& problem_type,
                         const std::string& problem_details)>;

  // Represents a stream produced by the demux.
  class DemuxStream {
    // TODO(dalesat): Replace this class with stream_type_, unless more stuff
    // needs to be added.
   public:
    virtual ~DemuxStream() {}

    virtual size_t index() const = 0;

    virtual std::unique_ptr<StreamType> stream_type() const = 0;
  };

  // Creates a Demux object for a given reader.
  static std::shared_ptr<Demux> Create(std::shared_ptr<Reader> reader);

  ~Demux() override {}

  // Sets a callback to call when metadata or problem changes occur.
  virtual void SetStatusCallback(const StatusCallback& callback) = 0;

  // Calls the callback when the initial streams and metadata have
  // established. THE CALLBACK MAY BE CALLED ON AN ARBITRARY THREAD.
  virtual void WhenInitialized(std::function<void(Result)> callback) = 0;

  // Gets the stream collection. This method should not be called until the
  // WhenInitialized callback has been called.
  virtual const std::vector<DemuxStream*>& streams() const = 0;

  // Seeks to the specified position and calls the callback. THE CALLBACK MAY
  // BE CALLED ON AN ARBITRARY THREAD.
  virtual void Seek(int64_t position, const SeekCallback& callback) = 0;
};

}  // namespace media
}  // namespace mojo

#endif  // SERVICES_MEDIA_FRAMEWORK_PARTS_DEMUX_H_
