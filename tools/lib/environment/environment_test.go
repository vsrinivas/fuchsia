// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Package environment ensures correct environment is available.
package environment

import (
	"testing"

	"github.com/google/go-cmp/cmp"
)

func newFakeEnv() fakeEnv {
	return fakeEnv{values: make(map[string]string)}
}

func (f fakeEnv) getenv(k string) string {
	return f.values[k]
}

func (f fakeEnv) lookupEnv(k string) (string, bool) {
	v, ok := f.values[k]
	return v, ok
}

func (f fakeEnv) setenv(k, v string) error {
	f.values[k] = v
	return nil
}

type fakeEnv struct {
	values map[string]string
}

func TestEnsureTmpUnset(t *testing.T) {
	e := newFakeEnv()
	cleanUp, err := ensure(e)
	if err != nil {
		t.Fatalf("ensure() failed: %v", err)
	}
	if cleanUp == nil {
		t.Fatalf("ensure() returned unexpectedly nil cleanup func")
	}
	defer cleanUp()
	if _, ok := e.lookupEnv("TMPDIR"); !ok {
		t.Errorf("$TMPDIR unset")
	}
	for k, v := range e.values {
		if diff := cmp.Diff(v, e.values["TMPDIR"]); diff != "" {
			t.Errorf("$%s -want, +got: %s", k, diff)
		}
	}
}

func TestEnsureTmpSet(t *testing.T) {
	e := newFakeEnv()
	e.setenv("TMPDIR", "foo")
	cleanUp, err := ensure(e)
	if err != nil {
		t.Fatalf("ensure() failed: %v", err)
	}
	if cleanUp == nil {
		t.Fatalf("ensure() returned unexpectedly nil cleanup func")
	}
	defer cleanUp()
	if diff := cmp.Diff(e.values["TMPDIR"], "foo"); diff != "" {
		t.Errorf("$TMPDIR -want, +got: %s", diff)
	}
	for k, v := range e.values {
		if diff := cmp.Diff(v, e.values["TMPDIR"]); diff != "" {
			t.Errorf("$%s -want, +got: %s", k, diff)
		}
	}
}

func TestEnsureOtherSet(t *testing.T) {
	e := newFakeEnv()
	e.setenv("TMPDIR", "foo")
	e.setenv("ANDROID_TMP", "bar")
	cleanUp, err := ensure(e)
	if err != nil {
		t.Fatalf("ensure() failed: %v", err)
	}
	if cleanUp == nil {
		t.Fatalf("ensure() returned unexpectedly nil cleanup func")
	}
	defer cleanUp()
	if diff := cmp.Diff(e.values["TMPDIR"], "foo"); diff != "" {
		t.Errorf("$TMPDIR -want, +got: %s", diff)
	}
	for k, v := range e.values {
		if k == "ANDROID_TMP" {
			if diff := cmp.Diff(v, "bar"); diff != "" {
				t.Errorf("-want, +got: %s", diff)
			}
			continue
		}
		if diff := cmp.Diff(v, e.values["TMPDIR"]); diff != "" {
			t.Errorf("$%s -want, +got: %s", k, diff)
		}
	}
}
