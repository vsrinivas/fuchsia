// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lib

import (
	"os"
	"testing"
)

func TestFileTreeNew(t *testing.T) {
	config, err := getConfig() // TODO(solomonkinard) better name: getTestConfig(0)
	if err != nil {
		t.Errorf("%v(): got %v", t.Name(), err)
	}
	var metrics Metrics
	metrics.Init()
	dir := "check-licenses"
	config.BaseDir = dir
	os.Mkdir(dir, 755)
	file_tree := NewFileTree(config, &metrics)
	if file_tree == nil {
		t.Errorf("%v(): got %v, want %v", t.Name(), nil, "*FileTree")
	}
}
