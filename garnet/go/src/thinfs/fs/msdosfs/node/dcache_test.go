// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package node

import (
	"testing"
	"time"
)

func TestDcacheBasic(t *testing.T) {
	fileBackedFAT, metadata := setupFAT32(t, "1G", false)
	metadata.Init()

	// Allocate a single cluster (use it as an ID in the dcache) for a fake directory we'll call
	// "foo".
	fooID, err := metadata.ClusterMgr.ClusterExtend(0)
	if err != nil {
		t.Fatal(err)
	}

	// This initializes the in-memory directory for "foo"
	d, err := metadata.Dcache.CreateOrAcquire(metadata, fooID, time.Now()) // Refs: 1
	if err != nil {
		t.Fatal(err)
	}
	metadata.Dcache.Acquire(fooID)                                          // Refs: 2
	metadata.Dcache.Release(fooID)                                          // Refs: 1
	d2, err := metadata.Dcache.CreateOrAcquire(metadata, fooID, time.Now()) // Refs: 2
	if err != nil {
		t.Fatal(err)
	} else if d != d2 {
		t.Fatal("Created new directory node when one should have been available in the dcache")
	}
	metadata.Dcache.Release(fooID) // Refs: 1
	// Once we release the number of references of "foo" to zero, it is no longer in the dcache...
	metadata.Dcache.Release(fooID) // Refs: 0
	// ... Therefore, a call to "CreateOrAcquire" should create a new DirectoryNode
	d2, err = metadata.Dcache.CreateOrAcquire(metadata, fooID, time.Now()) // Refs: 1
	if err != nil {
		t.Fatal(err)
	} else if d == d2 {
		t.Fatal("Should have created new directory in dcache, but did not")
	}
	metadata.Dcache.Release(fooID)   // Refs: 0
	metadata.Dcache.Insert(fooID, d) // Refs: 1 (only allowed since old version of d has been removed from cache)
	if len(metadata.Dcache.AllEntries()) != 1 {
		t.Fatal("Unexpected number of entries in dcache")
	}
	metadata.Dcache.Release(fooID) // Refs: 0
	if len(metadata.Dcache.AllEntries()) != 0 {
		t.Fatal("Unexpected number of entries in dcache")
	}

	cleanup(fileBackedFAT, metadata)
}
