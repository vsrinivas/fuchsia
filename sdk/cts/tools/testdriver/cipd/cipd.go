// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testdriver

import "fmt"

// CIPD interface allows us to implement a CIPD client in several ways
// while keeping the implementation details hidden.
type CIPD interface {
	// GetVersion takes three arguments:
	//
	//    string:
	//      CIPD package name (e.g. "fuchsia/sdk/gn/linux-amd64").
	//
	//    []Tag:
	//      List of Tags to use when searching for the CIPD package.
	//
	//    []Ref:
	//      List of Refs to use when searching for the CIPD package.
	GetVersion(string, []*Tag, []*Ref) (PkgInstance, error)

	// Download takes 3 arguments:
	//
	//    PkgInstance:
	//      Info about this CIPD package instance.
	//      This struct is returned from a call to CIPD.GetVersion().
	//
	//    string:
	//      Version string (long CIPD string that is retrieved by the above method).
	//
	//    string:
	//      Destination on the local filesystem to download the package.
	Download(PkgInstance, string) error
}

// PkgInstance is a struct containing a CIPD package name and the CIPD version string.
// This is returned from CIPD.GetVersion, and provided to CIPD.Download.
//
// The internal fields are package-private, as end users should not be directly
// creating this struct.
type PkgInstance struct {
	name    string
	version string
}

// A Package instance can be marked with tags, where a tag is a colon-separated
// key-value pair.
//
// Specifying tags can be useful to ensure you are referencing
// the correct instance of the CIPD package.
//
// More info:
//   https://chromium.googlesource.com/infra/luci/luci-go/+/master/cipd/README.md#tags
type Tag struct {
	key string
	val string
}

func NewTag(key, val string) *Tag {
	return &Tag{
		key: key,
		val: val,
	}
}

func (t *Tag) String() string {
	return fmt.Sprintf("%s:%s", t.key, t.val)
}

// A package can have git-like refs, where a ref of a package points to one of
// the instances of the package by ID.
//
// Specifying tags can be useful to ensure you are referencing the correct
// instance of the CIPD package.
//
// More info:
//   https://chromium.googlesource.com/infra/luci/luci-go/+/master/cipd/README.md#refs
type Ref struct {
	ref string
}

func NewRef(ref string) *Ref {
	return &Ref{
		ref: ref,
	}
}
func (r *Ref) String() string {
	return r.ref
}
