// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/archive.h"

#include <lib/syslog/cpp/macros.h>

#include "src/lib/files/file.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fsl/vmo/file.h"
#include "src/lib/fsl/vmo/sized_vmo.h"
#include "src/lib/fsl/vmo/vector.h"
#include "src/lib/fxl/strings/substitute.h"
#include "third_party/zlib/contrib/minizip/unzip.h"
#include "third_party/zlib/contrib/minizip/zip.h"

namespace forensics {
namespace {

using fuchsia::feedback::Attachment;
using fuchsia::mem::Buffer;

bool Archive(const std::vector<Attachment>& attachments, Buffer* archive, zipFile* zf) {
  for (const auto& attachment : attachments) {
    const std::string& filename = attachment.key;

    zip_fileinfo zf_info = {};
    if (int status = zipOpenNewFileInZip64(*zf, filename.c_str(), &zf_info, nullptr, 0, nullptr, 0,
                                           nullptr, Z_DEFLATED, Z_DEFAULT_COMPRESSION, /*zip64=*/1);
        status != ZIP_OK) {
      FX_LOGS(ERROR) << fxl::Substitute("cannot create $0 in output zip archive: ", filename)
                     << status;
      return false;
    }

    std::vector<uint8_t> content;
    if (!fsl::VectorFromVmo(attachment.value, &content)) {
      FX_LOGS(ERROR) << "failed to read VMO for " << filename;
      return false;
    }

    if (int status = zipWriteInFileInZip(*zf, content.data(), content.size()); status != ZIP_OK) {
      FX_LOGS(ERROR) << fxl::Substitute("cannot write $0 in output zip archive: ", filename)
                     << status;
      return false;
    }

    if (int status = zipCloseFileInZip(*zf); status != ZIP_OK) {
      FX_LOGS(WARNING) << fxl::Substitute("cannot close $0 in output zip archive: ", filename)
                       << status;
    }
  }

  return true;
}

}  // namespace

bool Archive(const std::vector<Attachment>& attachments, Buffer* archive) {
  // We write the archive to a temporary file because in-memory archiving in minizip is complicated.
  files::ScopedTempDir tmp_dir;
  std::string archive_filename;
  tmp_dir.NewTempFile(&archive_filename);

  zipFile zf = zipOpen64(archive_filename.c_str(), APPEND_STATUS_CREATE);
  if (zf == nullptr) {
    FX_LOGS(ERROR) << "cannot create output zip archive";
    return false;
  }

  bool success = Archive(attachments, archive, &zf);

  // We always close the archive regardless of the success status.
  if (int status = zipClose(zf, nullptr); status != ZIP_OK) {
    FX_LOGS(WARNING) << "cannot close output zip archive: " << status;
  }

  if (!success) {
    return false;
  }

  fsl::SizedVmo vmo;
  if (!fsl::VmoFromFilename(archive_filename, &vmo)) {
    FX_LOGS(ERROR) << "error loading output zip archive into VMO";
    return false;
  }
  *archive = std::move(vmo).ToTransport();

  return true;
}

namespace {

bool Unpack(const Buffer& archive, std::vector<Attachment>* attachments, unzFile* uf) {
  unz_global_info archive_info;
  if (int status = unzGetGlobalInfo(*uf, &archive_info); status != UNZ_OK) {
    FX_LOGS(ERROR) << "cannot read input zip archive info: " << status;
    return false;
  }

  const auto num_files = archive_info.number_entry;
  if (num_files <= 0) {
    FX_LOGS(ERROR) << "input zip archive contains no files";
    return false;
  }

  for (uint64_t current_file_index = 1; current_file_index <= num_files; ++current_file_index) {
    unz_file_info64 file_info;
    char filename[256];
    if (int status = unzGetCurrentFileInfo64(*uf, &file_info, filename, sizeof(filename), nullptr,
                                             0, nullptr, 0);
        status != UNZ_OK) {
      FX_LOGS(ERROR) << "cannot read current file info in input zip archive: " << status;
      return false;
    }

    const std::string filename_str = std::string(filename);

    if (int status = unzOpenCurrentFile(*uf); status != UNZ_OK) {
      FX_LOGS(ERROR) << fxl::Substitute("cannot open $0 in input zip archive: ", filename_str)
                     << status;
      return false;
    }

    std::vector<uint8_t> data;
    data.reserve(static_cast<size_t>(file_info.uncompressed_size));
    const size_t kBufferSize = 512;
    std::vector<uint8_t> buffer(kBufferSize);
    int num_bytes_or_status;
    do {
      num_bytes_or_status = unzReadCurrentFile(*uf, buffer.data(), kBufferSize);
      if (num_bytes_or_status < 0) {
        FX_LOGS(ERROR) << fxl::Substitute("cannot read $0 in input zip archive: ", filename_str)
                       << num_bytes_or_status;
        return false;
      } else if (num_bytes_or_status > 0) {
        data.insert(data.end(), buffer.data(), buffer.data() + num_bytes_or_status);
      }  // num_bytes_or_status == 0 means EOF
    } while (num_bytes_or_status > 0);

    if (int status = unzCloseCurrentFile(*uf); status != UNZ_OK) {
      FX_LOGS(WARNING) << fxl::Substitute("cannot close $0 in input zip archive: ", filename_str)
                       << status;
    }

    fsl::SizedVmo sized_vmo;
    if (!fsl::VmoFromVector(data, &sized_vmo)) {
      FX_LOGS(ERROR) << "cannot write output VMO for " << filename_str;
      return false;
    }
    Attachment attachment;
    attachment.key = std::move(filename_str);
    attachment.value = std::move(sized_vmo).ToTransport();
    attachments->push_back(std::move(attachment));

    if (current_file_index == num_files) {  // last file, bail out.
      break;
    }

    if (int status = unzGoToNextFile(*uf); status != UNZ_OK) {
      FX_LOGS(ERROR) << "cannot read next file in input zip archive: " << status;
      return false;
    }
  }

  return true;
}

}  // namespace

bool Unpack(const Buffer& archive, std::vector<Attachment>* attachments) {
  // We write the archive to a temporary file because minizip doesn't support unpacking an in-memory
  // archive.
  files::ScopedTempDir tmp_dir;
  std::string archive_filename;
  tmp_dir.NewTempFile(&archive_filename);

  auto data = std::make_unique<uint8_t[]>(archive.size);
  if (zx_status_t status = archive.vmo.read(data.get(), 0u, archive.size); status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "failed to read input zip archive VMO";
    return false;
  }

  if (!files::WriteFile(archive_filename, reinterpret_cast<const char*>(data.get()),
                        archive.size)) {
    FX_LOGS(ERROR) << "failed to write input zip archive VMO to temporary file";
    return false;
  }

  unzFile uf = unzOpen64(archive_filename.c_str());
  if (uf == nullptr) {
    FX_LOGS(ERROR) << "cannot open input zip archive at " << archive_filename;
    return false;
  }

  bool success = Unpack(archive, attachments, &uf);

  // We always close the archive regardless of the success status.
  if (int status = unzClose(uf); status != ZIP_OK) {
    FX_LOGS(WARNING) << "cannot close input zip archive: " << status;
  }

  return success;
}

}  // namespace forensics
