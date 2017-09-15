// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fat

import (
	"sync"
	"testing"

	"thinfs/fs"
	"thinfs/fs/msdosfs/bootrecord"
	"thinfs/fs/msdosfs/testutil"
	"thinfs/thinio"
)

func setup(t *testing.T, size string) (*testutil.FileFAT, *thinio.Conductor, *bootrecord.Bootrecord) {
	fs := testutil.MkfsFAT(t, size, 2, 0, 4, 512)
	d := fs.GetDevice()

	// Read the bootrecord
	br, err := bootrecord.New(d)
	if err != nil {
		t.Fatal(err)
	}

	return fs, d, br
}

func shutdown(fs *testutil.FileFAT, d *thinio.Conductor) {
	fs.RmfsFAT()
	d.Close()
}

// Tests the FAT can be read and written to (FAT32 only)
func TestFATReadWrite(t *testing.T) {
	fs, d, br := setup(t, "1G")
	defer shutdown(fs, d)

	readonly := false
	fat, err := Open(d, br, readonly)
	if err != nil {
		t.Fatal(err)
	}

	// Check the root cluster
	rootCluster := br.RootCluster()
	if rootCluster != 2 {
		t.Fatalf("Expected root cluster to be 2, but it was %d", rootCluster)
	}

	entry, err := fat.Get(rootCluster)
	if err != nil {
		t.Fatal(err)
	} else if !fat.IsEOF(entry) {
		t.Fatalf("Expected entry at root cluster to be EOF, but instead received %x", entry)
	}

	// Find the next root cluster
	cluster, err := fat.Allocate()
	if err != nil {
		t.Fatal(err)
	} else if cluster != rootCluster+1 {
		t.Fatalf("Expected cluster after root to be free, but instead, free cluster found at %d", cluster)
	}

	// Set the free cluster to a "bad" value
	if err := fat.Set(cluster, cluster); err != ErrInvalidCluster {
		t.Fatal("Expected that setting cluster to itself would cause an error")
	} else if err := fat.Set(cluster, 0); err != ErrInvalidCluster {
		t.Fatal("Expected that setting cluster zero to any value would cause an error")
	}

	// "... it is feasible for 0x0FFFFFF7 to be an allocateable cluster number on FAT32 volumes. To
	// avoid possible confusion by disk utilities, no FAT32 volume should ever be configured such
	// that 0x0FFFFFF7 is an allocatable cluster number."
	badCluster := uint32(0x0FFFFFF7)
	if err := fat.Set(cluster, badCluster); err != ErrInvalidCluster {
		t.Fatalf("Expected setting cluster %d would cause an error", badCluster)
	}

	// Set the free cluster to a value and set the following cluster to EOF
	if err := fat.Set(cluster+1, cluster); err != nil {
		t.Fatal(err)
	} else if err := fat.Set(fat.EOFValue(), cluster+1); err != nil {
		t.Fatal(err)
	}

	// Try reading a bad value.
	if _, err := fat.Get(0); err != ErrInvalidCluster {
		t.Fatal(err)
	}
}

// Tests that improperly closing a filesystem without unloading the FAT causes it to become "dirty",
// which prevents it from being opened later.
func TestFatBadClose(t *testing.T) {
	doTest := func(size string) {
		fs, d, br := setup(t, size)
		defer shutdown(fs, d)

		// Make a new FAT.
		readonly := false
		_, err := Open(d, br, readonly)
		if err != nil {
			t.Fatal(err)
		}

		// Close the device -- WITHOUT closing the FAT!
		d.Close()

		// Open the filesystem (again)
		d = fs.GetDevice()
		defer d.Close()

		// Read the bootrecord (again)
		br, err = bootrecord.New(d)
		if err != nil {
			t.Fatal(err)
		}

		// Make a new FAT (again)
		readonly = false
		_, err = Open(d, br, readonly)
		if err != ErrDirtyFAT {
			t.Fatal("Expected FAT to be dirty; not closed properly")
		}
	}

	doTest("1G")
	doTest("10M")
}

// Tests that a FAT filesystem cannot be loaded if the hard error bit is set.
func TestFATHardError(t *testing.T) {
	doTest := func(size string) {
		fs, d, br := setup(t, size)
		defer shutdown(fs, d)

		// Make a new FAT.
		readonly := false
		fat, err := Open(d, br, readonly)
		if err != nil {
			t.Fatal(err)
		}

		// Set a hard error
		if err := fat.SetHardError(); err != nil {
			t.Fatal(err)
		}

		// Close the FAT.
		if err := fat.Close(); err != nil {
			t.Fatal(err)
		}

		// Re-open the FAT, show that we encounter a hard error.
		if _, err := Open(d, br, readonly); err != ErrHardIO {
			t.Fatal("Expected hard IO error when opening FAT marked with the hard error bit")
		}
	}

	doTest("1G")
	doTest("10M")
}

// Tests that improperly closing a filesystem without unloading the FAT doesn't cause errors if the
// filesystem was loaded as "read only".
func TestFatBadCloseReadonly(t *testing.T) {
	doTest := func(size string) {
		fs, d, br := setup(t, size)
		defer shutdown(fs, d)

		// Make a new readonly FAT.
		readonly := true
		_, err := Open(d, br, readonly)
		if err != nil {
			t.Fatal(err)
		}

		// Close the device -- WITHOUT closing the FAT!
		d.Close()

		// Open the filesystem (again).
		d = fs.GetDevice()
		defer d.Close()

		// Read the bootrecord (again)
		br, err = bootrecord.New(d)
		if err != nil {
			t.Fatal(err)
		}

		// Make a new FAT (again)
		readonly = false
		fat, err := Open(d, br, readonly)
		if err != nil {
			t.Fatal(err)
		}

		// Close the FAT correctly this time.
		if err := fat.Close(); err != nil {
			t.Fatal(err)
		}
	}

	doTest("1G")
	doTest("10M")
}

func checkedChainAllocate(t *testing.T, fat *FAT, size uint32) []uint32 {
	// Make a chain of clusters.
	clusterChain := make([]uint32, size)
	for i := range clusterChain {
		cluster, err := fat.Allocate()
		if err != nil {
			t.Fatal(err)
		}

		// These clusters will point to EOF when allocated.
		clusterChain[i] = cluster
	}

	for i := range clusterChain {
		if i != len(clusterChain)-1 {
			// Point to the next cluster.
			if err := fat.Set(clusterChain[i+1], clusterChain[i]); err != nil {
				t.Fatal(err)
			}
		} else {
			// Set the last cluster to EOF, not "free".
			if err := fat.Set(fat.EOFValue(), clusterChain[i]); err != nil {
				t.Fatal(err)
			}
		}
	}
	return clusterChain
}

func checkedChainVerify(t *testing.T, fat *FAT, clusterChain []uint32) {
	for i := range clusterChain {
		entry, err := fat.Get(clusterChain[i])
		if err != nil {
			t.Fatal(err)
		}
		if i < len(clusterChain)-1 {
			if entry != clusterChain[i+1] {
				t.Fatalf("Expected cluster at %d to point to %d, but pointed to %d",
					clusterChain[i], clusterChain[i+1], entry)
			}
		} else {
			if !fat.IsEOF(entry) {
				t.Fatalf("Expected cluster at %d to be EOF, but was %d", clusterChain[i], entry)
			}
		}
	}
}

// Test that the FAT can be closed and re-opened.
func TestFATWriteCloseReopenRead(t *testing.T) {
	doTest := func(size string) {
		fileSys, d, br := setup(t, size)
		defer shutdown(fileSys, d)

		// Make a new FAT.
		readonly := false
		fat, err := Open(d, br, readonly)
		if err != nil {
			t.Fatal(err)
		}

		// Make a chain of clusters.
		clusterChain := checkedChainAllocate(t, fat, 10)

		// Close the FAT.
		if err := fat.Close(); err != nil {
			t.Fatal(err)
		}

		// Reopen the FAT. Check that the clusters still hold the values we expect them to.
		readonly = true
		fat, err = Open(d, br, readonly)
		if err != nil {
			t.Fatal(err)
		}
		checkedChainVerify(t, fat, clusterChain)

		// Demonstrate that in readonly mode, we cannot modify the FAT.
		if err := fat.Set(fat.EOFValue(), clusterChain[0]); err != fs.ErrReadOnly {
			t.Fatal("Expected setting FAT to fail in readonly mode")
		} else if _, err := fat.Allocate(); err != fs.ErrReadOnly {
			t.Fatal("Expected allocating from FAT to fail in readonly mode")
		} else if err := fat.SetHardError(); err != fs.ErrReadOnly {
			t.Fatal("Expected setting hard error bit in FAT to fail in readonly mode")
		}

		if err := fat.Close(); err != nil {
			t.Fatal(err)
		}
	}

	doTest("1G")
	doTest("10M")
}

// Tests that the FAT can deal with running out of space.
func TestOutOfStorage(t *testing.T) {
	fs, d, br := setup(t, "10M")
	defer shutdown(fs, d)

	// Make a new FAT.
	readonly := false
	fat, err := Open(d, br, readonly)
	if err != nil {
		t.Fatal(err)
	}

	for {
		// Keep allocating until we run out of space
		_, err := fat.Allocate()
		if err == ErrNoSpace {
			// Test finished succesfully; we ran out of space
			return
		} else if err != nil {
			t.Fatal(err)
		}
	}
}

// Test that the freeCount and nextFree values are usable and can be updated.
func TestFATHints(t *testing.T) {
	fs, d, br := setup(t, "10G")
	defer shutdown(fs, d)

	// Make a new FAT.
	readonly := false
	fat, err := Open(d, br, readonly)
	if err != nil {
		t.Fatal(err)
	}

	freeCountBefore := fat.freeCount
	nextFreeBefore := fat.nextFree

	// Make a chain of clusters.
	numClustersAllocated := uint32(10)
	clusterChain := checkedChainAllocate(t, fat, numClustersAllocated)

	// Verify the hints have updated.
	if fat.freeCount != freeCountBefore-numClustersAllocated {
		t.Fatalf("Expected free count to be %d, but it was %d", freeCountBefore-numClustersAllocated, fat.freeCount)
	}
	if fat.nextFree != nextFreeBefore+numClustersAllocated {
		t.Fatalf("Expected next free to be %d, but it was %d", nextFreeBefore+numClustersAllocated, fat.nextFree)
	}

	// Free one of the clusters.
	if err := fat.Set(fat.FreeValue(), clusterChain[0]); err != nil {
		t.Fatal(err)
	}

	// Verify the free count has updated. The "nextFree" is a hint, and may or may not have changed.
	numClustersAllocated--
	if fat.freeCount != freeCountBefore-numClustersAllocated {
		t.Fatalf("Expected free count to be %d, but it was %d", freeCountBefore-numClustersAllocated, fat.freeCount)
	}

	if err := fat.Close(); err != nil {
		t.Fatal(err)
	}
}

// Test that the FAT is inaccessible after being closed.
func TestFATUseAfterClose(t *testing.T) {
	doTest := func(size string) {
		fs, d, br := setup(t, size)
		defer shutdown(fs, d)

		// Make a new FAT.
		readonly := false
		fat, err := Open(d, br, readonly)
		if err != nil {
			t.Fatal(err)
		}

		// Close the FAT.
		if err := fat.Close(); err != nil {
			t.Fatal(err)
		}

		// Demonstrate that functions which would access the FAT return errors.
		if _, err := fat.Allocate(); err != ErrNotOpen {
			t.Fatal("Should not be able to allocate in FAT after closing")
		} else if _, err := fat.Get(5); err != ErrNotOpen {
			t.Fatal("Should not be able to get from FAT after closing")
		} else if err := fat.Set(5, 6); err != ErrNotOpen {
			t.Fatal("Should not be able to set to FAT after closing")
		} else if err := fat.Close(); err != ErrNotOpen {
			t.Fatal("Should not be able double close FAT")
		} else if err := fat.SetHardError(); err != ErrNotOpen {
			t.Fatal("Should not be able to set error bits while FAT is closed")
		}
	}

	doTest("1G")
	doTest("10M")
}

// Test that FAT can function with a single writer and multiple readers.
func TestFATSingleWriterMultipleReaders(t *testing.T) {
	doTest := func(size string) {
		fs, d, br := setup(t, size)
		defer shutdown(fs, d)

		// Make a new FAT.
		readonly := false
		fat, err := Open(d, br, readonly)
		if err != nil {
			t.Fatal(err)
		}

		var fatLock sync.RWMutex
		freeCountBefore := fat.freeCount
		numAccesses := 1000

		cluster, err := fat.Allocate()
		if err != nil {
			t.Fatal(err)
		}

		writer := func(c chan bool) {
			// Alternate between allocating and freeing clusters.
			doAllocate := false
			var err error
			for i := 0; i < numAccesses; i++ {
				if doAllocate {
					fatLock.Lock()
					cluster, err = fat.Allocate()
					fatLock.Unlock()
				} else {
					fatLock.Lock()
					err = fat.Set(fat.FreeValue(), cluster)
					fatLock.Unlock()
				}

				if err != nil {
					t.Fatal(err)
				} else if doAllocate && fat.freeCount != freeCountBefore-1 {
					t.Fatal("Invalid free count; should have decreased")
				} else if !doAllocate && fat.freeCount != freeCountBefore {
					t.Fatal("Invalid free count; should have returned to original value")
				}

				doAllocate = !doAllocate
			}
			c <- true
		}

		reader := func(c chan bool) {
			// Read clusters (which should be either allocated and set to EOF or free).
			for i := 0; i < numAccesses; i++ {
				fatLock.RLock()
				sampledCluster := cluster
				val, err := fat.Get(sampledCluster)
				fatLock.RUnlock()

				if err != nil {
					t.Fatal("Invalid cluster: ", sampledCluster)
				} else if val != fat.EOFValue() && val != fat.FreeValue() {
					t.Fatalf("Unexpected value seen at cluster %d: %d", sampledCluster, val)
				}
			}
			c <- true
		}

		finishChan := make(chan bool)
		numWriters := 1
		numReaders := 3

		// Launch writers and readers
		for i := 0; i < numWriters; i++ {
			go writer(finishChan)
		}
		for i := 0; i < numReaders; i++ {
			go reader(finishChan)
		}

		// Wait for writers and readers ot finish
		for i := 0; i < numWriters+numReaders; i++ {
			<-finishChan
		}

		// Close the FAT.
		if err := fat.Close(); err != nil {
			t.Fatal(err)
		}
	}

	doTest("1G")
	doTest("10M")
}
