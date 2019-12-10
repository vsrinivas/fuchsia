// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_STREAM_UTILS_IMAGE_IO_UTIL_H_
#define SRC_CAMERA_STREAM_UTILS_IMAGE_IO_UTIL_H_

#include <fuchsia/sysmem/cpp/fidl.h>

namespace camera {

inline constexpr const char* kMutablePartitionDirPath = "/data";
inline constexpr const char* kExtension = ".raw";
inline constexpr const char* kFilename = "/frame_";

// An ImageIOUtil object that writes frames from a stream to disk.
// If the files are accessed via `fx shell` they will appear at the following path:
//  data/r/sys/fuchsia.com:[COMPONENT NAME]:0#meta:[COMPONENT NAME].cmx
class ImageIOUtil {
 public:
  // Constructor.
  // Args:
  //  |buffer_collection| A cloned buffer collection containing vmo handles that will have frames
  //                      written into them by another consumer.
  explicit ImageIOUtil(fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection,
                       std::string dir_path)
      : dir_path_(std::move(dir_path)), buffer_collection_(std::move(buffer_collection)) {}

  // Factory method that outputs a Raw12Writer with all its params set for the format.
  // Args:
  //  |buffer_collection| A reference to a buffer collection to be used by a consumer to store
  //                      frames in.
  static std::unique_ptr<ImageIOUtil> Create(
      fuchsia::sysmem::BufferCollectionInfo_2* buffer_collection, const std::string& dir_path);

  // Deletes all data written to disk by this ImageIOUtil so far.
  zx_status_t DeleteImageData();

  // Writes a frame stored in a certain VmoBuffer to disk. Increments `num_image_`.
  // Args:
  //  |id| The id of the buffer containing the frame to be written to disk.
  zx_status_t WriteImageData(uint32_t id);

  std::string GetDirpath() { return kMutablePartitionDirPath + dir_path_; }
  std::string GetFilepath(uint32_t file_num) {
    return kMutablePartitionDirPath + dir_path_ + kFilename + std::to_string(file_num) + kExtension;
  }

 private:
  // Path to the directory under `kMutablePartitionDirPath` that frames will be written to.
  const std::string dir_path_;
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_;
  // Number of image files written so far.
  uint32_t num_image_ = 0;
};

}  // namespace camera

#endif  // SRC_CAMERA_STREAM_UTILS_IMAGE_IO_UTIL_H_
