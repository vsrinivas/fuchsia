// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.package main

package main

import (
	"bytes"
	"encoding/json"
	"errors"
	"flag"
	"path/filepath"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"go.fuchsia.dev/fuchsia/tools/build/ninjago/compdb"
	"go.fuchsia.dev/fuchsia/tools/build/ninjago/ninjalog"
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

	stats, err := extractBuildStats(&graph, steps)
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

type stubGraph struct {
	criticalPath []ninjalog.Step
	err          error
}

func (g *stubGraph) CriticalPath() ([]ninjalog.Step, error) {
	return g.criticalPath, g.err
}

func TestExtractStats(t *testing.T) {
	for _, v := range []struct {
		name  string
		g     stubGraph
		steps []ninjalog.Step
		want  buildStats
	}{
		{
			name: "empty steps",
		},
		{
			name: "successfully extract stats",
			g: stubGraph{
				criticalPath: []ninjalog.Step{
					{
						CmdHash: 1,
						End:     3 * time.Second,
						Command: &compdb.Command{Command: "gomacc a.cc"},
					},
					{
						CmdHash: 2,
						Start:   3 * time.Second,
						End:     5 * time.Second,
						Command: &compdb.Command{Command: "rustc b.rs"},
					},
				},
			},
			steps: []ninjalog.Step{
				{
					CmdHash: 1,
					End:     3 * time.Second,
					Command: &compdb.Command{Command: "gomacc a.cc"},
				},
				{
					CmdHash: 2,
					Start:   3 * time.Second,
					End:     5 * time.Second,
					Command: &compdb.Command{Command: "rustc b.rs"},
				},
				{
					CmdHash: 3,
					Start:   9 * time.Second,
					End:     10 * time.Second,
					Command: &compdb.Command{Command: "gomacc c.cc"},
				},
			},
			want: buildStats{
				CriticalPath: []action{
					{Command: "gomacc a.cc", End: 3 * time.Second, Category: "gomacc"},
					{Command: "rustc b.rs", Start: 3 * time.Second, End: 5 * time.Second, Category: "rustc"},
				},
				Slowests: []action{
					{Command: "gomacc a.cc", End: 3 * time.Second, Category: "gomacc"},
					{Command: "rustc b.rs", Start: 3 * time.Second, End: 5 * time.Second, Category: "rustc"},
					{Command: "gomacc c.cc", Start: 9 * time.Second, End: 10 * time.Second, Category: "gomacc"},
				},
				CatBuildTimes: []catBuildTime{
					{
						Category:     "gomacc",
						Count:        2,
						BuildTime:    4 * time.Second,
						MinBuildTime: time.Second,
						MaxBuildTime: 3 * time.Second,
					},
					{
						Category:     "rustc",
						Count:        1,
						BuildTime:    2 * time.Second,
						MinBuildTime: 2 * time.Second,
						MaxBuildTime: 2 * time.Second,
					},
				},
				TotalBuildTime: 6 * time.Second,
				BuildDuration:  10 * time.Second,
			},
		},
	} {
		t.Run(v.name, func(t *testing.T) {
			gotStats, err := extractBuildStats(&v.g, v.steps)
			if err != nil {
				t.Fatalf("extractBuildStats(%#v, %#v) got error: %v", v.g, v.steps, err)
			}
			if diff := cmp.Diff(v.want, gotStats); diff != "" {
				t.Errorf("extractBuildStats(%#v, %#v) got stats diff (-want +got):\n%s", v.g, v.steps, diff)
			}
		})
	}
}

func TestExtractStatsError(t *testing.T) {
	g := stubGraph{err: errors.New("test critical path error")}
	if _, err := extractBuildStats(&g, nil); err == nil {
		t.Errorf("extractBuildStats(%#v, nil) got no error, want error", g)
	}
}
