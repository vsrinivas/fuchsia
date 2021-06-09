// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_MIGRATION_UTILS_FILE_UTILS_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_MIGRATION_UTILS_FILE_UTILS_H_

#include <fuchsia/io/cpp/fidl.h>
#include <lib/fidl/cpp/interface_handle.h>

#include <fbl/unique_fd.h>

namespace forensics::feedback {

// Convert |dir| into a file descriptor. If |dir| is invalid or the conversion fails, an invalid
// file descriptor will be returned.
fbl::unique_fd IntoFd(::fidl::InterfaceHandle<fuchsia::io::Directory> dir);

// Converts |fd| into an InterfaceHandle. If the object at |fd| is invalid or the conversion fails,
// an invalid InterfaceHandle will be returned.
::fidl::InterfaceHandle<fuchsia::io::Directory> IntoInterfaceHandle(fbl::unique_fd fd);

// Copy the content of |relative_path| under |source_root_fd| to the same relative path under
// |sink_root_fd|. Returns false if an invalid |sink_root_fd| was provided or an error occurred
// while reading or writing.
//
// Note: If content exists at |relative_path| under |sink_root_fd|, this will overwrite it.
bool CopyFile(const fbl::unique_fd& source_root_fd, const fbl::unique_fd& sink_root_fd,
              const std::string& relative_path);

// Fetches the structure of directories under |rood_fd|, relative to |root_fd|. For example, if
// |root_fd| refers to "/foo" and looks like:
//
// * /foo/bar/bar_one.txt
// * /foo/bar/bar_two.txt
// * /foo/baz/baz_one/
//
// The returned vector will contain "./bar", "./baz", and "./baz/baz_one".
//
// Returns false is |root_fd| is invalid.
bool GetNestedDirectories(const fbl::unique_fd& root_fd, std::vector<std::string>* directories);

// Fetches all the files under |rood_fd|, relative to |root_fd|. For example, if |root_fd| refers to
// "/foo" and looks like:
//
// * /foo/foo_one.txt
// * /foo/bar/bar_one.txt
// * /foo/bar/bar_two.txt
// * /foo/baz/baz_one/baz_one.txt
//
// The returned vector will contain "./foo_one.txt", "./bar/bar_one.txt", "./bar/bar_two.txt",
// and "./baz/baz_one/baz_one.txt",
//
// Returns false is |root_fd| is invalid or there's an issue reading directories.
bool GetNestedFiles(const fbl::unique_fd& root_fd, std::vector<std::string>* files);

// Migrates files from |source_root_fd| to |sink_root_fd|, deleting the original files anlong the
// way.
//
// Returns false if |sink_root_fd| is invalid or there's an error migrating data.
bool Migrate(const fbl::unique_fd& source_root_fd, const fbl::unique_fd& sink_root_fd);

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_MIGRATION_UTILS_FILE_UTILS_H_
