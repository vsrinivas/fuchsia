// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_CORE_ELEMENTARY_SOURCE_SEGMENT_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_CORE_ELEMENTARY_SOURCE_SEGMENT_H_

#include <memory>
#include <vector>

#include "src/media/playback/mediaplayer/core/source_segment.h"
#include "src/media/playback/mediaplayer/graph/nodes/node.h"
#include "src/media/playback/mediaplayer/graph/types/stream_type.h"

namespace media_player {

// A source segment employing a demux.
class ElementarySourceSegment : public SourceSegment {
 public:
  static std::unique_ptr<ElementarySourceSegment> Create(
      int64_t duration_ns, bool can_pause, bool can_seek,
      std::unique_ptr<media_player::Metadata> metadata);

  ElementarySourceSegment(int64_t duration_ns, bool can_pause, bool can_seek,
                          std::unique_ptr<media_player::Metadata> metadata);

  ~ElementarySourceSegment() override;

  // Adds a stream to this source segment.
  void AddStream(std::shared_ptr<Node> node, const StreamType& output_stream_type);

  // SourceSegment overrides.
  std::vector<NodeRef> source_nodes() const override { return nodes_; }

  void Flush(bool hold_frame, fit::closure callback) override;

 protected:
  // SourceSegment overrides.
  void DidProvision() override;

  void WillDeprovision() override;

  int64_t duration_ns() const override { return duration_ns_; };

  bool can_pause() const override { return can_pause_; }

  bool can_seek() const override { return can_seek_; }

  const Metadata* metadata() const override { return metadata_.get(); }

  void Seek(int64_t position, fit::closure callback) override;

 private:
  std::vector<NodeRef> nodes_;
  int64_t duration_ns_;
  bool can_pause_;
  bool can_seek_;
  std::unique_ptr<Metadata> metadata_;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_CORE_ELEMENTARY_SOURCE_SEGMENT_H_
