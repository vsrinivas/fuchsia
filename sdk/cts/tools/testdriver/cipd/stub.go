// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Stub the CIPD interface, using default values.
// For testing.

package testdriver

type Stub struct {
}

func NewStub() *Stub {
	return &Stub{}
}

func (s *Stub) GetVersion(pkg string, tags []*Tag, refs []*Ref) (PkgInstance, error) {
	return PkgInstance{name: pkg, version: "123456789abcde"}, nil
}

func (s *Stub) Download(pkg PkgInstance, dest string) error {
	return nil
}
