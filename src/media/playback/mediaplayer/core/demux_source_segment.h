// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_CORE_DEMUX_SOURCE_SEGMENT_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_CORE_DEMUX_SOURCE_SEGMENT_H_

#include <memory>

#include "src/media/playback/mediaplayer/core/source_segment.h"
#include "src/media/playback/mediaplayer/demux/demux.h"
#include "src/media/playback/mediaplayer/util/incident.h"

namespace media_player {

// A source segment employing a demux.
class DemuxSourceSegment : public SourceSegment {
 public:
  static std::unique_ptr<DemuxSourceSegment> Create(
      std::shared_ptr<Demux> demux);

  DemuxSourceSegment(std::shared_ptr<Demux> demux);

  ~DemuxSourceSegment() override;

 protected:
  // SourceSegment overrides.
  void DidProvision() override;

  void WillDeprovision() override;

  int64_t duration_ns() const override { return duration_ns_; };

  bool can_pause() const override { return true; }

  bool can_seek() const override { return can_seek_; }

  const Metadata* metadata() const override { return metadata_.get(); }

  void Flush(bool hold_frame, fit::closure callback) override;

  void Seek(int64_t position, fit::closure callback) override;

  NodeRef source_node() const override { return demux_node_; }

 private:
  // Builds the graph.
  void BuildGraph();

  std::shared_ptr<Demux> demux_;
  NodeRef demux_node_;
  int64_t duration_ns_ = 0;
  bool can_seek_ = false;
  std::unique_ptr<Metadata> metadata_;
  Incident demux_initialized_;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_CORE_DEMUX_SOURCE_SEGMENT_H_
