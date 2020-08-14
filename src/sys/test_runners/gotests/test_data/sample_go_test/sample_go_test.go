// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sample_go_test

import (
	"testing"
)

func TestPassing(t *testing.T) {
	print("hello")
}

func TestFailing(t *testing.T) {
	t.Errorf("This will fail")
}

func TestCrashing(t *testing.T) {
	panic("This will crash")
}

func TestSubtests(t *testing.T) {
	names := []string{"Subtest1", "Subtest2", "Subtest3"}
	for _, name := range names {
		t.Run(name, func(t *testing.T) {
		})
	}
}
