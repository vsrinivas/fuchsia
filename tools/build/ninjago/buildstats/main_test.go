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

var testDataFlag = flag.String("test_data_dir", "../test_data", "Path to ../test_data/; only used in GN build")

func TestExtractAndSerializeBuildStats(t *testing.T) {
	graph, steps, err := constructGraph(paths{
		ninjalog: filepath.Join(*testDataFlag, "ninja_log"),
		compdb:   filepath.Join(*testDataFlag, "compdb.json"),
		graph:    filepath.Join(*testDataFlag, "graph.dot"),
	})
	if err != nil {
		t.Fatalf("Failed to construct graph: %v", err)
	}

	stats, err := extractBuildStats(graph, steps)
	if err != nil {
		t.Fatalf("Failed to extract build stats: %v", err)
	}
	if len(stats.CriticalPath) == 0 {
		t.Errorf("Critical path in stats is emtpy, expect non-empty")
	}
	if len(stats.Slowests) == 0 {
		t.Errorf("Slowest builds in stats is empty, expect non-empty")
	}
	if len(stats.CatBuildTimes) == 0 {
		t.Errorf("Build times by category in stats is empty, expect non-empty")
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
