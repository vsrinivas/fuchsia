// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_DEMUX_DEMUX_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_DEMUX_DEMUX_H_

#include <lib/fit/function.h>

#include <memory>
#include <vector>

#include "lib/component/cpp/startup_context.h"
#include "src/media/playback/mediaplayer/demux/reader_cache.h"
#include "src/media/playback/mediaplayer/graph/metadata.h"
#include "src/media/playback/mediaplayer/graph/nodes/node.h"
#include "src/media/playback/mediaplayer/graph/packet.h"
#include "src/media/playback/mediaplayer/graph/result.h"
#include "src/media/playback/mediaplayer/graph/types/stream_type.h"

namespace media_player {

// Abstract base class for sources that parse input from a reader and
// produce one or more output streams.
class Demux : public Node {
 public:
  using SeekCallback = ::fit::closure;
  using StatusCallback = ::fit::function<void(
      int64_t duration_ns, bool can_seek, const Metadata& metadata,
      const std::string& problem_type, const std::string& problem_details)>;

  // Represents a stream produced by the demux.
  class DemuxStream {
   public:
    virtual ~DemuxStream() {}

    virtual size_t index() const = 0;

    virtual std::unique_ptr<StreamType> stream_type() const = 0;

    virtual media::TimelineRate pts_rate() const = 0;
  };

  ~Demux() override {}

  // Sets a callback to call when metadata or problem changes occur.
  virtual void SetStatusCallback(StatusCallback callback) = 0;

  // Sets the lead duration ahead of playback and the retained duration behind
  // playback to optimize skipping back.
  virtual void SetCacheOptions(zx_duration_t lead, zx_duration_t backtrack) = 0;

  // Calls the callback when the initial streams and metadata have
  // established.
  virtual void WhenInitialized(fit::function<void(zx_status_t)> callback) = 0;

  // Gets the stream collection. This method should not be called until the
  // WhenInitialized callback has been called.
  virtual const std::vector<std::unique_ptr<DemuxStream>>& streams() const = 0;

  // Seeks to the specified position and calls the callback. THE CALLBACK MAY
  // BE CALLED ON AN ARBITRARY THREAD.
  virtual void Seek(int64_t position, SeekCallback callback) = 0;
};

// Abstract base class for |Demux| factories.
class DemuxFactory {
 public:
  // Creates a demux factory.
  static std::unique_ptr<DemuxFactory> Create(
      component::StartupContext* startup_context);

  virtual ~DemuxFactory() {}

  // Creates a |Demux| object for a given reader.
  virtual Result CreateDemux(std::shared_ptr<ReaderCache> reader_cache,
                             std::shared_ptr<Demux>* demux_out) = 0;

 protected:
  DemuxFactory() {}

 private:
  // Disallow copy and assign.
  DemuxFactory(const DemuxFactory&) = delete;
  DemuxFactory& operator=(const DemuxFactory&) = delete;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_DEMUX_DEMUX_H_
