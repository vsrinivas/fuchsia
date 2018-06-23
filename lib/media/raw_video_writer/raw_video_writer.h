// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MEDIA_RAW_VIDEO_WRITER_RAW_VIDEO_WRITER_H_
#define GARNET_LIB_MEDIA_RAW_VIDEO_WRITER_RAW_VIDEO_WRITER_H_

#include <fbl/atomic.h>
#include <lib/fxl/files/unique_fd.h>

namespace media {

// This class enables a client to easily create and write raw uncompressed video
// frames to a file.  The frames can be played back using a tool like this:
// mplayer -demuxer rawvideo -rawvideo w=320:h=240:format=nv12
//
// The file itself does not have any format metadata associated, so is only
// playable with a command such as the above if the format + size is known or
// guessed OOB.  Limited ability to write uint32_t to the file is provided but
// use of these is discouraged if all the frames are the same format.
//
// The source data is allowed to provide the data in format-specific ways when
// it comes to things like stride, offset to UV plane(s), etc.  Each format has
// a separate Write_.*() method to allow this class to pack the pixel data into
// the file without gaps.
//
// Callers will want to write all the same frame size and same format, but this
// is not enforced currently.  Playing back a file with a mix of frame sizes and
// formats isn't a thing.
//
// After creating the RawVideoWriter object, Initialize() must be called exactly
// once before invoking other methods.  If nullptr or "" is passed to Initialize
// (instead of a valid file name), a default file path+name of
// '/tmp/raw_video_writer_N.wav' is used, where N is an integer corresponding to
// the instance of RawVideoWriter running in that process.
//
// Following Initialize, an appropriate Write method is used to append a video
// frame to the file.  Once the client has completely written the file, the
// client should call Close to close the file.
//
// An instance of the class is single-use.
//
// An instance is thread compatible but not thread safe.  The client bears all
// responsibilities for synchronization.  Call from only one thread at a time,
// with adequate happens-before between calls.  The global "N" allocator is
// thread-safe.
//
// Appending frames is only supported in one format per object instance.
template <bool enabled = true>
class RawVideoWriter {
 public:
  // Default of nullptr uses /tmp/raw_video_writer_N.wav, where N is unique only
  // per instance in this process.
  //
  // A file is opened lazily by the first write call, to avoid creating an empty
  // file if no writes occur.
  explicit RawVideoWriter(const char* file_name = nullptr);

  // Will call Close()
  ~RawVideoWriter();

  // IsOk()
  //
  // Is the file still ok after the writes so far?
  //
  // Calling this is entirely optional.  The failure model here is stateful in
  // the sense that we don't try to write more to the file after a previous
  // open/write to the file fails.
  bool IsOk();

  // WriteUint32BigEndian() / WriteUint32LittleEndian()
  //
  // Escape hatch for writing ad-hoc file header / frame header info.  Currently
  // this is mostly here to establish a pattern of caring about endian-ness for
  // any such escape hatches, just to force consideration of which endian-ness
  // we actually intend to have in the file (not to have an opinion on which
  // choice is best).
  //
  // The return value is 4, always.  A return of 4 does not imply that the write
  // succeeded - for that see IsOk().
  //
  // BigEndian - Write in big-endian to the file regardless of where this code
  //     is running.
  //
  // LittleEndian - Write in little-endian to the file regardless of where this
  //     code is running.
  size_t WriteUint32BigEndian(uint32_t number);
  size_t WriteUint32LittleEndian(uint32_t number);

  // Write an NV12 format frame.
  //
  // The return value is the number of bytes implied by the input parameters
  // always, regardless of write success/failure.  For write success/failure see
  // IsOk().
  //
  // The y_base default of nullptr calculates and returns the count of bytes
  // that would be written to the file without actually writing them.
  //
  // The uv_offset is effectively optional - if 0 is specified (via default or
  // explicitly), the uv_offset used is height_pixels * stride.
  //
  // width_pixels - Y width - must be even, at least for now.  There are this
  //     many pixels width of Y data, and half this many pixels width of U and
  //     V data.
  // height_pixels - Y height - must be even, at least for now.  There are
  //     this many lines of Y data, and half this many lines of UV data.
  // stride - NV12 uses the same stride for Y and UV together.  This is the
  //     offset from Y start-of-line to next Y start-of-line, and the offset
  //     from UV start-of-U-then-V-line to next start-of-U-then-V-line.
  // y_base - pointer to first byte of first pixel of Y plane, or nullptr if
  //     the call is just supposed to calculate how many bytes would have been
  //     written.
  // uv_offset - the bytes offset from y_base to first byte of first pixel of
  //     U data.
  size_t WriteNv12(uint32_t width_pixels, uint32_t height_pixels,
                   uint32_t stride, const uint8_t* y_base = nullptr,
                   uint32_t uv_offset = 0);

  // Close() finishes/closes the file.  IsOk() will return true after this call
  // if closing worked.
  void Close();

  // Delete() deletes the file.  IsOk() will return true after this iff the
  // delete worked.
  void Delete();

 private:
  void EnsureInitialized();
  void Initialize();
  void Fail();
  void WriteData(const uint8_t* to_write, size_t size);

  // Once this becomes false, it stays false.
  bool is_ok_ = true;
  bool is_initialized_ = false;
  bool is_done_ = false;
  std::string file_name_;
  fxl::UniqueFD file_;

  static fbl::atomic<uint32_t> instance_count_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RawVideoWriter);
};

template <>
class RawVideoWriter<false> {
 public:
  explicit RawVideoWriter(const char* file_name = nullptr) {}
  ~RawVideoWriter() {}

  bool IsOk() { return true; }

  size_t WriteUint32BigEndian(uint32_t number) { return sizeof(uint32_t); }
  size_t WriteUint32LittleEndian(uint32_t number) { return sizeof(uint32_t); }

  size_t WriteNv12(uint32_t width_pixels, uint32_t height_pixels,
                   uint32_t stride, const uint8_t* y_base = nullptr,
                   uint32_t uv_offset = 0) {
    return height_pixels * width_pixels + height_pixels / 2 * width_pixels;
  }

  void Close() {}
  void Delete() {}

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(RawVideoWriter);
};

}  // namespace media

#endif  // GARNET_LIB_MEDIA_RAW_VIDEO_WRITER_RAW_VIDEO_WRITER_H_
