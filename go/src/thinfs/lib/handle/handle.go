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

// Package handle provides a mechanism for passing Go pointers through
// C/C++ code.  Go does not allow programs to pass Go pointers to C++ code
// directly (see https://golang.org/cmd/cgo/#hdr-Passing_pointers).  Code
// that needs to pass Go pointers through C/C++ code back to Go code can use
// this package to associate a unique handle with the Go pointer and then
// pass the handle through the C/C++ code.  It is safe to call the
// functions in this package concurrently via multiple goroutines.
package handle

import (
	"errors"
	"sync"

	"github.com/golang/glog"
)

var tab = struct {
	sync.Mutex
	m    map[uintptr]interface{} // All valid handles.
	next uintptr                 // Next handle to use.
}{
	m:    make(map[uintptr]interface{}),
	next: uintptr(1607),
}

// ErrInvalid is returned if a caller calls a function with an invalid handle.
var ErrInvalid = errors.New("invalid handle")

// New returns a new handle for v.
func New(v interface{}) uintptr {
	tab.Lock()
	h := tab.next
	tab.m[h] = v
	tab.next++
	tab.Unlock()

	return h
}

// Delete removes a handle.  Returns ErrInvalid if h is not a valid handle.
func Delete(h uintptr) error {
	var err error

	tab.Lock()
	if _, ok := tab.m[h]; !ok {
		err = ErrInvalid
	} else {
		delete(tab.m, h)
	}
	tab.Unlock()

	return err
}

// MustDelete removes a handle and logs a fatal error if it cannot do so.
func MustDelete(h uintptr) {
	if err := Delete(h); err != nil {
		glog.Fatalf("Error deleting handle %#x: %v\n", h, err)
	}
}

// Value returns the value associated with h.  If h is not a valid handle, Value
// returns nil and ErrInvalid.
func Value(h uintptr) (interface{}, error) {
	var err error

	tab.Lock()
	v, ok := tab.m[h]
	if !ok {
		err = ErrInvalid
	}
	tab.Unlock()

	return v, err
}

// MustValue returns the value associated with h.  It logs a fatal error if h is
// not a valid handle.
func MustValue(h uintptr) interface{} {
	v, err := Value(h)
	if err != nil {
		glog.Fatalf("Error fetching value for handle %#x: %v\n", h, err)
	}

	return v
}
