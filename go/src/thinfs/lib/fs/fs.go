// Copyright 2016 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Package fs defines the interface for all filesystems.
package fs

import (
	"errors"
	"os"
)

var (
	// ErrAlreadyExists indicates that requested resource already exists.
	ErrAlreadyExists = os.ErrExist

	// ErrNotFound indicates that the requested resource was not found.
	ErrNotFound = os.ErrNotExist

	// ErrReadOnly indicates that the operation failed because it is not a "Read Only" operation.
	ErrReadOnly = errors.New("operation failed due to read-only permissions")

	// ErrNotEmpty indicates that the caller attempted to remove a non-empty directory.
	ErrNotEmpty = errors.New("directory is not empty")

	// ErrNotOpen indicates that the caller is attempting to close a file or directory
	// that is not currently open.
	ErrNotOpen = errors.New("file or directory not open")

	// ErrNotAFile indicatess that the caller attempted to open a file using a path
	// that did not point to a file.
	ErrNotAFile = errors.New("not a file")

	// ErrNotADir indicates that the caller attempted to open a directory using a path
	// that did not point to a directory.
	ErrNotADir = errors.New("not a directory")

	// ErrIsActive indicates that the caller attempted to unlink a directory with active
	// references.
	ErrIsActive = errors.New("directory has active references")
)

// FileType describes the type of a given file.
type FileType int

// Generic FileTypes.
const (
	FileTypeUnknown FileType = iota
	FileTypeRegularFile
	FileTypeDirectory
	FileTypeCharacterDevice
	FileTypeBlockDevice
	FileTypeFifo
	FileTypeSocket
	FileTypeSymlink
)

// OpenFlags defines the options a client can request when opening a file or directory.
type OpenFlags int

const (
	// OpenFlagCreate indicates that the requested file or directory should be created if it doesn't
	// already exist.
	OpenFlagCreate OpenFlags = 1 << iota

	// OpenFlagExclusive indicates that the requested file or directory must not already exist.  It
	// is only meaningful when used with Create.
	OpenFlagExclusive
)

// FileSystemOptions defines the options a client can request for a filesystem.
type FileSystemOptions int

const (
	// ReadOnly indicates that the file system should be opened only for reading.
	ReadOnly FileSystemOptions = 1 << iota

	// ReadWrite indicates that the file system should be opened for reading as well as
	// writing.
	ReadWrite

	// Force indicates that the file system should be opened regardless of the feature
	// sets listed in the superblock.
	Force
)

// Dirent represents a directory entry in a generic file system.
type Dirent interface {
	// GetType returns the generic FileType of a Dirent.
	GetType() FileType
	// GetName returns the name of a Dirent.
	GetName() string
}

// FileSystem represents a generic filesystem.
type FileSystem interface {
	// Blockcount return the total number of blocks in the file system.
	Blockcount() int64

	// Blocksize returns the size of a single block (in bytes) in the file system.
	Blocksize() int64

	// Size returns the full size of the file system.  Equivalent to f.Blocksize() * f.Blockcount().
	Size() int64

	// Close closes the file system and cleans up any memory used by it.  To prevent memory leaks, it is
	// required for callers to call Close before converting the filesystem object into garbage for the
	// GC.  The returned error is informational only and the Close may not be retried.
	Close() error

	// RootDirectory returns the root directory for the file system.  Callers must close the returned
	// directory to ensure that all changes persist to disk.
	RootDirectory() Directory
}

// Directory represents the generic methods that can be taken on directory.
type Directory interface {
	// Close closes the directory, decrements the active reference count for it, and frees the
	// blocks pointed to by the file if there are no more active references to it and it is no
	// longer linked to by any directory.  Returns an error, if any.  The returned error is purely
	// informational and the close must not be retried.
	Close() error

	// Read returns the contents of the directory and an error, if any.
	Read() ([]Dirent, error)

	// OpenFile opens the file pointed to by name.  name is first cleaned with path.Clean().  d is
	// considered the root directory as well as the current directory for the cleaned path.  OpenFile
	// will return an error if name does not exist unless the Create flag is provided.  If both the
	// Create and Exclusive flags are provided then OpenFile will return an error if the requested
	// file already exists.  OpenFile will return the requested file and an error, if any.  Callers
	// must close the returned file to ensure that any changes made to the file are persisted to the disk.
	// For example, if a file is unlinked from its parent directory while there is still an active
	// reference to it, the underlying inode and blocks for that file will not be freed until the last
	// active reference has been closed.
	OpenFile(name string, flags OpenFlags) (File, error)

	// OpenDirectory opens the directory pointed to by name.  name is first cleaned with path.Clean().  d is
	// considered the root directory as well as the current directory for the cleaned path.  OpenDirectory
	// will return an error if name does not exist unless the Create flag is provided.  If both the
	// Create and Exclusive flags are provided then OpenDirectory will return an error if the requested
	// directory already exists.  OpenDirectory will return the requested directory and an error, if any.
	// Callers must close the returned directory to ensure that changes made to the directory will persist
	// to the disk.
	OpenDirectory(name string, flags OpenFlags) (Directory, error)

	// Rename renames the resource pointed to by src to dst.  Both src and dst are first cleaned
	// with path.Clean().  d is considered both the root and current working directory for the cleaned
	// paths.  Renaming a file or directory will not affect any active references to that file/directory.
	// Rename will not overwrite dst if it already exists.  Returns an error, if any.
	Rename(src, dst string) error

	// Flush does not return until all changes to this directory and its children have been
	// persisted to stable storage.  Returns an error, if any.
	Flush() error

	// Unlink unlinks target from its parent directory.  target is first cleaned with path.Clean().
	// d is considered both the root directory and the current working directory for the cleaned
	// path.  If target points to a directory, then the directory must be empty and it must not have
	// any active references.  If there are no more directories that link to target, then the blocks
	// held by target will be freed.  However, if target is a file and there are currently active
	// references to it then the blocks will not be freed until all the active references have been
	// closed.  Returns an error, if any.
	Unlink(target string) error
}

// File represents a file on the filesystem.
type File interface {
	// Close closes the file, decrements the active reference count for it, and frees the blocks
	// pointed to by the file if there are no more active references to it and it is no longer
	// linked to by any directory.  Returns an error, if any.  The returned error is purely
	// informational and the close must not be retried.
	Close() error

	// ReadAt implements io.ReaderAt for file.
	ReadAt(p []byte, off int64) (int, error)

	// WriteAt implements io.WriterAt for file.
	WriteAt(p []byte, off int64) (int, error)

	// Size returns the size of the file in bytes and an error, if any.
	Size() (int64, error)
}
