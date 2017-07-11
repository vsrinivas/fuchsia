// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package cluster is responsible for opening and using the FAT.
// The cluster manager is thread-safe.
package cluster

import (
	"errors"
	"sync"

	"github.com/golang/glog"

	"fuchsia.googlesource.com/thinfs/fs"
	"fuchsia.googlesource.com/thinfs/fs/msdosfs/bootrecord"
	"fuchsia.googlesource.com/thinfs/fs/msdosfs/cluster/fat"
	"fuchsia.googlesource.com/thinfs/thinio"
)

// Manager builds on top of the FAT, and can operate it on more complex ways.
type Manager struct {
	br       *bootrecord.Bootrecord
	readonly bool

	mu        sync.Mutex
	dev       *thinio.Conductor
	fat       *fat.FAT
	unmounted bool // "true" if the cluster manager has been unmounted, and all operations should fail immediately.
}

// Mount creates a new manager, which tracks access to the FAT.
func Mount(c *thinio.Conductor, br *bootrecord.Bootrecord, readonly bool) (*Manager, error) {
	// Load and validate the allocation table.
	fat, err := fat.Open(c, br, readonly)
	if err != nil {
		return nil, err
	}

	m := &Manager{
		dev:      c,
		br:       br,
		fat:      fat,
		readonly: readonly,
	}

	return m, nil
}

// Unmount removes the node manager and prevents the FAT from being referenced.
func (m *Manager) Unmount() error {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.unmounted = true
	return m.fat.Close()
}

// ClusterCollect reads all clusters starting from an initial cluster.
//
// If the starting cluster is EOF / free, an empty slice is returned.
// Returns an error if the cluster chain is malformed or cannot be read.
func (m *Manager) ClusterCollect(cluster uint32) ([]uint32, error) {
	glog.V(2).Infof("Cluster collect: %x\n", cluster)
	clusterSet := make(map[uint32]bool)
	var res []uint32
	m.mu.Lock()
	defer m.mu.Unlock()
	if m.unmounted {
		return nil, errors.New("FAT unmounted; cannot access clusters")
	}

	for !m.fat.IsEOF(cluster) && !m.fat.IsFree(cluster) {
		clusterSet[cluster] = true
		tail, err := m.fat.Get(cluster)
		if err != nil {
			return nil, err
		} else if m.fat.IsFree(tail) {
			return nil, errors.New("Malformed cluster chain does not point to EOF")
		} else if clusterSet[tail] {
			return nil, errors.New("Malformed cluster chain contains a loop")
		}

		res = append(res, cluster)
		cluster = tail
	}
	return res, nil
}

// ClusterExtend allocates a new cluster.
//
// If cluster is not an "EOF" value (or zero), the FAT's entry for cluster is updated to point to
// the NEW cluster.
// If cluster is an "EOF" (or zero) value, then a new cluster is allocated, but no old cluster is
// updated.
func (m *Manager) ClusterExtend(cluster uint32) (uint32, error) {
	glog.V(2).Infof("Cluster extend: %x\n", cluster)
	if m.readonly {
		return 0, fs.ErrReadOnly
	}

	extensionRequested := func(cluster uint32) bool {
		return cluster != 0 && !m.fat.IsEOF(cluster)
	}

	m.mu.Lock()
	defer m.mu.Unlock()
	if m.unmounted {
		return 0, errors.New("FAT unmounted; cannot access clusters")
	}

	if extensionRequested(cluster) {
		// Ensure the old cluster can be extended (if requested).
		tail, err := m.fat.Get(cluster)
		glog.V(2).Infof("  Tail was: %x\n", tail)
		if err != nil {
			return 0, err
		} else if m.fat.IsFree(tail) {
			return 0, errors.New("Trying to extend a free cluster")
		} else if !m.fat.IsEOF(tail) {
			return 0, errors.New("Trying to extend middle of cluster chain: would orphan the tail clusters")
		}
	}

	// Find a free cluster to use. It will automatically be pointing to EOF.
	newCluster, err := m.fat.Allocate()
	if err != nil {
		return 0, err
	}

	// Initialize the entirety of the new cluster to zero.
	deviceOffset := m.br.ClusterLocationData(newCluster)
	buf := make([]byte, m.br.ClusterSize())
	m.dev.WriteAt(buf, deviceOffset)

	glog.V(2).Infof("  Allocated new cluster at %x\n", newCluster)

	if extensionRequested(cluster) {
		// Set the old cluster to point to the new cluster (if requested).
		if err := m.fat.Set(newCluster, cluster); err != nil {
			// Try to free the newly allocated cluster in the case of an error.
			m.fat.Set(m.fat.FreeValue(), newCluster)
			return 0, err
		}
	}

	return newCluster, nil
}

// ClusterEOF returns the cluster that means "EOF".
func (m *Manager) ClusterEOF() uint32 {
	return m.fat.EOFValue()
}

// ClusterDelete removes all clusters chained together after "cluster". It also frees "cluster".
//
// It's okay to pass an EOF / Free cluster to delete. Nothing happens.
func (m *Manager) ClusterDelete(cluster uint32) error {
	glog.V(2).Info("Deleting from: ", cluster)
	return m.removeAllAfter(cluster, true)
}

// ClusterTruncate removes all clusters chained together after "cluster". It sets "cluster" to EOF.
//
// It's okay to pass an EOF / Free cluster to truncate. Nothing happens.
func (m *Manager) ClusterTruncate(cluster uint32) error {
	glog.V(2).Info("Truncating from: ", cluster)
	return m.removeAllAfter(cluster, false)
}

// removeAllAfter removes all clusters in a chain, starting with cluster "cluster".
func (m *Manager) removeAllAfter(cluster uint32, removeStart bool) error {
	if m.readonly {
		return errors.New("Cannot modify clusters in read-only filesystem")
	}

	if m.fat.IsEOF(cluster) || m.fat.IsFree(cluster) {
		// There is nothing to remove.
		return nil
	}

	m.mu.Lock()
	defer m.mu.Unlock()
	if m.unmounted {
		return errors.New("FAT unmounted; cannot access clusters")
	}

	// First, either free or remove the starting cluster's entry.
	tail, err := m.fat.Get(cluster)
	if err != nil {
		return err
	}

	var newVal uint32
	if removeStart {
		newVal = m.fat.FreeValue()
	} else {
		newVal = m.fat.EOFValue()
	}
	// Any error after this point will potentially orphan the tail, but will not cause issues for
	// the data up to and including "cluster".
	if err := m.fat.Set(newVal, cluster); err != nil {
		return err
	}
	cluster = tail

	for !m.fat.IsEOF(cluster) {
		tail, err := m.fat.Get(cluster)
		if err != nil {
			return err
		} else if m.fat.IsFree(tail) {
			return errors.New("Malformed cluster chain does not point to EOF")
		}

		if err := m.fat.Set(m.fat.FreeValue(), cluster); err != nil {
			return err
		}

		cluster = tail
	}
	return nil
}

// FreeCount returns the number of free clusters stored in fsinfo.
func (m *Manager) FreeCount() (int64, error) {
	return m.fat.FreeCount()
}
