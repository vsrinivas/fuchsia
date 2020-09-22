// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"fmt"
	"io"
	"io/ioutil"
	"path"
	"testing"
)

type mockBuild struct {
	paths            map[string]string
	brokenSymbolizer bool
}

func newMockBuild() (Build, error) {
	build := &mockBuild{}
	build.Prepare()
	return build, nil
}

func (b *mockBuild) Prepare() error {
	// Set up some mock paths
	// Exceptions where tools aren't named the same as their key:
	b.paths = map[string]string{
		"zbitool": "/path/to/zbi",
	}
	for _, k := range []string{"blk", "fvm", "zbi", "blk", "kernel",
		"symbolize", "llvm-symbolizer", "fuzzers.json", "authkeys", "sshid"} {
		b.paths[k] = fmt.Sprintf("/path/to/%s", k)
	}
	// Note: qemu is a special case because it needs to be a real tempfile,
	// so we rely on the test to handle this. See enableQemu() below.
	return nil
}

const (
	FakeQemuNormal  = "qemu-system-x86_64"
	FakeQemuFailing = "failing-qemu"
	FakeQemuSlow    = "slow-qemu"
)

// The qemu library stats the binary so we need to make a real file.
// The caller is responsible for cleaning up the returned directory.
// fakeType should be one of the FakeQemu* constants defined above
func (b *mockBuild) enableQemu(t *testing.T, fakeType string) (tmpDir string) {
	tmpDir = getTempdir(t)

	qemuPath := path.Join(tmpDir, fakeType)

	if err := ioutil.WriteFile(qemuPath, nil, 0644); err != nil {
		t.Fatalf("error writing local file: %s", err)
	}

	b.paths["qemu"] = qemuPath
	return
}

func (b *mockBuild) Fuzzer(name string) (*Fuzzer, error) {
	switch name {
	case "foo/bar":
		return NewFuzzer(b, "foo", "bar"), nil
	case "fail/nopid":
		return NewFuzzer(b, "fail", "nopid"), nil
	case "fail/notfound":
		return NewFuzzer(b, "fail", "notfound"), nil
	default:
		return nil, fmt.Errorf("invalid fuzzer name %q", name)
	}
}

func (b *mockBuild) Path(keys ...string) ([]string, error) {
	paths := make([]string, len(keys))
	for i, k := range keys {
		if path, found := b.paths[k]; found {
			paths[i] = path
		} else {
			return nil, fmt.Errorf("invalid path key %q", k)
		}
	}
	return paths, nil
}

func (b *mockBuild) Symbolize(in io.Reader, out io.Writer) error {
	if b.brokenSymbolizer {
		return fmt.Errorf("this symbolizer is intentionally broken")
	}

	return fakeSymbolize(in, out)
}

func (b *mockBuild) ListFuzzers() []string {
	return []string{"foo/bar", "fail/nopid", "fail/notfound"}
}
