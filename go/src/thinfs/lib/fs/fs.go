// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package fs defines the interface for all filesystems.
package fs

import (
	"errors"
	"os"
	"time"
)

var (
	// ErrInvalidArgs indicates the arguments were invalid
	ErrInvalidArgs = os.ErrInvalid

	// ErrNotFound indicates that the requested resource was not found
	ErrNotFound = os.ErrNotExist

	// ErrAlreadyExists indicates that requested resource already exists
	ErrAlreadyExists = os.ErrExist

	// ErrPermission indicates that the operation failed due to insufficient permissions
	ErrPermission = os.ErrPermission

	// ErrReadOnly indicates that the operation failed because the underlying resource is read only
	ErrReadOnly = ErrPermission

	// ErrResourceExhausted indicates that a resource (such as disk space) has been used up
	ErrResourceExhausted = errors.New("a filesystem resource has been exhausted")

	// ErrFailedPrecondition indicates the system is not in a state where the operation can succeed
	ErrFailedPrecondition = errors.New("the filesystem is not in a state required for the operation")

	// ErrAborted indicates that due to a system state change, the operation was aborted
	ErrAborted = errors.New("the operation was aborted")

	// ErrOutOfRange indicates the operation is not in a valid range
	ErrOutOfRange = errors.New("requested operation would be out of valid range")

	// ErrNotEmpty indicates that the caller attempted to remove a non-empty directory
	ErrNotEmpty = errors.New("directory is not empty")

	// ErrNotOpen indicates that the caller is attempting to access a file or directory
	// that is not currently open.
	ErrNotOpen = errors.New("file or directory not open")

	// ErrNotAFile indicatess that the caller attempted to open a file using a path
	// that did not point to a file
	ErrNotAFile = errors.New("not a file")

	// ErrNotADir indicates that the caller attempted to open a directory using a path
	// that did not point to a directory
	ErrNotADir = errors.New("not a directory")

	// ErrIsActive indicates that the caller attempted to unlink a directory with active
	// references
	ErrIsActive = errors.New("directory has active references")

	// ErrUnmounted indicates that the entire filesystem has been unmounted, and all future
	// operations will fail
	ErrUnmounted = errors.New("operation failed because filesystem is unmounted")
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
	// OpenFlagRead indicates the file should be opened with read permissions
	OpenFlagRead OpenFlags = 1 << iota
	// OpenFlagWrite indicates the file should be opened with write permissions
	OpenFlagWrite
	// OpenFlagAppend indicates all writes should append to the file (only valid with "write")
	OpenFlagAppend
	// OpenFlagTruncate indicates the file should be truncated before writing (only valid with
	// "write")
	OpenFlagTruncate
	// OpenFlagCreate indicates the file should be created if it does not exist (only valid with
	// "write")
	OpenFlagCreate
	// OpenFlagExclusive indicates that the operation should fail if it already exists (only valid
	// with "create")
	OpenFlagExclusive
	// OpenFlagFile indicates the operation must act on a file
	OpenFlagFile
	// OpenFlagDirectory indicates the operation must act on a directory
	OpenFlagDirectory
)

// Read returns if the Read permission is active
func (f OpenFlags) Read() bool {
	return f&OpenFlagRead != 0
}

// Write returns if the Write permission is active
func (f OpenFlags) Write() bool {
	return f&OpenFlagWrite != 0
}

// Append returns if the Append permission is active
func (f OpenFlags) Append() bool {
	return f&OpenFlagAppend != 0
}

// Truncate returns if the Truncate flag is active
func (f OpenFlags) Truncate() bool {
	return f&OpenFlagTruncate != 0
}

// Create returns if the Create flag is active
func (f OpenFlags) Create() bool {
	return f&OpenFlagCreate != 0
}

// Exclusive returns if the Exclusive flag is active
func (f OpenFlags) Exclusive() bool {
	return f&OpenFlagExclusive != 0
}

// File returns if the File flag is active
func (f OpenFlags) File() bool {
	return f&OpenFlagFile != 0
}

// Directory returns if the Directory flag is active
func (f OpenFlags) Directory() bool {
	return f&OpenFlagDirectory != 0
}

// Whence is used to explain the meaning of an offset in a file
const (
	// WhenceFromStart means the offset starts at the beginning of the file
	WhenceFromStart = iota
	// WhenceFromCurrent means the offset starts at the current file position
	WhenceFromCurrent
	// WhenceFromEnd means the offset starts at the end of the file
	WhenceFromEnd
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
	// Close closes the file, decrements the active reference count for it, and frees the
	// blocks pointed to by the file if there are no more active references to it and it is no
	// longer linked to by any directory.  Returns an error, if any.  The returned error is purely
	// informational and the close must not be retried.
	Close() error

	// Stat returns the size, last access time, and last modified time (if known) of the directory.
	Stat() (int64, time.Time, time.Time, error)

	// Touch updates the directory's access and modification times.
	Touch(lastAccess, lastModified time.Time) error

	// Dup returns a directory which shares all the information as 'this' directory, including the
	// permissions and file seek position.
	Dup() (Directory, error)

	// Reopen returns a directory which has a copy of all the information contained in 'this'
	// directory, except
	// 1) The flags may be downgraded with the "flags" provided.
	// 2) The file position is set to either zero or EOF depending on "flags".
	Reopen(flags OpenFlags) (Directory, error)

	// Read returns the contents of the directory and an error, if any.
	Read() ([]Dirent, error)

	// Open opens the file pointed to by "name".
	// d is considered the current directory for the "name".
	Open(name string, flags OpenFlags) (File, Directory, error)

	// Rename renames the resource pointed to by src to dst.
	// Renaming a file or directory will not affect any active references to that file/directory.
	// Rename will not overwrite dst if it already exists.  Returns an error, if any.
	Rename(src, dst string) error

	// Flush does not return until all changes to this directory and its children have been
	// persisted to stable storage.  Returns an error, if any.
	Flush() error

	// Unlink unlinks target from its parent directory.
	// If target points to a directory, then the directory must be empty and it must not have
	// any active references.  If there are no more directories that link to target, then the blocks
	// held by target will be freed.  However, if target is a file and there are currently active
	// references to it then the blocks will not be freed until all the active references have been
	// closed. Returns an error, if any.
	Unlink(target string) error
}

// File represents a normal file on the filesystem.
type File interface {
	// Close closes the file, decrements the active reference count for it, and frees the blocks
	// pointed to by the file if there are no more active references to it and it is no longer
	// linked to by any directory.  Returns an error, if any.  The returned error is purely
	// informational and the close must not be retried.
	Close() error

	// Stat returns the size, last access time, and last modified time of the file.
	Stat() (int64, time.Time, time.Time, error)

	// Touch updates the file's access and modification times.
	Touch(lastAccess, lastModified time.Time) error

	// Dup returns a file which shares all the information as 'this' file, including the
	// permissions and file seek position.
	Dup() (File, error)

	// Reopen returns a file which has a copy of all the information contained in 'this' file,
	// except
	// 1) The flags may be downgraded with the "flags" provided.
	// 2) The file position is set to either zero or EOF depending on "flags".
	Reopen(flags OpenFlags) (File, error)

	// Read reads a maximum of len(p) bytes into "p" from the file at a location decided by "off"
	// and "whence".
	// The seek position is only updated if fs.WhenceFromCurrent is passed as whence.
	Read(p []byte, off int64, whence int) (int, error)

	// Write writes a maximum of len(p) bytes from "p" into the file at a location decided by
	// "off" and "whence".
	// The seek position is only updated if fs.WhenceFromCurrent is passed as whence.
	Write(p []byte, off int64, whence int) (int, error)

	// Truncate reduces/expands the file to the size specified by "size" in bytes.
	// If the file length is increased, it is filled with zeroes.
	// Truncate does not modify the seek position.
	Truncate(size uint64) error

	// Tell identifies what the current seek position is.
	Tell() (int64, error)

	// Seek modified the seek position to offset + some starting position, dependent on whence.
	// Seek returns the new seek position.
	Seek(offset int64, whence int) (int64, error)
}
