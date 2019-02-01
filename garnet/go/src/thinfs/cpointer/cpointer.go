// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package cpointer provides a mechanism for passing Go pointers through
// C/C++ code.  Go does not allow programs to pass Go pointers to C++ code
// directly (see https://golang.org/cmd/cgo/#hdr-Passing_pointers).  Code
// that needs to pass Go pointers through C/C++ code back to Go code can use
// this package to associate a unique C pointer with the Go pointer and then
// pass the C pointer through the C/C++ code.  It is safe to call the
// functions in this package concurrently via multiple goroutines.
package cpointer

import (
	"errors"
	"sync"

	"github.com/golang/glog"
)

var tab = struct {
	sync.Mutex
	m    map[uintptr]interface{} // All valid C pointers.
	next uintptr                 // Next C pointer to use.
}{
	m:    make(map[uintptr]interface{}),
	next: uintptr(1607),
}

// ErrInvalid is returned if a caller calls a function with an invalid C pointer.
var ErrInvalid = errors.New("invalid C pointer")

// New allocates a C pointer, associates it with a Go value, and returns the new "C pointer" which
// can be passed to C.
func New(v interface{}) uintptr {
	tab.Lock()
	p := tab.next
	tab.m[p] = v
	tab.next++
	tab.Unlock()

	return p
}

// Delete removes a C pointer.  Returns ErrInvalid if p is not a valid C pointer.
func Delete(p uintptr) error {
	var err error

	tab.Lock()
	if _, ok := tab.m[p]; !ok {
		err = ErrInvalid
	} else {
		delete(tab.m, p)
	}
	tab.Unlock()

	return err
}

// MustDelete removes a C pointer and logs a fatal error if it cannot do so.
func MustDelete(p uintptr) {
	if err := Delete(p); err != nil {
		glog.Fatalf("Error deleting c pointer %#x: %v\n", p, err)
	}
}

// Value returns the value associated with p.  If p is not a valid C pointer, Value
// returns nil and ErrInvalid.
func Value(p uintptr) (interface{}, error) {
	var err error

	tab.Lock()
	v, ok := tab.m[p]
	if !ok {
		err = ErrInvalid
	}
	tab.Unlock()

	return v, err
}

// MustValue returns the value associated with p.  It logs a fatal error if p is
// not a valid C pointer.
func MustValue(p uintptr) interface{} {
	v, err := Value(p)
	if err != nil {
		glog.Fatalf("Error fetching value for C pointer %#x: %v\n", p, err)
	}

	return v
}
