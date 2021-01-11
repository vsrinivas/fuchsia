// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ninjagraph

import (
	"compress/gzip"
	"flag"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"go.fuchsia.dev/fuchsia/tools/build/ninjago/compdb"
	"go.fuchsia.dev/fuchsia/tools/build/ninjago/ninjalog"
)

var testDataDir = flag.String("test_data_dir", "../test_data", "Path to ../test_data/; only used in GN build")

// This ordering is not perfect but good enough for these tests, since we always
// have unique Rule names in edges.
var orderEdgesByRule = cmpopts.SortSlices(func(x, y *Edge) bool { return x.Rule < y.Rule })

func TestFromDOT(t *testing.T) {
	for _, tc := range []struct {
		desc string
		dot  string
		want Graph
	}{
		{
			desc: "empty",
			dot:  `digraph ninja {}`,
			want: Graph{},
		},
		{
			desc: "no inputs single output",
			// build some_file: touch
			dot: `digraph ninja {
"0x13e8900" [label="some_file"]
"0x13e8850" [label="touch", shape=ellipse]
"0x13e8850" -> "0x13e8900"
}`,
			want: Graph{
				Nodes: map[int64]*Node{
					0x13e8900: {
						ID:   0x13e8900,
						Path: "some_file",
						In:   &Edge{Outputs: []int64{0x13e8900}, Rule: "touch"},
					},
				},
				Edges: []*Edge{{Outputs: []int64{0x13e8900}, Rule: "touch"}},
			},
		},
		{
			desc: "single input single output",
			// build bar: cp foo
			dot: `digraph ninja {
"0x1cc1a20" [label="bar"]
"0x1cc1ac0" -> "0x1cc1a20" [label=" cp"]
"0x1cc1ac0" [label="foo"]
}`,
			want: Graph{
				Nodes: map[int64]*Node{
					0x1cc1ac0: {
						ID:   0x1cc1ac0,
						Path: "foo",
						Outs: []*Edge{
							{Inputs: []int64{0x1cc1ac0}, Outputs: []int64{0x1cc1a20}, Rule: "cp"},
						},
					},
					0x1cc1a20: {
						ID:   0x1cc1a20,
						Path: "bar",
						In:   &Edge{Inputs: []int64{0x1cc1ac0}, Outputs: []int64{0x1cc1a20}, Rule: "cp"},
					},
				},
				Edges: []*Edge{
					{Inputs: []int64{0x1cc1ac0}, Outputs: []int64{0x1cc1a20}, Rule: "cp"},
				},
			},
		},
		{
			desc: "multiple inputs multiple outputs",
			// build d e: phony a b c
			dot: `digraph ninja {
"0x1110870" [label="d"]
"0x11107e0" [label="phony", shape=ellipse]
"0x11107e0" -> "0x1110870"
"0x11107e0" -> "0x11108f0"
"0x11109a0" -> "0x11107e0" [arrowhead=none]
"0x1110a60" -> "0x11107e0" [arrowhead=none]
"0x1110b10" -> "0x11107e0" [arrowhead=none]
"0x11109a0" [label="a"]
"0x1110a60" [label="b"]
"0x1110b10" [label="c"]
"0x11108f0" [label="e"]
}`,
			want: Graph{
				Nodes: map[int64]*Node{
					0x11109a0: {
						ID:   0x11109a0,
						Path: "a",
						Outs: []*Edge{
							{
								Inputs:  []int64{0x11109a0, 0x1110a60, 0x1110b10},
								Outputs: []int64{0x1110870, 0x11108f0},
								Rule:    "phony",
							},
						},
					},
					0x1110a60: {
						ID:   0x1110a60,
						Path: "b",
						Outs: []*Edge{
							{
								Inputs:  []int64{0x11109a0, 0x1110a60, 0x1110b10},
								Outputs: []int64{0x1110870, 0x11108f0},
								Rule:    "phony",
							},
						},
					},
					0x1110b10: {
						ID:   0x1110b10,
						Path: "c",
						Outs: []*Edge{
							{
								Inputs:  []int64{0x11109a0, 0x1110a60, 0x1110b10},
								Outputs: []int64{0x1110870, 0x11108f0},
								Rule:    "phony",
							},
						},
					},
					0x1110870: {
						ID:   0x1110870,
						Path: "d",
						In: &Edge{
							Inputs:  []int64{0x11109a0, 0x1110a60, 0x1110b10},
							Outputs: []int64{0x1110870, 0x11108f0},
							Rule:    "phony",
						},
					},
					0x11108f0: {
						ID:   0x11108f0,
						Path: "e",
						In: &Edge{
							Inputs:  []int64{0x11109a0, 0x1110a60, 0x1110b10},
							Outputs: []int64{0x1110870, 0x11108f0},
							Rule:    "phony",
						},
					},
				},
				Edges: []*Edge{
					{
						Inputs:  []int64{0x11109a0, 0x1110a60, 0x1110b10},
						Outputs: []int64{0x1110870, 0x11108f0},
						Rule:    "phony",
					},
				},
			},
		},
		{
			desc: "multiple builds",
			// build baz: touch
			// build bar: cp foo
			dot: `digraph ninja {
"0x13c3c70" [label="bar"]
"0x13c3d10" -> "0x13c3c70" [label=" cp"]
"0x13c3d10" [label="foo"]
"0x13c3e30" [label="baz"]
"0x13c3dc0" [label="touch", shape=ellipse]
"0x13c3dc0" -> "0x13c3e30"
}`,
			want: Graph{
				Nodes: map[int64]*Node{
					0x13c3d10: {
						ID:   0x13c3d10,
						Path: "foo",
						Outs: []*Edge{
							{Inputs: []int64{0x13c3d10}, Outputs: []int64{0x13c3c70}, Rule: "cp"},
						},
					},
					0x13c3c70: {
						ID:   0x13c3c70,
						Path: "bar",
						In:   &Edge{Inputs: []int64{0x13c3d10}, Outputs: []int64{0x13c3c70}, Rule: "cp"},
					},
					0x13c3e30: {
						ID:   0x13c3e30,
						Path: "baz",
						In:   &Edge{Outputs: []int64{0x13c3e30}, Rule: "touch"},
					},
				},
				Edges: []*Edge{
					{Inputs: []int64{0x13c3d10}, Outputs: []int64{0x13c3c70}, Rule: "cp"},
					{Outputs: []int64{0x13c3e30}, Rule: "touch"},
				},
			},
		},
		{
			desc: "omit output not in build graph",
			// build b: rule a
			// Note in the graph "0x11108f0" doesn't exist, although `rule` claims to
			// produce it.
			dot: `digraph ninja {
"0x1110870" [label="b"]
"0x11107e0" [label="rule", shape=ellipse]
"0x11107e0" -> "0x1110870"
"0x11107e0" -> "0x11108f0"
"0x11109a0" -> "0x11107e0" [arrowhead=none]
"0x11109a0" [label="a"]
}`,
			want: Graph{
				Nodes: map[int64]*Node{
					0x11109a0: {
						ID:   0x11109a0,
						Path: "a",
						Outs: []*Edge{
							{
								Inputs:  []int64{0x11109a0},
								Outputs: []int64{0x1110870}, // Note 0x11108f0 is omitted.
								Rule:    "rule",
							},
						},
					},
					0x1110870: {
						ID:   0x1110870,
						Path: "b",
						In: &Edge{
							Inputs:  []int64{0x11109a0},
							Outputs: []int64{0x1110870}, // Note 0x11108f0 is omitted.
							Rule:    "rule",
						},
					},
				},
				Edges: []*Edge{
					{
						Inputs:  []int64{0x11109a0},
						Outputs: []int64{0x1110870}, // Note 0x11108f0 is omitted.
						Rule:    "rule",
					},
				},
			},
		},
	} {
		t.Run(tc.desc, func(t *testing.T) {
			got, err := FromDOT(strings.NewReader(tc.dot))
			if err != nil {
				t.Errorf("FromDOT(%s) failed: %s", tc.dot, err)
			}
			opts := []cmp.Option{
				cmpopts.EquateEmpty(),
				cmpopts.IgnoreUnexported(Graph{}, Node{}, Edge{}),
				orderEdgesByRule,
			}
			if diff := cmp.Diff(tc.want, got, opts...); diff != "" {
				t.Errorf("FromDOT(%s) got diff (-want +got):\n%s", tc.dot, diff)
			}
		})
	}
}

func TestFromDOTErrors(t *testing.T) {
	for _, tc := range []struct {
		desc string
		dot  string
	}{
		{
			desc: "multiple edges pointing to the same output",
			dot: `digraph ninja {
"0x13e8900" [label="some_file"]
"0x13e8850" [label="touch", shape=ellipse]
"0x13e8850" -> "0x13e8900"
"0x13e8851" [label="touch", shape=ellipse]
"0x13e8851" -> "0x13e8900"
}`,
		},
	} {
		t.Run(tc.desc, func(t *testing.T) {
			if got, err := FromDOT(strings.NewReader(tc.dot)); err == nil {
				t.Errorf("FromDOT(%s) got no error, want error, parsed graph:\n%v", tc.dot, got)
			}
		})
	}
}

func TestParsingFileFromRealBuild(t *testing.T) {
	dotFilePath := filepath.Join(*testDataDir, "graph.dot.gz")
	dotFile, err := os.Open(dotFilePath)
	if err != nil {
		t.Fatalf("Failed to open file %q: %s", dotFilePath, err)
	}
	defer dotFile.Close()

	dotFileUnzipped, err := gzip.NewReader(dotFile)
	if err != nil {
		t.Fatalf("Failed to unzip file %q: %s", dotFilePath, err)
	}
	defer dotFileUnzipped.Close()

	got, err := FromDOT(dotFileUnzipped)
	if err != nil {
		t.Fatalf("FromDOT failed: %s", err)
	}
	if gotNodes := len(got.Nodes); gotNodes != 12369 {
		t.Errorf("FromDOT(Zircon build graph) got %d nodes, want 12369", gotNodes)
	}
	if gotEdges := len(got.Edges); gotEdges != 10568 {
		t.Errorf("FromDOT(Zircon build graph) got %d edges, want 10567", gotEdges)
	}
}

func TestPopulateEdges(t *testing.T) {
	for _, tc := range []struct {
		desc      string
		steps     []ninjalog.Step
		graph     Graph
		wantGraph Graph
	}{
		{
			desc: "empty graph",
		},
		{
			desc: "phony edges are skipped",
			graph: Graph{
				Edges: []*Edge{
					{Outputs: []int64{0, 1, 2}, Rule: "phony"},
				},
			},
			wantGraph: Graph{
				Edges: []*Edge{
					{Outputs: []int64{0, 1, 2}, Rule: "phony"},
				},
			},
		},
		{
			desc: "single output",
			steps: []ninjalog.Step{
				{Start: time.Millisecond, End: time.Second, Out: "foo"},
			},
			graph: Graph{
				// `In` and `Outs` edges on nodes are omitted since they don't affect
				// this test.
				Nodes: map[int64]*Node{
					0: {ID: 0, Path: "foo"},
				},
				Edges: []*Edge{
					{Outputs: []int64{0}},
				},
			},
			wantGraph: Graph{
				Nodes: map[int64]*Node{
					0: {ID: 0, Path: "foo"},
				},
				Edges: []*Edge{
					{
						Outputs: []int64{0},
						Step:    &ninjalog.Step{Start: time.Millisecond, End: time.Second, Out: "foo"},
					},
				},
			},
		},
		{
			desc: "multiple outputs",
			steps: []ninjalog.Step{
				{Out: "foo", Outs: []string{"bar", "baz"}},
			},
			graph: Graph{
				// `In` and `Outs` edges on nodes are omitted since they don't affect
				// this test.
				Nodes: map[int64]*Node{
					0: {ID: 0, Path: "foo"},
					1: {ID: 1, Path: "bar"},
					2: {ID: 2, Path: "baz"},
				},
				Edges: []*Edge{
					{Outputs: []int64{0, 1, 2}},
				},
			},
			wantGraph: Graph{
				Nodes: map[int64]*Node{
					0: {ID: 0, Path: "foo"},
					1: {ID: 1, Path: "bar"},
					2: {ID: 2, Path: "baz"},
				},
				Edges: []*Edge{
					{
						Outputs: []int64{0, 1, 2},
						Step:    &ninjalog.Step{Out: "foo", Outs: []string{"bar", "baz"}},
					},
				},
			},
		},
		{
			desc: "multiple edges",
			steps: []ninjalog.Step{
				{Out: "foo"},
				{Out: "bar", Outs: []string{"baz"}},
			},
			graph: Graph{
				// `In` and `Outs` edges on nodes are omitted since they don't affect
				// this test.
				Nodes: map[int64]*Node{
					0:  {ID: 0, Path: "foo"},
					1:  {ID: 1, Path: "bar"},
					2:  {ID: 2, Path: "baz"},
					42: {ID: 42, Path: "truth"},
				},
				Edges: []*Edge{
					{Outputs: []int64{0}},
					{Outputs: []int64{1, 2}},
					{Outputs: []int64{42}, Rule: "phony"},
				},
			},
			wantGraph: Graph{
				Nodes: map[int64]*Node{
					0:  {ID: 0, Path: "foo"},
					1:  {ID: 1, Path: "bar"},
					2:  {ID: 2, Path: "baz"},
					42: {ID: 42, Path: "truth"},
				},
				Edges: []*Edge{
					{
						Outputs: []int64{0},
						Step:    &ninjalog.Step{Out: "foo"},
					},
					{
						Outputs: []int64{1, 2},
						Step:    &ninjalog.Step{Out: "bar", Outs: []string{"baz"}},
					},
					{
						Outputs: []int64{42},
						Rule:    "phony",
					},
				},
			},
		},
		{
			desc: "partial graph with missing steps",
			steps: []ninjalog.Step{
				{Out: "bar"},
			},
			graph: Graph{
				// `In` and `Outs` edges on nodes are omitted since they don't affect
				// this test.
				Nodes: map[int64]*Node{
					0: {ID: 0, Path: "foo"},
					1: {ID: 1, Path: "bar"},
				},
				Edges: []*Edge{
					{Outputs: []int64{0}},
					{Outputs: []int64{1}},
				},
			},
			wantGraph: Graph{
				Nodes: map[int64]*Node{
					0: {ID: 0, Path: "foo"},
					1: {ID: 1, Path: "bar"},
				},
				Edges: []*Edge{
					{
						Outputs: []int64{0},
					},
					{
						Outputs: []int64{1},
						Step:    &ninjalog.Step{Out: "bar"},
					},
				},
			},
		},
	} {
		t.Run(tc.desc, func(t *testing.T) {
			if err := tc.graph.PopulateEdges(tc.steps); err != nil {
				t.Errorf("PopulateEdges(%v) failed: %v", tc.steps, err)
			}
			if diff := cmp.Diff(tc.wantGraph, tc.graph, cmpopts.IgnoreUnexported(Graph{}, Node{}, Edge{})); diff != "" {
				t.Errorf("PopulateEdges(%v) got graph diff (-want +got):\n%s", tc.steps, diff)
			}
		})
	}
}

func TestPopulateEdgesErrors(t *testing.T) {
	for _, tc := range []struct {
		desc  string
		steps []ninjalog.Step
		graph Graph
	}{
		{
			desc: "steps with duplicate output",
			steps: []ninjalog.Step{
				{Out: "foo", Outs: []string{"bar"}},
				{Out: "bar"},
			},
		},
		{
			desc: "multiple steps match the same edge",
			steps: []ninjalog.Step{
				{Out: "foo", CmdHash: 123},
				{Out: "bar", CmdHash: 456},
			},
			graph: Graph{
				// `In` and `Outs` edges on the Node are not spelled out since they
				// don't affect this test.
				Nodes: map[int64]*Node{
					0: {ID: 0, Path: "foo"},
					1: {ID: 1, Path: "bar"},
				},
				Edges: []*Edge{
					{Outputs: []int64{0, 1}},
				},
			},
		},
	} {
		t.Run(tc.desc, func(t *testing.T) {
			if err := tc.graph.PopulateEdges(tc.steps); err == nil {
				t.Errorf("PopulateEdges(%v) got no errors, want error", tc.steps)
			}
		})
	}
}

func readAndUnzip(tb testing.TB, path string) *gzip.Reader {
	f, err := os.Open(path)
	if err != nil {
		tb.Fatalf("Failed to read %q: %v", path, err)
	}
	tb.Cleanup(func() { f.Close() })

	unzipped, err := gzip.NewReader(f)
	if err != nil {
		tb.Fatalf("Failed to unzip %q: %v", path, err)
	}
	tb.Cleanup(func() { unzipped.Close() })
	return unzipped
}

func readTestGraph(tb testing.TB) Graph {
	ninjaLogPath := filepath.Join(*testDataDir, "ninja_log.gz")
	ninjaLog, err := ninjalog.Parse(ninjaLogPath, readAndUnzip(tb, ninjaLogPath))
	if err != nil {
		tb.Fatalf("Failed to parse Ninja log %q: %v", ninjaLogPath, err)
	}
	steps := ninjalog.Dedup(ninjaLog.Steps)

	compdbPath := filepath.Join(*testDataDir, "compdb.json.gz")
	commands, err := compdb.Parse(readAndUnzip(tb, compdbPath))
	if err != nil {
		tb.Fatalf("Failed to parse compdb from path %q: %v", compdbPath, err)
	}
	steps = ninjalog.Populate(steps, commands)

	dotFilePath := filepath.Join(*testDataDir, "graph.dot.gz")
	graph, err := FromDOT(readAndUnzip(tb, dotFilePath))
	if err != nil {
		tb.Fatalf("Failed to parse Ninja graph %q: %v", dotFilePath, err)
	}

	if err := graph.PopulateEdges(steps); err != nil {
		tb.Fatalf("Failed to populate parsed Ninja graph with deduplicated steps: %v", err)
	}
	return graph
}

func TestPopulateEdgesWithRealBuild(t *testing.T) {
	graph := readTestGraph(t)
	for _, e := range graph.Edges {
		if isPhony, gotNilStep := e.Rule == "phony", e.Step == nil; isPhony != gotNilStep {
			var outputs []string
			for _, output := range e.Outputs {
				outputs = append(outputs, graph.Nodes[output].Path)
			}
			t.Errorf("Invalid edge after `PopulateEdges`, isPhony (%t) != gotNilStep (%t), outputs of this edge: %v", isPhony, gotNilStep, outputs)
		}
	}
}

func TestWithStepsOnly(t *testing.T) {
	edge2 := &Edge{Inputs: []int64{1}, Outputs: []int64{2}, Step: &ninjalog.Step{Out: "2"}}
	edge2NoStep := &Edge{Inputs: []int64{1}, Outputs: []int64{2}}

	edge4Phony := &Edge{Inputs: []int64{3}, Outputs: []int64{4}, Rule: "phony"}
	edge4NoStep := &Edge{Inputs: []int64{3}, Outputs: []int64{4}}

	edge5 := &Edge{Inputs: []int64{2, 4}, Outputs: []int64{5}, Step: &ninjalog.Step{Out: "5"}}
	edge5NoStep := &Edge{Inputs: []int64{2, 4}, Outputs: []int64{5}}

	edge6 := &Edge{Inputs: []int64{5}, Outputs: []int64{6}, Step: &ninjalog.Step{Out: "6"}}
	edge6NoStep := &Edge{Inputs: []int64{5}, Outputs: []int64{6}}

	for _, tc := range []struct {
		desc string
		g    Graph
		want Graph
	}{
		{
			desc: "empty graph",
			g:    Graph{},
			want: Graph{},
		},
		{
			desc: "partial graph",
			//     1 -> 2 -no step-> 5 -> 6
			//               /
			// 3 -phony-> 4 -
			g: Graph{
				Nodes: map[int64]*Node{
					1: {ID: 1, Outs: []*Edge{edge2}},
					2: {ID: 2, In: edge2, Outs: []*Edge{edge5NoStep}},
					3: {ID: 3, Outs: []*Edge{edge4Phony}},
					4: {ID: 4, In: edge4Phony, Outs: []*Edge{edge5NoStep}},
					5: {ID: 5, In: edge5NoStep, Outs: []*Edge{edge6}},
					6: {ID: 6, In: edge6},
				},
				Edges: []*Edge{edge2, edge4Phony, edge5NoStep, edge6},
			},
			// 1 -> 2  5 -> 6
			// 3 -phony-> 4
			want: Graph{
				Nodes: map[int64]*Node{
					1: {ID: 1, Outs: []*Edge{edge2}},
					2: {ID: 2, In: edge2},
					3: {ID: 3, Outs: []*Edge{edge4Phony}},
					4: {ID: 4, In: edge4Phony},
					5: {ID: 5, Outs: []*Edge{edge6}},
					6: {ID: 6, In: edge6},
				},
				Edges: []*Edge{edge2, edge4Phony, edge6},
			},
		},
		{
			desc: "yet another partial graph",
			// 1 -no step-> 2 ---> 5 -no step-> 6
			//                 /
			//   3 -phony-> 4 -
			g: Graph{
				Nodes: map[int64]*Node{
					1: {ID: 1, Outs: []*Edge{edge2NoStep}},
					2: {ID: 2, In: edge2NoStep, Outs: []*Edge{edge5}},
					3: {ID: 3, Outs: []*Edge{edge4Phony}},
					4: {ID: 4, In: edge4Phony, Outs: []*Edge{edge5}},
					5: {ID: 5, In: edge5, Outs: []*Edge{edge6NoStep}},
					6: {ID: 6, In: edge6NoStep},
				},
				Edges: []*Edge{edge2NoStep, edge4Phony, edge5, edge6NoStep},
			},
			//              2 ---> 5
			//                 /
			//   3 -phony-> 4 -
			want: Graph{
				Nodes: map[int64]*Node{
					2: {ID: 2, Outs: []*Edge{edge5}},
					3: {ID: 3, Outs: []*Edge{edge4Phony}},
					4: {ID: 4, In: edge4Phony, Outs: []*Edge{edge5}},
					5: {ID: 5, In: edge5},
				},
				Edges: []*Edge{edge4Phony, edge5},
			},
		},
		{
			desc: "full graph because all steps are populated",
			//        1 -> 2 --> 5 -> 6
			//               /
			// 3 -phony-> 4 -
			g: Graph{
				Nodes: map[int64]*Node{
					1: {ID: 1, Outs: []*Edge{edge2}},
					2: {ID: 2, In: edge2, Outs: []*Edge{edge5}},
					3: {ID: 3, Outs: []*Edge{edge4Phony}},
					4: {ID: 4, In: edge4Phony, Outs: []*Edge{edge5}},
					5: {ID: 5, In: edge5, Outs: []*Edge{edge6}},
					6: {ID: 6, In: edge6},
				},
				Edges: []*Edge{edge2, edge4Phony, edge5, edge6},
			},
			// 1 -> 2  5 -> 6
			// 3 -phony-> 4
			want: Graph{
				Nodes: map[int64]*Node{
					1: {ID: 1, Outs: []*Edge{edge2}},
					2: {ID: 2, In: edge2, Outs: []*Edge{edge5}},
					3: {ID: 3, Outs: []*Edge{edge4Phony}},
					4: {ID: 4, In: edge4Phony, Outs: []*Edge{edge5}},
					5: {ID: 5, In: edge5, Outs: []*Edge{edge6}},
					6: {ID: 6, In: edge6},
				},
				Edges: []*Edge{edge2, edge4Phony, edge5, edge6},
			},
		},
		{
			desc: "result graph empty because no steps are populated",
			// 1 -no step-> 2 -no step-> 5 -no step-> 6
			//                   /
			//   3 -no step-> 4 -
			g: Graph{
				Nodes: map[int64]*Node{
					1: {ID: 1, Outs: []*Edge{edge2NoStep}},
					2: {ID: 2, In: edge2NoStep, Outs: []*Edge{edge5NoStep}},
					3: {ID: 3, Outs: []*Edge{edge4NoStep}},
					4: {ID: 4, In: edge4NoStep, Outs: []*Edge{edge5NoStep}},
					5: {ID: 5, In: edge5NoStep, Outs: []*Edge{edge6NoStep}},
					6: {ID: 6, In: edge6NoStep},
				},
				Edges: []*Edge{edge2NoStep, edge4NoStep, edge5NoStep, edge6NoStep},
			},
			want: Graph{},
		},
	} {
		t.Run(tc.desc, func(t *testing.T) {
			got, err := WithStepsOnly(tc.g)
			if err != nil {
				t.Errorf("WithStepsOnly got error: %v", err)
			}
			opts := []cmp.Option{
				cmpopts.EquateEmpty(),
				cmpopts.IgnoreUnexported(Graph{}, Node{}, Edge{}),
			}
			if diff := cmp.Diff(tc.want, got, opts...); diff != "" {
				t.Errorf("WithStepsOnly got output graph diff (-want +got):\n%s", diff)
			}
		})
	}
}

func TestWithStepsOnlyErrors(t *testing.T) {
	edge := &Edge{Inputs: []int64{1}, Outputs: []int64{2}, Step: &ninjalog.Step{Out: "2"}}

	for _, tc := range []struct {
		desc string
		g    Graph
	}{
		{
			desc: "missing node",
			g: Graph{
				Edges: []*Edge{edge},
			},
		},
		{
			desc: "duplicate edges",
			g: Graph{
				Nodes: map[int64]*Node{
					1: {ID: 1, Outs: []*Edge{edge}},
					2: {ID: 2, In: edge},
				},
				Edges: []*Edge{edge, edge},
			},
		},
	} {
		t.Run(tc.desc, func(t *testing.T) {
			if _, err := WithStepsOnly(tc.g); err == nil {
				t.Error("WithStepsOnly got no error, want non-nil error")
			}
		})
	}
}
