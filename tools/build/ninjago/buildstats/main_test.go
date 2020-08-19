// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.package main

package main

import (
	"bytes"
	"encoding/json"
	"flag"
	"path/filepath"
	"testing"

	"github.com/google/go-cmp/cmp"
)

var testDataDir = flag.String("test_data_dir", "", "Path to the test DOT file")

func TestExtractAndSerializeBuildStats(t *testing.T) {
	graph, err := constructGraph(paths{
		ninjalog: filepath.Join(*testDataDir, "ninja_log"),
		compdb:   filepath.Join(*testDataDir, "compdb.json"),
		graph:    filepath.Join(*testDataDir, "graph.dot"),
	})
	if err != nil {
		t.Fatalf("Failed to construct graph: %v", err)
	}

	stats, err := extractBuildStats(graph)
	if err != nil {
		t.Fatalf("Failed to extract build stats: %v", err)
	}

	buffer := new(bytes.Buffer)
	if err := serializeBuildStats(stats, buffer); err != nil {
		t.Fatalf("Failed to serialize build stats: %v", err)
	}

	var gotStats buildStats
	if err := json.NewDecoder(buffer).Decode(&gotStats); err != nil {
		t.Fatalf("Failed to deserialize build stats: %v", err)
	}

	if diff := cmp.Diff(stats, gotStats); diff != "" {
		t.Errorf("build stats diff after deserialization (-want, +got):\n%s", diff)
	}
}
