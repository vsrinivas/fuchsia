// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FILES_PATH_H_
#define SRC_LIB_FILES_PATH_H_

#include <string>

namespace files {

// Resolves ".." and "." components of the path syntactically without consulting
// the file system.
std::string SimplifyPath(std::string path);

// Returns the absolute path of a possibly relative path.
// It doesn't consult the filesystem or simplify the path.
std::string AbsolutePath(const std::string& path);

// Returns the directory name component of the given path.
std::string GetDirectoryName(const std::string& path);

// Returns the basename component of the given path by stripping everything up
// to and including the last slash.
std::string GetBaseName(const std::string& path);

// Returns true if |path| is a valid Fuchsia path name per the fuchsia.io/Name
// rules:
//
// * It cannot be longer than [`MAX_NAME_LENGTH`] (255 bytes).
// * It cannot be empty.
// * It cannot be ".." (dot-dot).
// * It cannot be "." (single dot).
// * It cannot contain "/".
// * It cannot contain embedded NUL.
bool IsValidName(std::string_view name);

// Returns true if |path| is a valid Fuchsia path in canonical form per the
// fuchsia.io/Path rules:
//
// * It cannot be empty.
// * It cannot be longer than `MAX_PATH_LENGTH` (4095 bytes).
// * It cannot have a leading "/".
// * It cannot have a trailing "/".
// * Each component must be a valid `Name`. See IsValidCanonicalName().
bool IsValidCanonicalPath(std::string_view path);

// Delete the file or directory at the given path. If recursive is true, and
// path is a directory, also delete the directory's content.
bool DeletePath(const std::string& path, bool recursive);

// Delete the file or directory at the given path. If recursive is true, and
// path is a directory, also delete the directory's content. If |path| is
// relative, resolve it with |root_fd| as reference. See |openat(2)|.
bool DeletePathAt(int root_fd, const std::string& path, bool recursive);

// Joins two paths together.
// Regardless if |path1| has a trailing '/' or |path2| has a leading '/', there
// will be only one '/' in-between in the joined path.
// Note that if either path is "" then the other path is returned unchanged.
//
// JoinPath("/foo",   "bar") -> "/foo/bar"
// JoinPath("/foo",  "/bar") -> "/foo/bar"
// JoinPath("/foo/",  "bar") -> "/foo/bar"
// JoinPath("/foo/", "/bar") -> "/foo/bar"
//
// JoinPath("",      "") -> ""
// JoinPath("",  "/foo") -> "/foo"
// JoinPath("",   "foo") -> "foo"
// JoinPath("/foo",  "") -> "/foo"
// JoinPath("foo",   "") -> "foo"
// JoinPath("/foo/", "") -> "/foo/"
// JoinPath("foo/",  "") -> "foo/"
std::string JoinPath(const std::string& path1, const std::string& path2);

}  // namespace files

#endif  // SRC_LIB_FILES_PATH_H_
