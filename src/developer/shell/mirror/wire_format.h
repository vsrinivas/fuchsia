// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/time.h>

#include <filesystem>
#include <string>
#include <vector>

#include "src/developer/debug/shared/stream_buffer.h"
#include "src/developer/shell/mirror/common.h"

#ifndef SRC_DEVELOPER_SHELL_MIRROR_WIRE_FORMAT_H_
#define SRC_DEVELOPER_SHELL_MIRROR_WIRE_FORMAT_H_

namespace shell::mirror {

namespace remote_commands {
// Send this to a service to kill it
constexpr char kQuitCommand[] = "quitquitquit";

// Send this to a service to get the files out.
constexpr char kFilesCommand[] = "hello";
}  // namespace remote_commands

// Basic abstraction for storing an individual file.
// |Name| is the file path.
// |Data| is the contents of the file.
class File {
 public:
  File(const std::filesystem::path& name, std::unique_ptr<char[]> data, int length) {
    buf_ = std::move(data);
    data_ = std::string_view(buf_.get(), length);
    SetName(name);
  }

  File(File&& other)
      : path_(std::move(other.path_)),
        buf_(std::move(other.buf_)),
        data_(buf_.get(), other.data_.length()) {}

  File& operator=(File&& other) {
    buf_ = std::move(other.buf_);
    path_ = std::move(other.path_);
    data_ = std::string_view(buf_.get(), other.data_.length());
    return *this;
  }

  File() = delete;
  File(File&) = delete;
  File& operator=(const File&) = delete;

  size_t NameLength() const { return path_.string().size(); }
  const char* Name() const { return path_.c_str(); }
  void SetName(const std::filesystem::path& path_name) { path_ = path_name; }

  const std::string_view View() const { return data_; }

 private:
  std::filesystem::path path_;   // non-NUL-terminated name.
  std::unique_ptr<char[]> buf_;  // File contents
  std::string_view data_;        // File contents as string_view
};

// Abstraction for storing multiple files.
class Files {
 public:
  // Constructor.  |root_dir| is the root of the directory containing the files,
  // and will not be serialized as part of filenames.
  explicit Files(const std::string& root_dir) : root_dir_(root_dir) {}
  Files() = default;

  // Add the file at location |path| to this list of File objects.  Reads the contents from the
  // filesystem.
  int AddFile(const std::filesystem::path&);

  // Add the file at location |path| to this list of File objects, with the contents from |contents|
  // of length |length|.
  int AddFile(const std::filesystem::path& path, std::unique_ptr<char[]>&& contents, long length);

  // Returns a reference to the list of File objects stored by this object.
  const std::vector<File>& GetFiles() const { return files_; }

  // The following two methods refer to "the data format".  The data format is:
  // uint64_t <number-of-files>
  // <repeat number-of-files times>
  //   uint64_t path-length
  //   char[path-length] path
  //   uint64_t content-length
  //   char[content-length] content

  // Writes the contents of the Files into the std::vector using the data format.
  int DumpFiles(std::vector<char>*);

  // Returns a new instantiation of Files, pulled off of fd in the data format.
  static Files FilesFromFD(int fd, Err* error, struct timeval* timeout = nullptr);

  std::vector<File>::iterator begin() { return files_.begin(); }
  std::vector<File>::const_iterator begin() const { return files_.begin(); }
  std::vector<File>::iterator end() { return files_.end(); }
  std::vector<File>::const_iterator end() const { return files_.end(); }

  size_t size() { return files_.size(); }

  Files(Files&& other) { *this = std::move(other); }

  Files& operator=(Files&& other) {
    root_dir_ = std::move(other.root_dir_);
    files_ = std::move(other.files_);
    return *this;
  }

 private:
  std::filesystem::path root_dir_;
  std::vector<File> files_;
};

}  // namespace shell::mirror

#endif  // SRC_DEVELOPER_SHELL_MIRROR_WIRE_FORMAT_H_
