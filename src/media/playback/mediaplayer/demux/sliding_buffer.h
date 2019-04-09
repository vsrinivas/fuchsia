// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_DEMUX_SLIDING_BUFFER_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_DEMUX_SLIDING_BUFFER_H_

#include <lib/fit/function.h>

#include <map>
#include <optional>
#include <vector>

namespace media_player {

// |SlidingBuffer| is a ring buffer of fixed size that emulates an infinite
// index space by revolving its ring buffer to accomodate the most recent write.
// It is designed for use in caching a mostly linear progression through a data
// stream.
//
// The relationship between a virtual index |vi| and the real index |ri| in the
// buffer that backs it is |ri = vi % capacity|.
//
// When consuming from this buffer, call |Read()| to try and read a range. If
// all desired bytes are not read, call |Slide()| to slide the buffer up to the
// end of the read. |Slide()| will return the writes that must be made to the
// buffer to accomodate the |Read()|.
class SlidingBuffer {
 public:
  struct Block {
    // Position in the virtual index space where the block starts.
    size_t start = 0;
    size_t size = 0;
    // Raw access to the portion of the ring buffer that emulates
    // |start|.
    uint8_t* buffer = nullptr;
  };

  SlidingBuffer(size_t capacity);
  ~SlidingBuffer();

  // Reads from the virtual position |pos| into |buffer| up to |bytes_to_read|.
  size_t Read(size_t pos, uint8_t* buffer, size_t bytes_to_read);

  // Slides the buffer so it will accomodate the virtual position |dest_pos|
  // and |budget| bytes after it. It will return a set of Blocks that must
  // be filled with the contents of the upstream data source in order to
  // complete the slide.
  //
  // The actual range of available bytes may be larger than
  // |[dest_pos, dest_pos + budget)| if the desired range overlaps with bytes
  // the buffer already holds, but it reads summing more than |budget| bytes
  // will never be requested.
  std::vector<Block> Slide(size_t dest_pos, size_t budget);

  // Returns the virtual position at which a |Read()| starting at |pos| would
  // necessarily terminate because a bytes is missing.
  size_t NextMissingByte(size_t pos) {
    return filled_range_.contains(pos) ? filled_range_.end() : pos;
  }

  size_t capacity() { return store_.size(); }

 private:
  struct Range {
    size_t start = 0;
    size_t length = 0;
    size_t end() const { return start + length; }
    bool contains(size_t pos) const { return pos >= start && pos < end(); }
  };

  static std::vector<Range> ClipRange(const Range& base, const Range& clip);

  Range FindNewRange(size_t dest_pos, size_t budget);

  Range CurrentRange();

  std::vector<Block> BlocksInRange(const Range& range);

  Range filled_range_;
  std::vector<uint8_t> store_;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_DEMUX_SLIDING_BUFFER_H_
