// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_MKFS_H_
#define SRC_STORAGE_F2FS_MKFS_H_

namespace f2fs {

constexpr uint32_t kChecksumOffset = 4092;

static const char* kMediaExtList[] = {"jpg", "gif", "png",  "avi", "divx", "mp4", "mp3", "3gp",
                                      "wmv", "wma", "mpeg", "mkv", "mov",  "asx", "asf", "wmx",
                                      "svi", "wvx", "wm",   "mpg", "mpe",  "rm",  "ogg"};

struct MkfsOptions {
  char* label = nullptr;
  bool heap_based_allocation = true;
  uint32_t overprovision_ratio = 5;
  uint32_t segs_per_sec = 1;
  uint32_t secs_per_zone = 1;
  char* extension_list = nullptr;
};

class MkfsWorker {
 public:
  explicit MkfsWorker(Bcache* bc);

  ~MkfsWorker() = default;

  zx_status_t DoMkfs();
  zx_status_t ParseOptions(int argc, char** argv);

 private:
  Bcache* bc_;
  MkfsOptions mkfs_options_;

  // F2FS Parameter
  GlobalParameters params_;
  SuperBlock super_block_;

  void InitGlobalParameters();
  zx_status_t GetDeviceInfo();
  zx_status_t FormatDevice();

  void ConfigureExtensionList();

  zx_status_t WriteToDisk(void* buf, uint64_t offset, size_t length);

  zx_status_t PrepareSuperBlock();
  zx_status_t InitSitArea();
  zx_status_t InitNatArea();

  zx_status_t WriteCheckPointPack();
  zx_status_t WriteSuperBlock();
  zx_status_t WriteRootInode();
  zx_status_t UpdateNatRoot();
  zx_status_t AddDefaultDentryRoot();
  zx_status_t CreateRootDir();

  void PrintUsage();
  void PrintCurrentOption();

  zx_status_t TrimDevice();
};

zx_status_t Mkfs(Bcache* bc, int argc, char** argv);

void AsciiToUnicode(const std::string& in_string, std::u16string* out_string);

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_MKFS_H_
