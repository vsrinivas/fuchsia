// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package cluster

import (
	"math/rand"
	"sync"
	"testing"
	"time"

	"fuchsia.googlesource.com/thinfs/lib/fs/msdosfs/bootrecord"
	"fuchsia.googlesource.com/thinfs/lib/fs/msdosfs/testutil"
)

// Without accessing any clusters, open and close the cluster manager.
func TestMountUnmount(t *testing.T) {
	fs := testutil.MkfsFAT(t, "1G", 2, 0, 4, 512)
	defer fs.RmfsFAT()
	dev := fs.GetDevice()
	defer dev.Close()

	br, err := bootrecord.New(dev)
	if err != nil {
		t.Fatal(err)
	}

	mgr, err := Mount(dev, br, false)
	if err != nil {
		t.Fatal(err)
	}

	err = mgr.Unmount()
	if err != nil {
		t.Fatal(err)
	}
}

// Utility to quickly allocate a cluster chain and return the collected clusters.
func checkAllocateChain(t *testing.T, mgr *Manager, size uint32) []uint32 {
	var err error
	clusters := make([]uint32, size)
	clusters[0], err = mgr.ClusterExtend(0)
	if err != nil {
		t.Fatal(err)
	}
	for i := 1; i < len(clusters); i++ {
		clusters[i], err = mgr.ClusterExtend(clusters[i-1])
		if err != nil {
			t.Fatal(err)
		}
	}
	return clusters
}

// Utility to quickly verify the contents of a cluster chain.
func checkClusterChain(t *testing.T, mgr *Manager, start uint32, goldChain []uint32) {
	// Verify the clusters we allocated
	collectedClusters, err := mgr.ClusterCollect(start)
	if err != nil {
		t.Fatal(err)
	} else if len(collectedClusters) != len(goldChain) {
		t.Fatalf("Unexpected ClusterCollect length %d", len(collectedClusters))
	}
	for i := range goldChain {
		if goldChain[i] != collectedClusters[i] {
			t.Fatalf("Expected collected clusters[%d] to be %d, but it was %d",
				i, goldChain[i], collectedClusters[i])
		}
	}
}

// Test basic operations on clusters.
func TestClusterOperationsBasic(t *testing.T) {
	fs := testutil.MkfsFAT(t, "1G", 2, 0, 4, 512)
	defer fs.RmfsFAT()
	dev := fs.GetDevice()
	defer dev.Close()

	br, err := bootrecord.New(dev)
	if err != nil {
		t.Fatal(err)
	}

	mgr, err := Mount(dev, br, false)
	if err != nil {
		t.Fatal(err)
	}

	// Allocate some clusters
	clusters := checkAllocateChain(t, mgr, 5)

	// Verify the clusters we allocated
	checkClusterChain(t, mgr, clusters[0], clusters)

	// Truncate the cluster chain
	newLen := 3
	err = mgr.ClusterTruncate(clusters[newLen-1])
	if err != nil {
		t.Fatal(err)
	}
	collectedClusters, err := mgr.ClusterCollect(clusters[0])
	if err != nil {
		t.Fatal(err)
	} else if len(collectedClusters) != newLen {
		t.Fatalf("Unexpected ClusterCollect length %d", len(collectedClusters))
	}
	for i := 0; i < len(collectedClusters)-1; i++ {
		if clusters[i] != collectedClusters[i] {
			t.Fatalf("Expected collected clusters[%d] to be %d, but it was %d",
				i, clusters[i], collectedClusters[i])
		}
	}
	finalCluster := collectedClusters[len(collectedClusters)-1]
	val, err := mgr.fat.Get(finalCluster)
	if err != nil {
		t.Fatal(err)
	} else if val != mgr.ClusterEOF() {
		t.Fatalf("Truncated chain at %d to be EOF, but it was %d", finalCluster, val)
	}

	// Delete the rest of the cluster chain
	err = mgr.ClusterDelete(clusters[0])
	if err != nil {
		t.Fatal(err)
	}

	// Check that all values of the cluster have been deleted
	for i := range clusters {
		val, err := mgr.fat.Get(clusters[i])
		if err != nil {
			t.Fatalf("Cannot read FAT cluster %d", clusters[i])
		} else if !mgr.fat.IsFree(val) {
			t.Fatalf("Expected cluster %d to be free, but it was %d", clusters[i], val)
		}
	}

	err = mgr.Unmount()
	if err != nil {
		t.Fatal(err)
	}
}

// Mount, allocate some clusters, unmount, and remount as readonly.
// This test shows the limited capabilities of read-only access.
func TestClusterManagerReadonly(t *testing.T) {
	fs := testutil.MkfsFAT(t, "1G", 2, 0, 4, 512)
	defer fs.RmfsFAT()
	dev := fs.GetDevice()
	defer dev.Close()

	br, err := bootrecord.New(dev)
	if err != nil {
		t.Fatal(err)
	}

	mgr, err := Mount(dev, br, false)
	if err != nil {
		t.Fatal(err)
	}

	// Allocate some clusters and unmount.
	clusters := checkAllocateChain(t, mgr, 5)
	checkClusterChain(t, mgr, clusters[0], clusters)
	err = mgr.Unmount()
	if err != nil {
		t.Fatal(err)
	}

	// Remount as readonly
	mgr, err = Mount(dev, br, true)
	if err != nil {
		t.Fatal(err)
	}

	// Demonstrate that we can collect the clusters
	checkClusterChain(t, mgr, clusters[0], clusters)

	// Demonstrate that we CANNOT modify clusters
	if _, err := mgr.ClusterExtend(clusters[4]); err == nil {
		t.Fatal("Expected that adding clusters while in readonly mode would cause an error")
	} else if err := mgr.ClusterDelete(clusters[0]); err == nil {
		t.Fatal("Expected that deleting clusters while in readonly mode would cause an error")
	} else if err := mgr.ClusterTruncate(clusters[0]); err == nil {
		t.Fatal("Expected that truncating clusters while in readonly mode would cause an error")
	}

	// For good measure, collect the clusters again (it would be bad if we managed to change the
	// FAT on one of the previous error cases).
	checkClusterChain(t, mgr, clusters[0], clusters)

	err = mgr.Unmount()
	if err != nil {
		t.Fatal(err)
	}
}

// Test some invalid cluster operations.
func TestClusterOperationsInvalid(t *testing.T) {
	fs := testutil.MkfsFAT(t, "1G", 2, 0, 4, 512)
	defer fs.RmfsFAT()
	dev := fs.GetDevice()
	defer dev.Close()

	br, err := bootrecord.New(dev)
	if err != nil {
		t.Fatal(err)
	}

	mgr, err := Mount(dev, br, false)
	if err != nil {
		t.Fatal(err)
	}

	// Allocate some clusters
	clusters := make([]uint32, 5)
	clusters[0], err = mgr.ClusterExtend(0)
	if err != nil {
		t.Fatal(err)
	}
	for i := 1; i < len(clusters); i++ {
		clusters[i], err = mgr.ClusterExtend(clusters[i-1])
		if err != nil {
			t.Fatal(err)
		}
	}

	// Extend from the middle of a cluster chain
	_, err = mgr.ClusterExtend(clusters[1])
	if err == nil {
		t.Fatal("Expected that extending from the middle of a cluster chain would cause an error")
	}

	// Delete / Truncate using EOF / free cluster
	if err := mgr.ClusterDelete(mgr.fat.FreeValue()); err != nil {
		t.Fatal("Expected ClusterDelete(free) to be a no-op")
	} else if err := mgr.ClusterDelete(mgr.fat.EOFValue()); err != nil {
		t.Fatal("Expected ClusterDelete(EOF) to be a no-op")
	} else if err := mgr.ClusterTruncate(mgr.fat.FreeValue()); err != nil {
		t.Fatal("Expected ClusterTruncate(free) to be a no-op")
	} else if err := mgr.ClusterTruncate(mgr.fat.EOFValue()); err != nil {
		t.Fatal("Expected ClusterTruncate(EOF) to be a no-op")
	}

	err = mgr.Unmount()
	if err != nil {
		t.Fatal(err)
	}
}

// Test access to cluster chains which contains loops (one of those things that should never happen).
func TestClusterChainLoop(t *testing.T) {
	fs := testutil.MkfsFAT(t, "1G", 2, 0, 4, 512)
	defer fs.RmfsFAT()
	dev := fs.GetDevice()
	defer dev.Close()

	br, err := bootrecord.New(dev)
	if err != nil {
		t.Fatal(err)
	}

	mgr, err := Mount(dev, br, false)
	if err != nil {
		t.Fatal(err)
	}

	// Allocate some clusters
	clusters := make([]uint32, 5)
	clusters[0], err = mgr.ClusterExtend(0)
	if err != nil {
		t.Fatal(err)
	}
	for i := 1; i < len(clusters); i++ {
		clusters[i], err = mgr.ClusterExtend(clusters[i-1])
		if err != nil {
			t.Fatal(err)
		}
	}

	// Erroneously force the last cluster to point to the first one. This makes the chain invalid.
	if err := mgr.fat.Set(clusters[0], clusters[4]); err != nil {
		t.Fatal(err)
	}

	// Demonstrate that collecting the clusters results in an error (rather than an infinite loop).
	if _, err := mgr.ClusterCollect(clusters[0]); err == nil {
		t.Fatal("Expected that collecting a looped cluster would cause an error")
	}

	// Demonstrate that deleting the chain results in an error (does not end in EOF).
	if err := mgr.ClusterDelete(clusters[0]); err == nil {
		t.Fatal("Expected that deleting chain would cause an error")
	}

	err = mgr.Unmount()
	if err != nil {
		t.Fatal(err)
	}
}

// Test access to cluster manager after unmounting it. Demonstrate that no requests can actually
// access or modify the FAT.
func TestClusterUnmountedAccess(t *testing.T) {
	fs := testutil.MkfsFAT(t, "1G", 2, 0, 4, 512)
	defer fs.RmfsFAT()
	dev := fs.GetDevice()
	defer dev.Close()

	br, err := bootrecord.New(dev)
	if err != nil {
		t.Fatal(err)
	}

	mgr, err := Mount(dev, br, false)
	if err != nil {
		t.Fatal(err)
	}

	// Allocate some clusters and unmount.
	clusters := checkAllocateChain(t, mgr, 5)
	checkClusterChain(t, mgr, clusters[0], clusters)
	err = mgr.Unmount()
	if err != nil {
		t.Fatal(err)
	}

	// Demonstrate that we CANNOT access clusters after unmounting.
	if _, err := mgr.ClusterCollect(clusters[0]); err == nil {
		t.Fatal("Expected that accessing clusters while unmounted would cause an error")
	} else if _, err := mgr.ClusterExtend(clusters[4]); err == nil {
		t.Fatal("Expected that adding clusters while unmounted would cause an error")
	} else if err := mgr.ClusterDelete(clusters[0]); err == nil {
		t.Fatal("Expected that deleting clusters while unmounted would cause an error")
	} else if err := mgr.ClusterTruncate(clusters[0]); err == nil {
		t.Fatal("Expected that truncating clusters while unmounted would cause an error")
	}

	// Remount as readonly; verify the clusters have not changed since we unmounted.
	mgr, err = Mount(dev, br, true)
	if err != nil {
		t.Fatal(err)
	}
	checkClusterChain(t, mgr, clusters[0], clusters)

	err = mgr.Unmount()
	if err != nil {
		t.Fatal(err)
	}
}

// Test concurrent access to cluster manager by allocating and deleting cluster chains concurrently.
func TestClusterConcurrentAccess(t *testing.T) {
	fs := testutil.MkfsFAT(t, "1G", 2, 0, 4, 512)
	defer fs.RmfsFAT()
	dev := fs.GetDevice()
	defer dev.Close()

	br, err := bootrecord.New(dev)
	if err != nil {
		t.Fatal(err)
	}

	mgr, err := Mount(dev, br, false)
	if err != nil {
		t.Fatal(err)
	}

	// Access to the "chainStarts" slice is locked, but access to the cluster manager itself should
	// NOT BE LOCKED (in this test code).
	var chainStartsLock sync.Mutex
	var chainStarts []uint32

	numClustersPerThread := 100

	// Thread type 1: Allocate a large number of chains.
	allocator := func(c chan bool) {
		for i := 0; i < numClustersPerThread; i++ {
			clusters := checkAllocateChain(t, mgr, 5)
			chainStartsLock.Lock()
			chainStarts = append(chainStarts, clusters[0])
			chainStartsLock.Unlock()
		}

		c <- true
	}

	// Thread type 2: Delete a large number of chains (equal in count to those allocated).
	seed := time.Now().UnixNano()
	r := rand.New(rand.NewSource(seed))
	deleter := func(c chan bool) {
		for i := 0; i < numClustersPerThread; i++ {
			for {
				chainStartsLock.Lock()
				if len(chainStarts) == 0 {
					chainStartsLock.Unlock()
					continue
				}
				// Choose a random chain to remove.
				r := r.Intn(len(chainStarts))
				start := chainStarts[r]
				// Delete the "start" from chainStarts
				chainStarts[r] = chainStarts[len(chainStarts)-1]
				chainStarts = chainStarts[:len(chainStarts)-1]
				chainStartsLock.Unlock()

				if err := mgr.ClusterDelete(start); err != nil {
					t.Fatal(err)
				}
				break
			}
		}
		c <- true
	}

	// Thread type 3: Try collecting as many clusters as possible. Errors are likely, so ignore
	// them.
	collector := func(c chan bool) {
		for {
			select {
			case _ = <-c: // Exit once channel has been closed.
				return
			default:
				chainStartsLock.Lock()
				if len(chainStarts) == 0 {
					chainStartsLock.Unlock()
					continue
				}
				// Choose a random chain to collect.
				r := r.Intn(len(chainStarts))
				start := chainStarts[r]
				chainStartsLock.Unlock()
				// We have unlocked the chainStarts lock! "start" may be invalid!

				clusters, err := mgr.ClusterCollect(start)
				if err == nil && len(clusters) != 5 {
					t.Fatal("Collector thread read invalid cluster chain")
				}

			}
		}
	}

	finishChan := make(chan bool)
	closeChan := make(chan bool)
	numAllocatorDeleterPairs := 10

	for i := 0; i < numAllocatorDeleterPairs; i++ {
		go allocator(finishChan)
		go deleter(finishChan)
		go collector(closeChan)
	}

	// Wait for the allocator / deleters to complete.
	for i := 0; i < numAllocatorDeleterPairs*2; i++ {
		<-finishChan
	}

	// Inform the collectors they can stop.
	close(closeChan)

	err = mgr.Unmount()
	if err != nil {
		t.Fatal(err)
	}
}
