// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"go.fuchsia.dev/fuchsia/tools/build/ninjago/compdb"
	"go.fuchsia.dev/fuchsia/tools/build/ninjago/ninjagraph"
	"go.fuchsia.dev/fuchsia/tools/build/ninjago/ninjalog"
)

var (
	edge1 = &ninjagraph.Edge{Outputs: []int64{1}}
	edge2 = &ninjagraph.Edge{Inputs: []int64{1}, Outputs: []int64{2}}
	edge3 = &ninjagraph.Edge{Inputs: []int64{1}, Outputs: []int64{3}}
)

func TestJoin(t *testing.T) {
	for _, v := range []struct {
		name         string
		artifacts    artifacts
		criticalPath bool
		wantSteps    []ninjalog.Step
	}{
		{
			name: "empty",
		},
		{
			name: "join compdb",
			artifacts: artifacts{
				steps: []ninjalog.Step{
					{
						Out:     "a",
						CmdHash: ninjalog.MurmurHash64A([]byte("touch a")),
					},
					{
						Out:     "b",
						CmdHash: ninjalog.MurmurHash64A([]byte("touch b")),
					},
					{
						Out: "c",
					},
				},
				commands: []compdb.Command{
					{Command: "touch a", Output: "a", Arguments: []string{"a arg"}},
					{Command: "touch b"},
				},
			},
			wantSteps: []ninjalog.Step{
				{
					Out:     "a",
					CmdHash: ninjalog.MurmurHash64A([]byte("touch a")),
					Command: &compdb.Command{Command: "touch a", Output: "a", Arguments: []string{"a arg"}},
				},
				{
					Out:     "b",
					CmdHash: ninjalog.MurmurHash64A([]byte("touch b")),
					Command: &compdb.Command{Command: "touch b"},
				},
				{
					Out: "c",
				},
			},
		},
		{
			name: "join graph for critical path",
			artifacts: artifacts{
				steps: []ninjalog.Step{
					{
						Out:     "1",
						CmdHash: ninjalog.MurmurHash64A([]byte("touch 1")),
						End:     time.Second,
					},
					{
						Out:   "2",
						Start: time.Second,
						End:   10 * time.Second,
					},
					{
						Out:   "3",
						Start: time.Second,
						End:   2 * time.Second,
					},
				},
				commands: []compdb.Command{
					{Command: "touch 1"},
				},
				// 1 ------> 2 (critical path)
				//    \
				//      ---> 3
				graph: ninjagraph.Graph{
					Nodes: map[int64]*ninjagraph.Node{
						1: {
							ID:   1,
							Path: "1",
							In:   edge1,
							Outs: []*ninjagraph.Edge{edge2, edge3},
						},
						2: {ID: 2, Path: "2", In: edge2},
						3: {ID: 3, Path: "3", In: edge3},
					},
					Edges: []*ninjagraph.Edge{edge1, edge2, edge3},
				},
			},
			criticalPath: true,
			wantSteps: []ninjalog.Step{
				{
					Out:            "1",
					End:            time.Second,
					CmdHash:        ninjalog.MurmurHash64A([]byte("touch 1")),
					Command:        &compdb.Command{Command: "touch 1"},
					OnCriticalPath: true,
					Drag:           time.Second,
				},
				{
					Out:            "2",
					Start:          time.Second,
					End:            10 * time.Second,
					OnCriticalPath: true,
					Drag:           8 * time.Second,
				},
				{
					Out:        "3",
					Start:      time.Second,
					End:        2 * time.Second,
					TotalFloat: 8 * time.Second,
				},
			},
		},
	} {
		t.Run(v.name, func(t *testing.T) {
			got, err := join(v.artifacts, v.criticalPath)
			if err != nil {
				t.Fatalf("join(%+v, %t) failed: %v", v.artifacts, v.criticalPath, err)
			}
			if diff := cmp.Diff(v.wantSteps, got); diff != "" {
				t.Fatalf("join(%+v, %t) got steps diff (-want, +got):\n%s", v.artifacts, v.criticalPath, diff)
			}
		})
	}
}

func TestJoinMissingStep(t *testing.T) {
	as := artifacts{
		// The graph is expecting an edge to output 3, but it's missing from `steps`.
		steps: []ninjalog.Step{{Out: "1"}, {Out: "2"}},
		graph: ninjagraph.Graph{
			Nodes: map[int64]*ninjagraph.Node{
				1: {
					ID:   1,
					Path: "1",
					In:   edge1,
					Outs: []*ninjagraph.Edge{edge2, edge3},
				},
				2: {ID: 2, Path: "2", In: edge2},
				3: {ID: 3, Path: "3", In: edge3},
			},
			Edges: []*ninjagraph.Edge{edge1, edge2, edge3},
		},
	}
	if _, err := join(as, true); err == nil {
		t.Fatalf("join(%+v, true) succeeded, want error", as)
	}
}
