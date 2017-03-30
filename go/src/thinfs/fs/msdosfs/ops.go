// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package msdosfs

import (
	"time"

	"fuchsia.googlesource.com/thinfs/fs"
	"fuchsia.googlesource.com/thinfs/fs/msdosfs/direntry"
	"fuchsia.googlesource.com/thinfs/fs/msdosfs/node"
)

func closeFile(n node.FileNode) error {
	doClose := true
	return flushFile(n, doClose)
}

func closeDirectory(n node.DirectoryNode, unmount bool) error {
	n.Lock()
	defer n.Unlock()
	err := n.RefDown(1)
	n.Metadata().Dcache.Release(n.ID())
	return err
}

func stat(n node.Node) (int64, time.Time, time.Time, error) {
	n.RLock()
	defer n.RUnlock()
	return n.Size(), time.Time{}, n.MTime(), nil
}

func touch(n node.Node, lastAccess, lastModified time.Time) {
	n.Lock()
	defer n.Unlock()
	n.SetMTime(lastModified)
}

func dup(n node.Node) {
	metadata := n.Metadata()
	if n.IsDirectory() {
		metadata.Dcache.Acquire(n.(node.DirectoryNode).ID())
	} else {
		parent, _ := n.(node.FileNode).LockParent()
		if parent != nil {
			// If we have a parent, ACQUIRE a reference to it.
			metadata.Dcache.Acquire(parent.ID())
			parent.Unlock()
		}
	}

	n.Lock()
	n.RefUp()
	n.Unlock()
}

func readDir(n node.DirectoryNode) ([]fs.Dirent, error) {
	n.RLock()
	defer n.RUnlock()

	var result []fs.Dirent
	if n.IsRoot() {
		// The root directory does not contain "." or "..". Create these fake entries.
		dot := direntry.New(".", 0, fs.FileTypeDirectory)
		dotdot := direntry.New("..", 0, fs.FileTypeDirectory)
		dot.WriteTime = n.MTime()
		dotdot.WriteTime = n.MTime()
		result = append(result, dot, dotdot)
	}

	for i := 0; ; {
		entry, numSlots, err := node.Read(n, i)
		if err != nil {
			return nil, err
		}

		if entry.IsLastFree() {
			break
		} else if !entry.IsFree() {
			result = append(result, entry)
		}
		i += numSlots
	}
	return result, nil
}

func open(n node.DirectoryNode, name string, flags fs.OpenFlags) (node.Node, error) {
	// ACQUIRE parent directory from dcache.
	// After this function succeeds, we'll need to be sure to RELEASE "parent" before returning.
	parent, singleName, err := traversePath(n, name)
	if err != nil { // Parent directory containing "name" cannot be resolved
		return nil, err
	}

	// Helper function to validate the flags when opening files via "." or "..".
	validateDotFlags := func(flags fs.OpenFlags) error {
		if flags.Create() || flags.Exclusive() {
			return fs.ErrInvalidArgs // Node already exists; these are bad arguments
		} else if flags.Append() || flags.Truncate() || flags.File() {
			return fs.ErrNotAFile // File-specific args
		} else if !flags.Read() {
			return fs.ErrPermission // Directories require read privileges
		}
		return nil
	}

	switch singleName {
	case ".":
		if err = validateDotFlags(flags); err != nil {
			n.Metadata().Dcache.Release(parent.ID())
			return nil, err
		}
		parent.Lock()
		parent.RefUp()
		parent.Unlock()
		// We don't need to RELEASE the dcache reference to the parent directory, since we're
		// opening that node anyway.
		return parent, nil
	case "..":
		if err = validateDotFlags(flags); err != nil {
			n.Metadata().Dcache.Release(parent.ID())
			return nil, err
		}
	}

	// Either "openIncremental" or "createIncremental" will ACQUIRE the target from the dcache
	// if it is a directory.
	openedNode, err := func() (node.Node, error) {
		parent.Lock() // Read from the parent; potentially open a child.
		defer parent.Unlock()
		defer n.Metadata().Dcache.Release(parent.ID())
		openedNode, err := openIncremental(parent, singleName, flags)
		if err == fs.ErrNotFound {
			// If we could not open the file/directory because it doesn't exist, try creating it.
			openedNode, err = createIncremental(parent, singleName, flags)
		}
		if err != nil {
			return nil, err
		}
		return openedNode, nil
	}()
	if err != nil {
		return nil, err
	}

	// Thanks to {traverse,create}Incremental, we know that openedNode exists in the dcache (if it
	// is a directory). Increase the external refcount, and possibly truncate.
	openedNode.Lock()
	defer openedNode.Unlock()
	openedNode.RefUp()
	if flags.Truncate() {
		openedNode.SetSize(0)
	}
	return openedNode, nil
}

// Renames a file or directory to a new location.
// Currently, only supports a single-threaded version which locks the entire filesystem.
func rename(srcStart node.DirectoryNode, dstStart node.DirectoryNode, src, dst string) error {
	metadata := srcStart.Metadata()
	srcParent, srcName, err := traversePath(srcStart, src) // ACQUIRE srcParent...
	if err != nil {
		return err
	}
	defer metadata.Dcache.Release(srcParent.ID()) // ... ensure it is RELEASED

	// Verify that src is valid, independent of dst
	if srcName == "." || srcName == ".." {
		return fs.ErrIsActive
	}
	srcEntry, srcDirentryIndex, err := node.Lookup(srcParent, srcName)
	if err != nil {
		return err
	} else if srcEntry == nil {
		return fs.ErrNotFound
	}

	dstParent, dstName, err := traversePath(dstStart, dst) // ACQUIRE dstParent...
	if err != nil {
		return err
	}
	defer metadata.Dcache.Release(dstParent.ID()) // ... ensure it is RELEASED

	// Verify that dst is valid, independent of src
	if dstName == "." || dstName == ".." {
		return fs.ErrIsActive
	}
	// Verify that dst is not src (same directory, same name)
	if srcParent.ID() == dstParent.ID() && srcName == dstName {
		return fs.ErrIsActive
	}

	// If we are moving a directory to a new directory dst...
	if srcParent.ID() != dstParent.ID() && srcEntry.GetType() == fs.FileTypeDirectory {
		// ... Verify that dst is not a subdirectory of src.
		// Call this code as an anonymous function to make it easier to stack deferred calls to
		// RELEASE traversed directories.
		if err := func(srcEntry *direntry.Dirent, observedNode node.DirectoryNode) error {
			for !observedNode.IsRoot() {
				if observedNode.StartCluster() == srcEntry.Cluster {
					// Cannot rename src into a subdirectory of itself
					return fs.ErrInvalidArgs
				}
				parentEntry, _, err := node.Lookup(observedNode, "..")
				if err != nil {
					return err
				}
				// ACQUIRE a new observed node...
				observedNode, err = metadata.Dcache.CreateOrAcquire(metadata, parentEntry.Cluster, parentEntry.WriteTime)
				if err != nil {
					return err
				}
				defer metadata.Dcache.Release(observedNode.ID()) // ... ensure it is RELEASED
			}
			return nil
		}(srcEntry, dstParent); err != nil {
			return err
		}
	}

	// Begin modification of the on-disk directory structures.
	// Since this process is tricky, if we have already begun modification but encountered an
	// unexpected error, the entire filesystem should be either (1) unmounted or (2) altered to
	// readonly mode.

	// Does the destination already exist?
	dstEntry, dstDirentryIndex, err := node.Lookup(dstParent, dstName)
	if err != nil {
		return err
	}
	if dstEntry == nil {
		// Destination DOES NOT exist. Create an updated srcEntry (new name)
		newSrcEntry := direntry.New(dstName, srcEntry.Cluster, srcEntry.GetType())
		newSrcEntry.Size = srcEntry.Size
		srcEntry = newSrcEntry

		// Add src to new dst
		dstDirentryIndex, err = node.Allocate(dstParent, srcEntry)
		if err != nil {
			return err
		}
	} else {
		// Destination DOES exist. We should unlink and replace it.
		if dstEntry.GetType() != srcEntry.GetType() {
			return fs.ErrNotADir
		} else if _, err := ensureCanUnlink(dstParent, dstName); err != nil {
			return err
		}
		oldCluster, err := doReplace(dstParent, dstDirentryIndex, srcEntry.Cluster, srcEntry.WriteTime, srcEntry.Size)
		if err != nil {
			return err
		}
		// Even if we struggle to free the old cluster, we've already replaced the destination
		// direntry.
		metadata.ClusterMgr.ClusterDelete(oldCluster)
	}

	if (srcEntry.GetType() == fs.FileTypeDirectory) && (srcParent != dstParent) {
		// If src is a directory, and it is moving to a new parent, update src's ".."
		// ACQUIRE the srcEntry...
		srcNode, err := metadata.Dcache.CreateOrAcquire(metadata, srcEntry.Cluster, srcEntry.WriteTime)
		if err != nil {
			panic(err)
		} else if err := node.WriteDotAndDotDot(srcNode, srcEntry.Cluster, dstParent.StartCluster()); err != nil {
			panic(err)
		}
		metadata.Dcache.Release(srcNode.ID()) // ... ensure it is RELEASED
	}

	// Remove src from the srcParent
	if _, err = node.Free(srcParent, srcDirentryIndex); err != nil {
		panic(err)
	} else if srcNode, ok := srcParent.ChildFile(srcDirentryIndex); ok {
		// If the source is an open file, relocate it.
		srcNode.MoveFile(dstParent, dstDirentryIndex)
		// ACQUIRE the new parent directory
		metadata.Dcache.Acquire(dstParent.ID())
		// RELEASE the old parent directory
		metadata.Dcache.Release(srcParent.ID())
	}

	return nil
}

func syncFile(n node.FileNode) error {
	doClose := false
	flushFile(n, doClose)
	n.Metadata().Dev.Flush()
	return nil
}

func syncDirectory(n node.DirectoryNode) error {
	n.Metadata().Dev.Flush()
	return nil
}

func unlink(n node.DirectoryNode, target string) error {
	parent, name, err := traversePath(n, target) // ACQUIRE parent...
	if err != nil {
		return err
	}
	defer n.Metadata().Dcache.Release(parent.ID()) // ... ensure it is RELEASED

	// Use an anonymous function for the duration of holding a lock on the parent.
	cluster, err := func() (uint32, error) {
		parent.Lock()
		defer parent.Unlock()
		// Ensure that we are not attempting to unlink a non-empty directory
		direntryIndex, err := ensureCanUnlink(parent, name)
		if err != nil {
			return 0, err
		}
		// The target dirent exists, and is in an unlinkable state.
		cluster, err := doUnlink(parent, direntryIndex)
		if err != nil {
			return 0, err
		}
		return cluster, nil
	}()

	if err != nil {
		return err
	}
	return parent.Metadata().ClusterMgr.ClusterDelete(cluster)
}

// Sync the file with its parent directory. If requested, decrease the number of references to the
// file.
//
// Precondition:
//	 - the node lock is not held by the caller
//	 - the parent lock is not held by the caller
// Postcontiion:
//	 - same as precondition
func flushFile(n node.FileNode, doClose bool) (err error) {
	// Lock parent (if not nil) then node
	parent, direntIndex := n.LockParent()
	n.Lock()

	if parent != nil {
		// If this node has a direntry which can be updated, update it
		if !n.Metadata().Readonly {
			if _, err = node.Update(parent, n.StartCluster(), n.MTime(), uint32(n.Size()), direntIndex); err != nil {
				panic(err)
			}
		}
		if doClose {
			// We are about to "ref down" the child. If it is the LAST child, remove it from the parent.
			if n.RefCount() == 1 {
				parent.RemoveFile(direntIndex)
			}

			// RELEASE the parent directory from the access acquired when opening
			parent.Metadata().Dcache.Release(parent.ID())
		}

		// Unlock the parent after updating the dirent, but before possibly deleting the child's
		// clusters. This is fine because (1) the parent is not updated beyond this point, and (2)
		// the lock acquisition order is preserved, so deadlock is prevented.
		parent.Unlock()
	}

	if doClose {
		err = n.RefDown(1)
	}
	n.Unlock()
	return err
}

// Ensure that we are not attempting to unlink a non-empty directory by looking up the name of a
// file/directory in the parent. Also returns the direntryIndex of the child.
//
// Precondition:
//	 - parent is locked
//	 - the node lock corresponding to 'name' is not held by the caller
// Postcondition:
//	 - parent is locked
func ensureCanUnlink(parent node.DirectoryNode, name string) (int, error) {
	if name == "." || name == ".." {
		return 0, fs.ErrIsActive
	}

	entry, direntryIndex, err := node.Lookup(parent, name)
	if err != nil {
		return 0, err
	} else if entry == nil {
		return 0, fs.ErrNotFound
	}

	if entry.GetType() == fs.FileTypeDirectory {
		// ACQUIRE openedDir...
		openedDir, err := traverseDirectory(parent, name, fs.OpenFlagRead|fs.OpenFlagDirectory)
		if err != nil {
			return 0, err
		}
		defer parent.Metadata().Dcache.Release(openedDir.ID()) // ... ensure it is RELEASED

		openedDir.RLock()
		defer openedDir.RUnlock()
		if openedDir.RefCount() > 0 {
			// Cannot delete open directories. As long as the parent remains locked until 'unlink'
			// is complete, the child directory cannot be opened.
			return 0, fs.ErrIsActive
		} else if empty, err := node.IsEmpty(openedDir); err != nil {
			return 0, err
		} else if !empty {
			// Cannot delete non-empty directories. As long as the parent remains locked until
			// 'unlink' is complete, the child directory cannot become non-empty (it's closed).
			return 0, fs.ErrNotEmpty
		}
	}
	return direntryIndex, nil
}

// Update the in-memory connection between a parent and a child to signify that the child should be
// removed from the parent and deleted.
// Does not modify direntries -- exclusively in-memory.
// Returns true if child file is open.
//
// Precondition:
//	 - parent is locked
//	 - child lock is not held by the caller
// Postcondition:
//	 - parent is locked
func nodeDeleteChild(parent node.DirectoryNode, direntryIndex int) bool {
	if child, ok := parent.ChildFile(direntryIndex); ok {
		// RELEASE the parent directory from the access acquired when opening
		parent.Metadata().Dcache.Release(parent.ID())
		child.Lock()
		defer child.Unlock()
		parent.RemoveFile(direntryIndex)
		child.MarkDeleted()
		child.RefDown(0)
		return true
	}
	return false
}

// Replace the direntry at index "direntryIndex" with the node "replacement".
// Returns the replaced cluster which should be deleted, if any.
//
// Precondition:
//	 - parent is locked
//	 - the node lock corresponding to 'direntryIndex' is not held by the caller
// Postcondition:
//	 - parent is locked
//	 - the dirent corresponding to 'direntryIndex' is replaced with an entry for 'replacement'
//	 - the node corresponding to 'direntryIndex' is marked as deleted (if open)
func doReplace(parent node.DirectoryNode, direntryIndex int, cluster uint32, writeTime time.Time, size uint32) (uint32, error) {
	if oldCluster, err := node.Update(parent, cluster, writeTime, size, direntryIndex); err != nil {
		return 0, err
	} else if childOpen := nodeDeleteChild(parent, direntryIndex); !childOpen {
		// Return the child's cluster for deleting if it is not open
		return oldCluster, nil
	}
	return 0, nil // The cluster will be deleted when the child file is closed
}

// Remove the direntry at index "direntryIndex" in the parent node.
// Returns the cluster which should be deleted.
//
// Precondition:
//	 - parent is locked
//	 - the node lock corresponding to 'direntryIndex' is not held by the caller
// Postcondition:
//	 - parent is locked
//	 - dirent is removed from the directory
//	 - the node corresponding to 'direntryIndex' is marked as deleted (if open)
func doUnlink(parent node.DirectoryNode, direntryIndex int) (uint32, error) {
	// Remove the dirent from the directory. The node is not yet deleted.
	if entry, err := node.Free(parent, direntryIndex); err != nil {
		return 0, err
	} else if childOpen := nodeDeleteChild(parent, direntryIndex); !childOpen {
		// Return the child's cluster for deleting if it is not open
		return entry.Cluster, nil
	}
	return 0, nil // The cluster will be deleted when the child file is closed
}

// Given a direntry and a parent directory, create a node representing the child.
//
// Precondition:
//	 - parent is locked
// Postcondition:
//	 - parent is locked
//	 - If successful, the parent node is ACQUIRED (ref++) in the dcache
func openFileFromDirent(parent node.DirectoryNode, index int, dirent *direntry.Dirent) (node.FileNode, error) {
	// Look up the file node in the local map of children
	if child, ok := parent.ChildFile(index); ok {
		parent.Metadata().Dcache.Acquire(parent.ID())
		return child, nil
	}

	// Child is not open. Add to parent.
	child, err := node.NewFile(parent.Metadata(), parent, index, dirent.Cluster, dirent.WriteTime)
	if err != nil {
		return nil, err
	}
	// Set the size exactly -- it is recorded in the direntry referring to this file
	child.SetSize(int64(dirent.Size))

	// ACQUIRE dcache access to the parent node, since we need it when we update the file's
	// direntry. This access is RELEASED in the following circumstances:
	// 1) File is unlinked
	// 2) File is closed
	// 3) File is renamed
	parent.Metadata().Dcache.Acquire(parent.ID())
	return child, nil
}

// Opens a single node WITHOUT path resolution.
//
// Precondition:
//	 - parent is locked
// Postcontidion:
//	 - parent is locked
//	 - If the opened node is a directory, it is ACQUIRED in the dcache
func openIncremental(parent node.DirectoryNode, name string, flags fs.OpenFlags) (node.Node, error) {
	if name == "." || name == ".." {
		return traverseDirectory(parent, name, flags)
	}
	// Check that the path already exists with the requested name
	entry, direntryIndex, err := lookupAndCheck(parent, name, flags)
	if err != nil {
		return nil, err
	} else if entry.GetType() == fs.FileTypeDirectory {
		return parent.Metadata().Dcache.CreateOrAcquire(parent.Metadata(), entry.Cluster, entry.WriteTime)
	}
	return openFileFromDirent(parent, direntryIndex, entry)
}

// Creates a single node WITHOUT path resolution.
//
// Precondition:
//	 - parent is locked
// Postcondition:
//	 - parent is locked
//	 - If the created node is a directory, it is ACQUIRED in the dcache
func createIncremental(parent node.DirectoryNode, name string, flags fs.OpenFlags) (node.Node, error) {
	// File / Directory does not exist...
	if !flags.Create() {
		return nil, fs.ErrNotFound // If we aren't creating anything, we didn't find our file
	} else if !flags.Write() {
		return nil, fs.ErrPermission // Creating a file requires write permssions
	} else if flags.File() == flags.Directory() {
		return nil, fs.ErrInvalidArgs // We must know if we're creating a file or directory
	} else if name == "." || name == ".." {
		return nil, fs.ErrInvalidArgs
	}
	// ... but the flags identify we should create it.

	// TODO(smklein): Should we be checking readonly permissions here? The cluster manager also has
	// a readonly flag, and will fail appropriately, but still. If anything, shouldn't "Readonly" be
	// applied at a block shim layer? That would make it easier to "flip on readonly" in case of
	// tragic error.

	if flags.File() {
		entry := direntry.New(name, 0, fs.FileTypeRegularFile)
		direntryIndex, err := node.Allocate(parent, entry)
		if err != nil {
			return nil, err
		}
		child, err := openFileFromDirent(parent, direntryIndex, entry)
		if err != nil {
			panic(err)
		}
		return child, nil
	}

	if flags.Append() || flags.Truncate() {
		return nil, fs.ErrNotAFile // Reject non-directory flags
	} else if !flags.Read() {
		return nil, fs.ErrPermission // Directories require read permission
	}

	// Create / Open the child directory -- it's in a weird state right now, since it's totally empty.
	newCluster, err := parent.Metadata().ClusterMgr.ClusterExtend(0)
	if err != nil {
		return nil, err
	}

	// Add the new directory's direntry to the parent directory.
	entry := direntry.New(name, newCluster, fs.FileTypeDirectory)
	_, err = node.Allocate(parent, entry)
	if err != nil {
		parent.Metadata().ClusterMgr.ClusterDelete(newCluster) // Free the cluster we just allocated.
		return nil, err
	}

	// Create the child directory and ACQUIRE it from the dcache.
	child, err := parent.Metadata().Dcache.CreateOrAcquire(parent.Metadata(), newCluster, time.Now())
	if err != nil {
		panic("Unable to create new directory after allocating cluster")
	}

	child.Lock()
	defer child.Unlock()

	// "The dotdot entry points to the starting cluster of the parent directory..."
	parentCluster := parent.StartCluster()
	if parent.IsRoot() {
		// "... which is 0 if this directory's parent is the root directory"
		// - FAT: General Overview of On-Disk Format, Page 25
		parentCluster = 0
	}

	// Initialize the new child directory
	if err := node.WriteDotAndDotDot(child, newCluster, parentCluster); err != nil {
		panic(err)
	} else if err := node.MakeEmpty(child); err != nil {
		panic(err)
	}

	return child, nil
}
