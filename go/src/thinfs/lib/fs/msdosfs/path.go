// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package msdosfs

import (
	"strings"

	"fuchsia.googlesource.com/thinfs/lib/fs"
	"fuchsia.googlesource.com/thinfs/lib/fs/msdosfs/direntry"
	"fuchsia.googlesource.com/thinfs/lib/fs/msdosfs/node"
)

// Given a path "foo/bar/baz.txt", this function with return a node pointing to "bar" and a string
// containing "baz.txt".
//
// Precondition:
//	 - no node locks are held by the caller
// Postcondition:
//	 - no node locks are held by the caller
//	 - If successful, the opened parent node is ACQUIRED in the dcache
func traversePath(n node.DirectoryNode, path string) (node.DirectoryNode, string, error) {
	if len(path) == 0 || path[0] == '/' {
		// Empty and absolute paths are both disallowed
		return nil, "", fs.ErrInvalidArgs
	}

	// Break the path into components. Remove extraneous "/" characters.
	// TODO(smklein): Be careful, "a/" and "a" are the same right now. Doesn't the "a/" version
	// mean that "a" must exist? Shouldn't we check that?
	pathComponents := strings.Split(path, "/")
	var temp []string
	for i := range pathComponents {
		if pathComponents[i] != "" {
			temp = append(temp, pathComponents[i])
		}
	}
	pathComponents = temp

	oldDirectoryID := int64(-1)
	for i := 0; i < len(pathComponents)-1; i++ {
		component := pathComponents[i]
		if component == "." || (component == ".." && n.IsRoot()) {
			// In this case, "n" stays the same.
		} else {
			n.RLock()
			newDir, err := traverseDirectory(n, component, fs.OpenFlagRead|fs.OpenFlagDirectory)
			n.RUnlock()
			n = newDir
			if oldDirectoryID != -1 {
				n.Metadata().Dcache.Release(uint32(oldDirectoryID))
			}
			if err != nil {
				return nil, "", err
			}
			oldDirectoryID = int64(n.ID())
		}
	}

	if oldDirectoryID == -1 {
		// We haven't iterated over any components. Therefore, our "parent" directory is "n". We
		// need to ACQUIRE this node in the dcache before returning.
		n.Metadata().Dcache.Acquire(n.ID())
	}

	// The final component of the path may be ".", "..", a file name, or a directory name.
	return n, pathComponents[len(pathComponents)-1], nil
}

// Validates that the open flags can open a file of a certain type.
func validateFlags(flags fs.OpenFlags, fileType fs.FileType) error {
	if flags.Create() && flags.Exclusive() {
		return fs.ErrAlreadyExists
	} else if fileType != fs.FileTypeDirectory && flags.Directory() {
		return fs.ErrNotADir
	} else if fileType != fs.FileTypeRegularFile && flags.File() {
		return fs.ErrNotAFile
	} else if fileType == fs.FileTypeDirectory {
		if flags.Append() || flags.Truncate() {
			return fs.ErrNotAFile
		} else if !flags.Read() { // Directories require read permission
			return fs.ErrPermission
		}
	} else if flags.Truncate() && !flags.Write() {
		return fs.ErrPermission
	}
	return nil
}

// Loads a dirent from a directory and verifies that it can be opened with the requested flags.
//
// Precondition:
//	 - parent is rlocked or locked
// Postcondition:
//	 - parent is rlocked or locked
func lookupAndCheck(parent node.DirectoryNode, name string, flags fs.OpenFlags) (*direntry.Dirent, int, error) {
	// Check that the path already exists with the requested name
	entry, direntryIndex, err := node.Lookup(parent, name)
	if err != nil {
		return nil, 0, err
	} else if entry == nil {
		return nil, 0, fs.ErrNotFound
	} else if err := validateFlags(flags, entry.GetType()); err != nil {
		return nil, 0, err
	}
	return entry, direntryIndex, nil
}

// Opens a SINGLE directory WITHOUT path resolution.
//
// Precondition:
//	 - parent is rlocked or locked
// Postcondition:
//	 - parent is rlocked or locked
//	 - If successful, the opened node is ACQUIRED in the dcache
func traverseDirectory(parent node.DirectoryNode, name string, flags fs.OpenFlags) (node.DirectoryNode, error) {
	metadata := parent.Metadata()
	if name == "." {
		panic("Cannot traverse '.'")
	} else if name == ".." && parent.IsRoot() {
		// Edge case: ".." in root does not exist. It should just open the root.
		if err := validateFlags(flags, fs.FileTypeDirectory); err != nil {
			return nil, err
		}
		// ACQUIRE a reference to root (it should already be in the dcache)
		metadata.Dcache.Acquire(parent.ID())
		return parent, nil
	}
	entry, _, err := lookupAndCheck(parent, name, flags)
	if err != nil {
		return nil, err
	} else if entry.GetType() != fs.FileTypeDirectory {
		return nil, fs.ErrNotADir
	}
	return metadata.Dcache.CreateOrAcquire(metadata, entry.Cluster, entry.WriteTime)
}
