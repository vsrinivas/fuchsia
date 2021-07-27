// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <string.h>
#include <sys/stat.h>
#include <zircon/errors.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "blobfs.h"
#include "src/storage/blobfs/blob_layout.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/fsck_host.h"
#include "src/storage/blobfs/host.h"

namespace {

// Add the blob described by |info| on host to the |blobfs| blobfs store.
zx_status_t AddBlob(blobfs::Blobfs* blobfs, JsonRecorder* json_recorder,
                    const blobfs::BlobInfo& info) {
  const std::optional<std::filesystem::path>& blob_src = info.GetSrcFilePath();
  if (zx::status<> status = blobfs->AddBlob(info); status.is_error()) {
    if (blob_src.has_value()) {
      fprintf(stderr, "blobfs: Failed to add blob '%s': %d\n", blob_src->c_str(),
              status.status_value());
    }
    return status.status_value();
  }

  if (json_recorder != nullptr && blob_src.has_value()) {
    const blobfs::BlobLayout& blob_layout = info.GetBlobLayout();
    json_recorder->Append(blob_src->string(), info.GetDigest().ToString().c_str(),
                          blob_layout.FileSize(),
                          size_t{blob_layout.TotalBlockCount()} * blobfs::kBlobfsBlockSize);
  }

  return ZX_OK;
}

}  // namespace

zx_status_t BlobfsCreator::Usage() {
  zx_status_t status = FsCreator::Usage();

  fprintf(stderr, "\nblobfs specific options:\n");
  fprintf(stderr,
          "\t--deprecated_padded_format\tFormat blobfs using the deprecated format that uses more "
          "space.\n"
          "Valid for the commands: mkfs and create.\n");
  // Additional information about manifest format.
  fprintf(stderr, "\nEach manifest line must adhere to one of the following formats:\n");
  fprintf(stderr, "\t'dst/path=src/path'\n");
  fprintf(stderr, "\t'dst/path'\n");
  fprintf(stderr, "with one dst/src pair or single dst per line.\n");

  fprintf(stderr, "\nblobfs specific commands:\n");
  fprintf(stderr, "\texport [IMAGE] [PATH]\n");
  fprintf(stderr,
          "\nExports each blob in IMAGE to the directory in PATH. If PATH does not exist, will "
          "attempt to "
          "create it.\n");
  fprintf(stderr,
          "\nEach blob exported to PATH is named after their merkle root, and the contents match "
          "what IMAGE has.\n");
  return status;
}

bool BlobfsCreator::IsCommandValid(Command command) {
  switch (command) {
    case Command::kMkfs:
    case Command::kFsck:
    case Command::kUsedDataSize:
    case Command::kUsedInodes:
    case Command::kUsedSize:
    case Command::kAdd:
      return true;
    default:
      return false;
  }
}

bool BlobfsCreator::IsOptionValid(Option option) {
  // TODO(planders): Add offset and length support to blobfs.
  switch (option) {
    case Option::kDepfile:
    case Option::kReadonly:
    case Option::kCompress:
    case Option::kJsonOutput:
    case Option::kHelp:
      return true;
    default:
      return false;
  }
}

bool BlobfsCreator::IsArgumentValid(Argument argument) {
  switch (argument) {
    case Argument::kManifest:
    case Argument::kBlob:
      return true;
    default:
      return false;
  }
}

zx_status_t BlobfsCreator::ProcessManifestLine(FILE* manifest, const char* dir_path) {
  char src[PATH_MAX];
  src[0] = '\0';
  char dst[PATH_MAX];
  dst[0] = '\0';

  zx_status_t status;
  if ((status = ParseManifestLine(manifest, dir_path, src, dst)) != ZX_OK) {
    return status;
  }

  if (!strlen(src)) {
    fprintf(stderr, "Manifest line must specify source file\n");
    return ZX_ERR_INVALID_ARGS;
  }

  blob_list_.push_back(src);
  return ZX_OK;
}

zx_status_t BlobfsCreator::ProcessCustom(int argc, char** argv, uint8_t* processed) {
  if (strcmp(argv[0], "--blob") == 0) {
    constexpr uint8_t required_args = 2;
    if (argc < required_args) {
      fprintf(stderr, "Not enough arguments for %s\n", argv[0]);
      return ZX_ERR_INVALID_ARGS;
    }
    blob_list_.push_back(argv[1]);
    *processed = required_args;
    return ZX_OK;
  }

  if (strcmp(argv[0], "--deprecated_padded_format") == 0) {
    if (GetCommand() != Command::kMkfs) {
      fprintf(stderr, "%s is only valid for mkfs and create\n", argv[0]);
      return ZX_ERR_INVALID_ARGS;
    }
    blob_layout_format_ = blobfs::BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart;
    *processed = 1;
    return ZX_OK;
  }

  // TODO(fxbug.dev/81353) Remove this flag. It duplicates the --deprecated_padded_format flag
  // above and is left here to facilitate a soft transition with out-of-tree uses of this script.
  if (strcmp(argv[0], "--blob_layout_format") == 0) {
    constexpr uint8_t required_args = 2;
    if (GetCommand() != Command::kMkfs) {
      fprintf(stderr, "%s is only valid for mkfs and create\n", argv[0]);
      return ZX_ERR_INVALID_ARGS;
    }
    if (argc < required_args) {
      fprintf(stderr, "Not enough arguments for %s\n", argv[0]);
      return ZX_ERR_INVALID_ARGS;
    }
    auto format = blobfs::ParseBlobLayoutFormatCommandLineArg(argv[1]);
    if (format.is_error()) {
      fprintf(stderr, "Invalid argument to %s, expected \"padded\" or \"compact\" but got \"%s\"\n",
              argv[0], argv[1]);
      return format.status_value();
    }
    blob_layout_format_ = format.value();
    *processed = required_args;
    return ZX_OK;
  }

  fprintf(stderr, "Argument not found: %s\n", argv[0]);
  return ZX_ERR_INVALID_ARGS;
}

zx_status_t BlobfsCreator::CalculateRequiredSize(off_t* out) {
  std::vector<std::thread> threads;
  unsigned blob_index = 0;
  unsigned n_threads = std::thread::hardware_concurrency();
  if (!n_threads) {
    n_threads = 4;
  }
  zx_status_t status = ZX_OK;
  std::mutex mtx;
  bool should_compress = ShouldCompress();
  for (unsigned j = n_threads; j > 0; j--) {
    threads.emplace_back([&] {
      std::vector<blobfs::BlobInfo> local_blob_info_list;
      unsigned i = 0;
      while (true) {
        mtx.lock();
        if (status != ZX_OK) {
          mtx.unlock();
          return;
        }
        i = blob_index++;
        mtx.unlock();
        if (i >= blob_list_.size()) {
          break;
        }
        const std::filesystem::path& path = blob_list_[i];
        zx_status_t res;
        if ((res = AppendDepfile(path.c_str())) != ZX_OK) {
          mtx.lock();
          status = res;
          mtx.unlock();
          return;
        }

        fbl::unique_fd data_fd(open(path.c_str(), O_RDONLY, 0644));
        zx::status<blobfs::BlobInfo> blob_info =
            should_compress ? blobfs::BlobInfo::CreateCompressed(data_fd.get(), blob_layout_format_,
                                                                 std::optional(path))
                            : blobfs::BlobInfo::CreateUncompressed(
                                  data_fd.get(), blob_layout_format_, std::optional(path));
        if (blob_info.is_error()) {
          mtx.lock();
          status = blob_info.status_value();
          mtx.unlock();
          return;
        }

        local_blob_info_list.push_back(std::move(blob_info).value());
      }
      mtx.lock();
      blob_info_list_.insert(blob_info_list_.end(),
                             std::make_move_iterator(local_blob_info_list.begin()),
                             std::make_move_iterator(local_blob_info_list.end()));
      mtx.unlock();
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  if (status != ZX_OK) {
    return status;
  }

  // Remove all duplicate blobs by first sorting the merkle trees by
  // digest, and then by reshuffling the vector to exclude duplicates.
  std::sort(blob_info_list_.begin(), blob_info_list_.end(), DigestCompare());
  auto compare = [](const blobfs::BlobInfo& lhs, const blobfs::BlobInfo& rhs) {
    return lhs.GetDigest() == rhs.GetDigest();
  };
  auto it = std::unique(blob_info_list_.begin(), blob_info_list_.end(), compare);
  blob_info_list_.erase(it, blob_info_list_.end());

  for (const auto& blob_info : blob_info_list_) {
    data_blocks_ += blob_info.GetBlobLayout().TotalBlockCount();
  }

  required_inodes_ = std::max(blobfs::kBlobfsDefaultInodeCount, uint64_t{blob_info_list_.size()});

  blobfs::Superblock info;
  // Initialize enough of |info| to be able to compute the number of bytes the image will occupy.
  info.inode_count = required_inodes_;
  info.data_block_count = data_blocks_;
  info.journal_block_count = blobfs::kDefaultJournalBlocks;
  *out = blobfs::TotalBlocks(info) * blobfs::kBlobfsBlockSize;
  return ZX_OK;
}

zx_status_t BlobfsCreator::Mkfs() {
  uint64_t block_count;
  if (blobfs::GetBlockCount(fd_.get(), &block_count)) {
    fprintf(stderr, "blobfs: cannot find end of underlying device\n");
    return ZX_ERR_IO;
  }

  int r = blobfs::Mkfs(fd_.get(), block_count,
                       {.blob_layout_format = blob_layout_format_, .num_inodes = required_inodes_});

  if (r >= 0 && !blob_list_.empty()) {
    zx_status_t status;
    if ((status = Add()) != ZX_OK) {
      return status;
    }
  }
  return r;
}

zx_status_t BlobfsCreator::Fsck() {
  zx_status_t status;
  std::unique_ptr<blobfs::Blobfs> vn;
  if ((status = blobfs::blobfs_create(&vn, std::move(fd_))) < 0) {
    return status;
  }

  return blobfs::Fsck(vn.get());
}

zx_status_t BlobfsCreator::UsedDataSize() {
  zx_status_t status;
  uint64_t size;
  if ((status = blobfs::UsedDataSize(fd_, &size)) != ZX_OK) {
    return status;
  }

  printf("%" PRIu64 "\n", size);
  return ZX_OK;
}

zx_status_t BlobfsCreator::UsedInodes() {
  zx_status_t status;
  uint64_t used_inodes;
  if ((status = blobfs::UsedInodes(fd_, &used_inodes)) != ZX_OK) {
    return status;
  }

  printf("%" PRIu64 "\n", used_inodes);
  return ZX_OK;
}

zx_status_t BlobfsCreator::UsedSize() {
  zx_status_t status;
  uint64_t size;
  if ((status = blobfs::UsedSize(fd_, &size)) != ZX_OK) {
    return status;
  }

  printf("%" PRIu64 "\n", size);
  return ZX_OK;
}

zx_status_t BlobfsCreator::Add() {
  if (blob_list_.empty()) {
    fprintf(stderr, "Adding a blob requires an additional file argument\n");
    return Usage();
  }

  zx_status_t status = ZX_OK;
  std::unique_ptr<blobfs::Blobfs> blobfs;
  if ((status = blobfs_create(&blobfs, std::move(fd_))) != ZX_OK) {
    return status;
  }

  for (const auto& blob_info : blob_info_list_) {
    if ((status = AddBlob(blobfs.get(), json_recorder(), blob_info)) < 0) {
      break;
    }
  }

  return status;
}

int ExportBlobs(std::string& source_path, std::string& output_path) {
  fbl::unique_fd blobfs_image(open(source_path.c_str(), O_RDONLY));
  if (!blobfs_image.is_valid()) {
    fprintf(stderr, "Failed to open blobfs image at %s. More specifically: %s.\n",
            source_path.c_str(), strerror(errno));
    return -1;
  }

  std::unique_ptr<blobfs::Blobfs> fs = nullptr;
  if (blobfs::blobfs_create(&fs, std::move(blobfs_image)) != ZX_OK) {
    return -1;
  }

  // Try to create path if it doesn't exist.
  std::filesystem::create_directories(output_path);
  fbl::unique_fd output_fd(open(output_path.c_str(), O_DIRECTORY));
  if (!output_fd.is_valid()) {
    fprintf(stderr, "Failed to obtain a handle to output path at %s. More specifically: %s.\n",
            output_path.c_str(), strerror(errno));
    return -1;
  }

  auto export_result = blobfs::ExportBlobs(output_fd.get(), *fs);
  if (export_result.is_error()) {
    fprintf(stderr, "Encountered error while exporting blobs. More specifically: %s.\n",
            export_result.error().c_str());
    return -1;
  }
  fprintf(stderr, "Successfully exported all blobs.\n");
  return 0;
}

int main(int argc, char** argv) {
  BlobfsCreator blobfs;

  if (argc > 3) {
    if (strcmp(argv[1], "export") == 0) {
      std::string image_path = argv[2];
      std::string output_path = argv[3];
      return ExportBlobs(image_path, output_path);
    }
  }

  if (blobfs.ProcessAndRun(argc, argv) != ZX_OK) {
    return -1;
  }

  return 0;
}
