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

package node

import (
	"errors"
	"sync"

	"fuchsia.googlesource.com/thinfs/lib/fs/msdosfs/metadata"
)

var (
	// ErrEOF indicates the operation could not complete due to reaching the End of the File
	ErrEOF = errors.New("FAT Node: EOF hit before operation completed")

	// ErrNoSpace indicates the requested operation requires more space than is available
	ErrNoSpace = errors.New("FAT Node: Not enough space for requested operation")

	// ErrBadArgument indicates the argument passed to a node was invalid
	ErrBadArgument = errors.New("FAT Node: Bad argument passed to node")
)

// Node implements the interface of a single node (file, directory, or root) in the FAT filesystem.
type Node interface {
	sync.Locker // Writer-lock access
	RLock()     // Reader-lock access
	RUnlock()   // Unlock reader lock

	// Write-access methods, which may modify the contents of the Node
	MoveNode(newParent Node, newDirentIndex uint)
	RemoveChild(direntIndex uint)
	SetSize(size int64) error                 // Modify the number of bytes accessible within the node
	WriteAt(p []byte, off int64) (int, error) // Implements io.WriterAt
	RefUp()                                   // Increment refs
	RefDown(numRefs int) error                // Decrement refs, possibly delete clusters if they're unused
	MarkDeleted()                             // Mark that the node's clusters should be deleted when refs is zero
	setChild(direntIndex uint, child Node)    // Internal method to place a child at an empty direntInex

	// Read-access methods, which do not modify the contents of the Node
	Parent() (parent Node, direntIndex uint)
	Children() []Node
	Child(direntIndex uint) (Node, bool)
	Size() int64                             // Return the number of bytes accessible within the node
	StartCluster() uint32                    // Returns the first cluster of the node (or EOF)
	NumClusters() int                        // Returns the number of clusters used by the node (internally)
	ReadAt(p []byte, off int64) (int, error) // Implements io.ReaderAt
	RefCount() int                           // Number of external references ('refs') to the node
	IsRoot() bool                            // True iff the node corresponds to the root of a filesystem

	// Accessible without a lock
	IsDirectory() bool    // True iff the node corresponds to a directory
	Info() *metadata.Info // Return constant info about the node's filesystem
}
