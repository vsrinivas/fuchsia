// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package node

import (
	"time"

	"fuchsia.googlesource.com/thinfs/fs"
	"fuchsia.googlesource.com/thinfs/fs/msdosfs/direntry"
)

// Lookup finds a dirent with a given name inside a directory node
//
// Returns the direntry and the index at which the node was found.
func Lookup(n DirectoryNode, name string) (*direntry.Dirent, int, error) {
	return direntry.LookupDirent(direntryCallbackGenerator(n), name)
}

// Read reads the direntry at an index.
// Returns the number of direntry slots used by the direntry. This is meaningful for indexing
// purposes.
func Read(n DirectoryNode, index int) (entry *direntry.Dirent, numSlots int, err error) {
	return direntry.LoadDirent(direntryCallbackGenerator(n), index)
}

// IsEmpty returns true if "n" represents an empty directory.
func IsEmpty(n DirectoryNode) (bool, error) {
	entry, _, err := Read(n, firstDirentIndex(n))
	if err != nil {
		return false, err
	}
	return entry.IsLastFree(), nil
}

// Allocate allocates space for a dirent in the directory, placing entry into
// directory on disk.  Also relocates the "last free" marker if necessary.
//
// Returns the index of the direntry allocated.
// Returns an error if the entry cannot be serialized, there isn't enough space to write the
// direntry/direntries to disk, or there is an error writing to storage.
func Allocate(n DirectoryNode, entry *direntry.Dirent) (direntryIndex int, err error) {
	// Convert in-memory directory entry to on-disk format.
	diskDirents, err := entry.Serialize(direntryCallbackGenerator(n))
	if err != nil {
		return 0, err
	}

	// Ensure the directory has enough space to hold the on-disk entries.
	numNewEntries := len(diskDirents) / direntry.DirentrySize
	direntryIndex = 0
	startDirentryIndex := 0
	numFree := 0
	for {
		if entry, numSlots, err := Read(n, direntryIndex); err != nil {
			return 0, err
		} else if entry.IsLastFree() {
			if numFree != 0 {
				panic("Corrupt directory: 'LastFree' direntry was preceded by free entries")
			}
			// Write the new "LastFree" marker
			if err := write(n, direntry.LastFreeDirent(), startDirentryIndex+numNewEntries); err != nil {
				return 0, err
			}
			// Write our entry before the "LastFree" marker
			return startDirentryIndex, write(n, diskDirents, startDirentryIndex)
		} else if entry.IsFree() {
			// Found a single free spot.
			numFree++
			if numFree == numNewEntries {
				// Found enough free spots to fill initial demand, without hitting final free spot.
				return startDirentryIndex, write(n, diskDirents, startDirentryIndex)
			}
			direntryIndex++
		} else {
			// This spot is NOT free. Reset counter.
			numFree = 0
			direntryIndex += numSlots
			startDirentryIndex = direntryIndex
		}
	}
}

// WriteDotAndDotDot updates the "." and ".." entries for a directory.
// Does not alter node size and does not write any free entries.
func WriteDotAndDotDot(n DirectoryNode, cluster, parentCluster uint32) error {
	if n.IsRoot() {
		panic("Root should not have . or .. entries")
	}
	if err := writeDot(n, cluster); err != nil {
		return err
	} else if err := writeDotDot(n, parentCluster); err != nil {
		return err
	}
	return nil
}

// MakeEmpty writes the "last free" direntry to the node in a location indicating it is empty. It
// also updates the node's size to reflect that it is empty.
func MakeEmpty(n DirectoryNode) error {
	lastFreeIndex := firstDirentIndex(n)
	if err := write(n, direntry.LastFreeDirent(), lastFreeIndex); err != nil {
		return err
	}
	n.SetSize(int64(lastFreeIndex+1) * direntry.DirentrySize)
	return nil
}

// Update updates the direntry for the child node by updating its parent direntry.
// Cannot be used to alter a direntry name.
// Returns the old cluster which was replaced
func Update(parent DirectoryNode, cluster uint32, mTime time.Time, size uint32, direntryIndex int) (uint32, error) {
	entry, numSlots, err := Read(parent, direntryIndex)
	if err != nil {
		return 0, err
	} else if entry.IsFree() {
		panic("Attempting to update a free entry")
	}

	oldCluster := entry.Cluster
	entry.Cluster = cluster
	entry.WriteTime = mTime
	if entry.GetType() == fs.FileTypeDirectory {
		// On disk, FAT directories always store their size as "0"
		entry.Size = 0
	} else {
		entry.Size = size
	}

	// If the name of the dirent has not changed, then the size of the dirent has not changed.
	buf, err := entry.Serialize(nil)
	if err != nil {
		return 0, err
	} else if len(buf)/direntry.DirentrySize != numSlots {
		panic("Unexpected dirent size")
	}

	return oldCluster, write(parent, buf, direntryIndex)
}

// Free frees the direntry at a provided index inside a directory node.
func Free(n DirectoryNode, index int) (*direntry.Dirent, error) {
	entry, numSlots, err := Read(n, index)
	if err != nil {
		return nil, err
	} else if entry.IsFree() {
		panic("Attempting to remove a dirent which is already free")
	}

	// Get the next direntry -- it might be "lastFree", and we might need to move it.
	entryNext, _, err := Read(n, index+numSlots)
	if err != nil {
		return nil, err
	}

	if entryNext.IsLastFree() {
		// If the NEXT direntry was "last free", it isn't any longer.
		// The entry at "index" should be new "last free" -- unless there are free entries before
		// "index", in which case the first entry before "index" to be free is the new "last free".
		lastFreeIndex := index
		for lastFreeIndex > firstDirentIndex(n) {
			entryPrev, _, err := Read(n, lastFreeIndex-1)
			if err != nil {
				return nil, err
			} else if !entryPrev.IsFree() {
				break
			}
			lastFreeIndex--
		}
		// Wipe out the direntry by writing the last free dirent.  Additionally, explicitly fill the
		// rest of the cluster with zeros, as defined by the FAT specification:
		//   "If [the last free marker is found], there are no allocated directory entries after
		//   this one...  all of the DIR_Name[0] bytes in all the entries after this one are also
		//   set to 0"
		clusterSize := int(n.Metadata().Br.ClusterSize())
		off := lastFreeIndex * direntry.DirentrySize
		clusterRemainder := clusterSize - (off+direntry.DirentrySize)%clusterSize
		buf := append(direntry.LastFreeDirent(), make([]byte, clusterRemainder)...)
		if _, err := n.writeAt(buf, int64(off)); err != nil {
			return nil, err
		}
		n.SetSize(int64(lastFreeIndex+1) * direntry.DirentrySize)
	} else {
		// Wipe out the direntry. If multiple direntries were used, then wipe them out.
		for i := index; i < index+numSlots; i++ {
			if err = write(n, direntry.FreeDirent(), i); err != nil {
				return nil, err
			}
		}
	}

	return entry, nil
}

// If "n" is a directory, what's the first non-reserved dirent index?
// If "n" is not a directory, this function panics.
func firstDirentIndex(n DirectoryNode) int {
	if n.IsRoot() {
		return 0
	}
	// One entry for "..", one for "."
	return 2
}

// writeDot and writeDotDot are helpers to write the "." and ".." entries in a directory.
func writeDot(n DirectoryNode, cluster uint32) error {
	if buf, err := direntry.New(".", cluster, fs.FileTypeDirectory).Serialize(nil); err != nil {
		return err
	} else if err = write(n, buf, 0); err != nil {
		return err
	}
	return nil
}
func writeDotDot(n DirectoryNode, parentCluster uint32) error {
	if buf, err := direntry.New("..", parentCluster, fs.FileTypeDirectory).Serialize(nil); err != nil {
		return err
	} else if err = write(n, buf, 1); err != nil {
		return err
	}
	return nil
}

// write updates the direntry at index from buf, a buffer which has a size the must be a
// multiple of "direntry.DirentrySize".
func write(n DirectoryNode, buf []byte, index int) error {
	if len(buf)%direntry.DirentrySize != 0 {
		panic("Malformed direntry cannot be written")
	} else if _, err := n.writeAt(buf, int64(index*direntry.DirentrySize)); err != nil {
		return err
	}
	return nil
}

// direntryCallbackGenerator returns the most commonly used callback for accessing direntries.
// It provides a callback which indexes directly into the contents of a directory node.
func direntryCallbackGenerator(n DirectoryNode) direntry.GetDirentryCallback {
	return func(i int) ([]byte, error) {
		buf := make([]byte, direntry.DirentrySize)
		if _, err := n.readAt(buf, int64(i)*direntry.DirentrySize); err != nil {
			return nil, err
		}
		return buf, nil
	}
}
