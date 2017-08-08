// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package thinio provides functionality for orchestrating I/O operations on a block.Device.
package thinio

import (
	"errors"
	"fmt"
	"sync"
	"time"

	"fuchsia.googlesource.com/thinfs/block"
	"fuchsia.googlesource.com/thinfs/cache"
	"github.com/golang/glog"
)

// ErrClosed is returned if a caller attempts to perform any operations on a Conductor after
// it has been closed.
var ErrClosed = errors.New("conductor has been closed")

type errKind int

const (
	readErr errKind = iota
	writeErr
	flushErr
	closeErr
)

type devError struct {
	kind errKind
	off  int64
	data []byte
	err  error
}

func (de devError) Error() string {
	switch de.kind {
	case readErr:
		return fmt.Sprintf("failed to read block at offset %#x: %v", de.off, de.err)
	case writeErr:
		return fmt.Sprintf("failed to write block at offset %#x: %v", de.off, de.err)
	case flushErr:
		return fmt.Sprintf("failed to flush device: %v", de.err)
	case closeErr:
		return fmt.Sprintf("failed to close device: %v", de.err)
	default:
		return "unknown error"
	}
}

// device implements cache.BackingStore, converting cache.Keys to the corresponding
// block device offsets and performing any necessary I/O.
type device struct {
	dev block.Device

	// Tells the conductor that an error has occurred.
	recordError func(error)
}

// Get implements cache.BackingStore.Get for device.
func (d *device) Get(k cache.Key) cache.Value {
	off := k.(int64)
	if off%d.dev.BlockSize() != 0 {
		// panic because this indicates an internal error.
		panic(fmt.Sprintf("off (%#x) is not block size aligned\n", off))
	}
	if glog.V(2) {
		glog.Infof("Fetching block at offset %#x from the device\n", off)
	}

	var err error
	buf := make([]byte, d.dev.BlockSize())
	for dur := time.Millisecond; dur < 2*time.Second; dur *= 2 {
		_, err = d.dev.ReadAt(buf, off)
		if err == nil {
			return buf
		}

		glog.Warningf("Failed to read block at offset %#x from device: %v.  Retrying after %v\n", off, err, dur)
		time.Sleep(dur)
	}

	d.recordError(devError{
		kind: readErr,
		off:  off,
		err:  err,
	})
	return nil
}

// Put implements cache.BackingStore.Put for device.
func (d *device) Put(k cache.Key, v cache.Value) {
	off := k.(int64)
	if off%d.dev.BlockSize() != 0 {
		// panic because this indicates an internal error.
		panic(fmt.Sprintf("off (%#x) is not block size aligned\n", off))
	}
	if glog.V(2) {
		glog.Infof("Writing block at offset %#x to the device\n", off)
	}

	var err error
	buf := v.([]byte)
	for dur := time.Millisecond; dur < 2*time.Second; dur *= 2 {
		_, err = d.dev.WriteAt(buf, off)
		if err == nil {
			return
		}

		glog.Warningf("Failed to write block at offset %#x to device: %v.  Retrying after %v\n", off, err, dur)
		time.Sleep(dur)
	}

	d.recordError(devError{
		kind: writeErr,
		off:  off,
		data: buf,
		err:  err,
	})
}

// BlockSize is a convenience function for getting the block size of the underlying block.Device.
func (d *device) BlockSize() int64 {
	return d.dev.BlockSize()
}

// Discard is a convenience function for calling Discard on the underlying block.Device.
func (d *device) Discard(off, len int64) error {
	return d.dev.Discard(off, len)
}

// Flush is a convenience function for calling Flush on the underlying block.Device.
func (d *device) Flush() {
	// Yes the error *could* be recorded here, but I don't want it popping up on another random
	// function.
	// TODO(smklein): Redo the error handling in this file; it doesn't make much sense to defer
	// errors to unrelated functions.
	d.dev.Flush()
}

// Close is a convenience function for calling Close on the underlying block.Device.
func (d *device) Close() {
	if err := d.dev.Close(); err != nil {
		d.recordError(devError{
			kind: closeErr,
			err:  err,
		})
	}
}

// Path is a convenience function for calling Path on the underlying block.Device.
func (d *device) Path() string {
	return d.dev.Path()
}

// Conductor orchestrates all I/O operations on a block.Device, maintaining an internal cache
// of disk blocks for better performance
type Conductor struct {
	// TODO(smklein): This lock is too coarse-grained and prevents parallel access to the device.
	mu    sync.Mutex
	dev   *device
	cache *cache.C
	errs  []error // TODO(chirantan): recover from error?
}

// NewConductor returns a new, initialized Conductor for orchestrating I/O on dev.  cacheSize
// should be the size in bytes of the cache that the Conductor should use for caching disk blocks.
func NewConductor(dev block.Device, cacheSize int) *Conductor {
	numEntries := cacheSize / int(dev.BlockSize())
	store := &device{dev: dev}

	if glog.V(1) {
		glog.Infof("Creating Conductor with a cache of %v entries holding %v bytes\n", numEntries, cacheSize)
	}

	conductor := &Conductor{
		dev:   store,
		cache: cache.New(numEntries, store),
	}

	store.recordError = func(err error) {
		conductor.errs = append(conductor.errs, err)
	}

	return conductor
}

// DeviceSize returns the size of the underlying device in bytes.
func (c *Conductor) DeviceSize() int64 {
	return c.dev.dev.Size()
}

// ReadAt implements io.ReaderAt for Conductor.
func (c *Conductor) ReadAt(p []byte, off int64) (int, error) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if len(c.errs) > 0 {
		return 0, c.errs[0]
	}

	if glog.V(2) {
		glog.Infof("Reading %v bytes from offset %v\n", len(p), off)
	}

	var n int
	if rem := off % c.dev.BlockSize(); rem != 0 {
		addr := off - rem

		entry := c.cache.Get(addr)
		if len(c.errs) > 0 {
			return n, c.errs[0]
		}

		buf := entry.Value.([]byte)
		copied := copy(p, buf[rem:])
		n += copied
		p = p[copied:]
		off += int64(copied)
	}

	// Now we know that off is block size aligned.
	for len(p) > 0 {
		entry := c.cache.Get(off)
		if len(c.errs) > 0 {
			return n, c.errs[0]
		}

		buf := entry.Value.([]byte)
		copied := copy(p, buf)
		n += copied
		p = p[copied:]
		off += int64(copied)
	}

	return n, nil
}

// WriteAt implements io.WriterAt for Conductor.
func (c *Conductor) WriteAt(p []byte, off int64) (int, error) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if len(c.errs) > 0 {
		return 0, c.errs[0]
	}

	if glog.V(2) {
		glog.Infof("Writing %v bytes to offset %v\n", len(p), off)
	}

	var n int
	if rem := off % c.dev.BlockSize(); rem != 0 {
		addr := off - rem

		entry := c.cache.Get(addr)
		if len(c.errs) > 0 {
			return n, c.errs[0]
		}

		buf := entry.Value.([]byte)
		copied := copy(buf[rem:], p)
		entry.IsDirty = true
		n += copied
		p = p[copied:]
		off += int64(copied)
	}

	// Now we know that off is block size aligned.
	for int64(len(p)) >= c.dev.BlockSize() {
		buf := make([]byte, c.dev.BlockSize())
		copy(buf, p)

		c.cache.Put(off, buf)
		if len(c.errs) > 0 {
			return n, c.errs[0]
		}

		copied := len(buf)
		n += copied
		p = p[copied:]
		off += int64(copied)
	}

	if len(p) > 0 {
		entry := c.cache.Get(off)
		if len(c.errs) > 0 {
			return n, c.errs[0]
		}

		buf := entry.Value.([]byte)
		copied := copy(buf, p)
		entry.IsDirty = true
		n += copied
		p = p[copied:]
		off += int64(copied)
	}

	return n, nil
}

// Discard discards the data in the address range [off, off+size).  Returns an error, if any.
func (c *Conductor) Discard(off, size int64) error {
	c.mu.Lock()
	defer c.mu.Unlock()
	if len(c.errs) > 0 {
		return c.errs[0]
	}

	if glog.V(2) {
		glog.Infof("Discarding data in range [%v, %v)\n", off, off+size)
	}

	// A failure to discard is not considered a fatal error so it is not persisted in c.errs.
	return c.dev.Discard(off, size)
}

// Flush flushes the Conductor's in-memory copy of recently written data to the underlying
// block.Device.  Flush does not return until all previous writes are committed to stable
// storage. Returns an error, if any.
func (c *Conductor) Flush() error {
	c.mu.Lock()
	defer c.mu.Unlock()
	if len(c.errs) > 0 {
		return c.errs[0]
	}

	if glog.V(2) {
		glog.Info("Committing all data in Conductor to stable storage")
	}

	c.cache.Flush()
	if len(c.errs) > 0 {
		return c.errs[0]
	}

	c.dev.Flush()
	if len(c.errs) > 0 {
		return c.errs[0]
	}

	return nil
}

// Close closes the Conductor as well as the underlying block.Device, rendering them unusable
// for I/O.  Returns an error, if any. Future operations on the Conductor will return ErrClosed.
// Callers must call Flush before calling Close to ensure that all pending changes have been
// flushed to stable storage.
func (c *Conductor) Close() error {
	c.mu.Lock()
	defer c.mu.Unlock()
	if len(c.errs) > 0 {
		return c.errs[0]
	}

	c.dev.Close()
	if len(c.errs) > 0 {
		return c.errs[0]
	}

	c.errs = []error{ErrClosed}
	c.dev = nil

	return nil
}

// Errors returns a slice of all the errors that have occurred during the Conductor's
// operation, if any.  Callers must not modify the returned slice, which is invalidated
// if any other method is invoked on c.
func (c *Conductor) Errors() []error {
	c.mu.Lock()
	defer c.mu.Unlock()
	return c.errs
}

// Path returns the path to the underlying block device.
func (c *Conductor) Path() string {
	return c.dev.Path()
}
