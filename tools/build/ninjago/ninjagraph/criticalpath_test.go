// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ninjagraph

import (
	"fmt"
	"strings"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"go.fuchsia.dev/fuchsia/tools/build/ninjago/ninjalog"
)

func TestCriticalPathWithRealBuild(t *testing.T) {
	graph := readTestGraph(t)

	cp, err := graph.CriticalPath()
	if err != nil {
		t.Fatalf("CriticalPath() got error: %v", err)
	}
	cp2, err := graph.CriticalPathV2()
	if err != nil {
		t.Fatalf("CriticalPathV2() got error: %v", err)
	}
	if diff := cmp.Diff(cp, cp2); diff != "" {
		t.Errorf("CriticalPath() and CriticalPathV2() returned different critical paths (-v1, +v2):\n%s", diff)
	}
}

func BenchmarkCriticalPath(b *testing.B) {
	graph := readTestGraph(b)

	b.Run("CriticalPath", func(b *testing.B) {
		for i := 0; i < b.N; i++ {
			clearMemoization(&graph)
			if _, err := graph.CriticalPath(); err != nil {
				b.Fatalf("CriticalPath() got error: %v", err)
			}
		}
	})

	b.Run("CriticalPathV2", func(b *testing.B) {
		for i := 0; i < b.N; i++ {
			clearMemoization(&graph)
			if _, err := graph.CriticalPathV2(); err != nil {
				b.Fatalf("CriticalPathV2() got error: %v", err)
			}
		}
	})
}

func TestCriticalPathDrag(t *testing.T) {
	graph := readTestGraph(t)

	if err := graph.addSink(); err != nil {
		t.Fatalf("addSink() got error: %v", err)
	}

	type edgeDrag struct {
		edge *Edge
		drag time.Duration
	}

	var criticalEdges []edgeDrag
	for _, e := range graph.Edges {
		if e.Rule == "phony" || e.Rule == ninjagoArtificialSinkRule {
			continue
		}
		tf, err := graph.totalFloat(e)
		if err != nil {
			t.Fatalf("totalFloat got error: %v", err)
		}
		if tf != 0 {
			continue
		}
		drag, err := graph.drag(e)
		if err != nil {
			t.Fatalf("drag got error: %v", err)
		}
		criticalEdges = append(criticalEdges, edgeDrag{edge: e, drag: drag})
	}

	steps, err := graph.CriticalPathV2()
	if err != nil {
		t.Fatalf("CriticalPathV2 got error: %v", err)
	}
	totalBuildTime := totalDuration(steps)

	for _, e := range criticalEdges {
		clearMemoization(&graph)

		origEnd := e.edge.Step.End
		// Reduce this edge's duration to 0.
		e.edge.Step.End = e.edge.Step.Start

		// Calculate the new critical path, expect build speed improvement to
		// equal to this edge's drag.
		steps, err = graph.CriticalPathV2()
		if err != nil {
			t.Fatalf("CriticalPathV2 got error: %v", err)
		}
		newTotalBuildTime := totalDuration(steps)

		// Revert this edge's duration back so it doesn't affect later iterations.
		e.edge.Step.End = origEnd

		if got := totalBuildTime - newTotalBuildTime; got != e.drag {
			t.Errorf("After changing build duration of edge producing %s to zero, got build time improvement: %s, drag: %s", e.edge.Step.Out, got, e.drag)
		}
	}
}

func clearMemoization(graph *Graph) {
	for _, n := range graph.Nodes {
		n.criticalInput = nil
		n.criticalBuildDuration = nil
	}
	for _, edge := range graph.Edges {
		edge.earliestStart = nil
		edge.latestFinish = nil
	}
}

func totalDuration(steps []ninjalog.Step) time.Duration {
	var d time.Duration
	for _, step := range steps {
		d += step.Duration()
	}
	return d
}

func TestCriticalPath(t *testing.T) {
	step1 := ninjalog.Step{End: 5}
	edge1 := &Edge{Outputs: []int64{1}, Step: &step1}

	step2 := ninjalog.Step{Start: 5, End: 10, Out: "2"}
	edge2 := &Edge{Inputs: []int64{1}, Outputs: []int64{2}, Step: &step2}

	step3 := ninjalog.Step{Start: 42, End: 420, Out: "3"}
	edge3 := &Edge{Inputs: []int64{2}, Outputs: []int64{3}, Step: &step3}

	edge3Phony := &Edge{Inputs: []int64{2}, Outputs: []int64{3}, Rule: "phony"}

	step4 := ninjalog.Step{End: 100, Out: "4"}
	edge4 := &Edge{Inputs: []int64{3}, Outputs: []int64{4}, Step: &step4}

	step4LateStart := ninjalog.Step{Start: 99, End: 100, Out: "4"}
	edge4LateStart := &Edge{Inputs: []int64{3}, Outputs: []int64{4}, Step: &step4LateStart}

	step5 := ninjalog.Step{Start: 100, End: 200, Out: "5"}
	edge5 := &Edge{Inputs: []int64{2, 4}, Outputs: []int64{5}, Step: &step5}

	step6 := ninjalog.Step{Start: 20, End: 30, Out: "6"}
	edge6 := &Edge{Inputs: []int64{2, 7}, Outputs: []int64{6}, Step: &step6}

	step7 := ninjalog.Step{Start: 10, End: 20, Out: "7"}
	edge7 := &Edge{Inputs: []int64{2}, Outputs: []int64{7}, Step: &step7}

	clearEdgeMemoizations := func() {
		for _, e := range []*Edge{edge1, edge2, edge3, edge4, edge4LateStart, edge5} {
			e.earliestStart = nil
			e.latestFinish = nil
		}
	}

	for _, tc := range []struct {
		desc  string
		graph Graph
		want  []ninjalog.Step
	}{
		{
			desc: "empty graph",
		},
		{
			// 1 -> 2 -> 3
			desc: "single inputs",
			graph: Graph{
				Nodes: map[int64]*Node{
					1: {ID: 1, Outs: []*Edge{edge2}},
					2: {ID: 2, In: edge2, Outs: []*Edge{edge3}},
					3: {ID: 3, In: edge3},
				},
				Edges: []*Edge{edge2, edge3},
			},
			want: []ninjalog.Step{step2, step3},
		},
		{
			// 1 -> 2 --phony--> 3
			desc: "phony edge",
			graph: Graph{
				Nodes: map[int64]*Node{
					1: {ID: 1, Outs: []*Edge{edge2}},
					2: {ID: 2, In: edge2, Outs: []*Edge{edge3Phony}},
					3: {ID: 3, In: edge3Phony},
				},
				Edges: []*Edge{edge2, edge3Phony},
			},
			want: []ninjalog.Step{step2}, // Phony edge is not included.
		},
		{
			// -> 1 -> 2 -> 3
			desc: "edge with no input file",
			graph: Graph{
				Nodes: map[int64]*Node{
					1: {ID: 1, In: edge1, Outs: []*Edge{edge2}},
					2: {ID: 2, In: edge2, Outs: []*Edge{edge3}},
					3: {ID: 3, In: edge3},
				},
				Edges: []*Edge{edge1, edge2, edge3},
			},
			want: []ninjalog.Step{step1, step2, step3},
		},
		{
			// 1 -> 2 ---> 5
			//         /
			// 3 -> 4 -
			desc: "multiple inputs same start time",
			graph: Graph{
				Nodes: map[int64]*Node{
					// `Outs` edges on nodes are omitted since they don't affect this test.
					1: {ID: 1, Outs: []*Edge{edge2}},
					2: {ID: 2, In: edge2, Outs: []*Edge{edge5}},

					3: {ID: 3, Outs: []*Edge{edge4}},
					4: {ID: 4, In: edge4, Outs: []*Edge{edge5}},

					5: {ID: 5, In: edge5},
				},
				// `Edges` are omitted since they don't affect this test.
				Edges: []*Edge{edge2, edge4, edge5},
			},
			want: []ninjalog.Step{step4, step5},
		},
		{
			// 1 -> 2 ---> 5
			//         /
			// 3 -> 4 -
			desc: "multiple inputs different start times",
			graph: Graph{
				Nodes: map[int64]*Node{
					// `Outs` edges on nodes are omitted since they don't affect this test.
					1: {ID: 1, Outs: []*Edge{edge2}},
					2: {ID: 2, In: edge2, Outs: []*Edge{edge5}},

					3: {ID: 3, Outs: []*Edge{edge4LateStart}},
					4: {ID: 4, In: edge4LateStart, Outs: []*Edge{edge5}},

					5: {ID: 5, In: edge5},
				},
				Edges: []*Edge{edge2, edge4LateStart, edge5},
			},
			want: []ninjalog.Step{step2, step5},
		},
		{
			// 1 -> 2 -------> 6
			//       \     /
			//        -> 7
			desc: "multiple inputs all on critical path",
			graph: Graph{
				Nodes: map[int64]*Node{
					1: {ID: 1, Outs: []*Edge{edge2}},
					2: {ID: 2, In: edge2, Outs: []*Edge{edge6, edge7}},
					6: {ID: 6, In: edge6},
					7: {ID: 7, In: edge7, Outs: []*Edge{edge6}},
				},
				Edges: []*Edge{edge2, edge6, edge7},
			},
			want: []ninjalog.Step{step2, step7, step6},
		},
	} {
		t.Run(tc.desc, func(t *testing.T) {
			// Clear memoization fields because we are reusing pointers to edges.
			defer clearEdgeMemoizations()

			got, err := tc.graph.CriticalPath()
			if err != nil {
				t.Fatalf("CriticalPath() got error: %v", err)
			}
			if diff := cmp.Diff(tc.want, got); diff != "" {
				t.Errorf("CriticalPath() got diff (-want, +got):\n%s", diff)
			}

			got, err = tc.graph.CriticalPathV2()
			if err != nil {
				t.Fatalf("CriticalPathV2() got error: %v", err)
			}
			if diff := cmp.Diff(tc.want, got); diff != "" {
				t.Errorf("CriticalPathV2() got diff (-want +got):\n%s", diff)
			}
		})
	}
}

func TestCriticalPathErrors(t *testing.T) {
	edge2 := &Edge{Inputs: []int64{1}, Outputs: []int64{2}, Step: &ninjalog.Step{End: 10, Out: "2"}}
	edge2WithNoStep := &Edge{Inputs: []int64{1}, Outputs: []int64{2}}

	for _, tc := range []struct {
		desc  string
		graph Graph
	}{
		{
			desc: "edge missing step",
			graph: Graph{
				Nodes: map[int64]*Node{
					1: {ID: 1},
					2: {ID: 2, In: edge2WithNoStep},
				},
				Edges: []*Edge{edge2WithNoStep},
			},
		},
		{
			desc: "missing input node",
			graph: Graph{
				Nodes: map[int64]*Node{
					2: {ID: 2, In: edge2},
				},
				Edges: []*Edge{edge2},
			},
		},
	} {
		t.Run(tc.desc, func(t *testing.T) {
			if _, err := tc.graph.CriticalPath(); err == nil {
				t.Error("CriticalPath() got no error, want error")
			}
		})
	}
}

func TestAddSink(t *testing.T) {
	edge2 := &Edge{Inputs: []int64{1}, Outputs: []int64{2}}
	edge3 := &Edge{Inputs: []int64{1}, Outputs: []int64{3}}

	for _, v := range []struct {
		desc  string
		graph Graph
		want  []*Edge
	}{
		{
			desc: "empty graph",
		},
		{
			// 1 -> 2
			desc: "single pure output",
			graph: Graph{
				Nodes: map[int64]*Node{
					1: {ID: 1, Outs: []*Edge{edge2}},
					2: {ID: 2, In: edge2},
				},
				Edges: []*Edge{edge2},
			},
			want: []*Edge{
				edge2,
				{
					Inputs: []int64{2},
					Rule:   ninjagoArtificialSinkRule,
					Step:   &ninjalog.Step{},
				},
			},
		},
		{
			// 1 ----> 2
			//    \
			//     --> 3
			desc: "multiple pure outputs",
			graph: Graph{
				Nodes: map[int64]*Node{
					1: {ID: 1, Outs: []*Edge{edge2, edge3}},
					2: {ID: 2, In: edge2},
					3: {ID: 3, In: edge3},
				},
				Edges: []*Edge{edge2, edge3},
			},
			want: []*Edge{
				edge2,
				edge3,
				{
					Inputs: []int64{2, 3},
					Rule:   ninjagoArtificialSinkRule,
					Step:   &ninjalog.Step{},
				},
			},
		},
	} {
		t.Run(v.desc, func(t *testing.T) {
			if err := v.graph.addSink(); err != nil {
				t.Fatalf("addSink() got error: %v", err)
			}
			opts := []cmp.Option{
				cmpopts.IgnoreUnexported(Edge{}),
				// Input IDs on sink edge has indeterministic order.
				cmpopts.SortSlices(func(i, j int64) bool { return i < j }),
			}
			if diff := cmp.Diff(v.graph.Edges, v.want, opts...); diff != "" {
				t.Errorf("After addSink(), got graph.Edges diff (-want, +got):\n%s", diff)
			}

			// addSink should be idempotent.
			if err := v.graph.addSink(); err != nil {
				t.Fatalf("addSink() got error: %v", err)
			}
			if diff := cmp.Diff(v.graph.Edges, v.want, opts...); diff != "" {
				t.Errorf("After addSink(), got graph.Edges diff (-want, +got):\n%s", diff)
			}
		})
	}
}

func TestTotalFloat(t *testing.T) {
	step1 := ninjalog.Step{Out: "1", End: 10}
	edge1 := &Edge{
		Outputs: []int64{1},
		Step:    &step1,
	}

	step2 := ninjalog.Step{Out: "2", Start: 10, End: 30}
	edge2 := &Edge{
		Inputs:  []int64{1},
		Outputs: []int64{2},
		Step:    &step2,
	}

	step3 := ninjalog.Step{Out: "3", Start: 30, End: 35}
	edge3 := &Edge{
		Inputs:  []int64{2},
		Outputs: []int64{3},
		Step:    &step3,
	}

	step4 := ninjalog.Step{Out: "4", Start: 35, End: 45}
	edge4 := &Edge{
		Inputs:  []int64{3},
		Outputs: []int64{4},
		Step:    &step4,
	}

	step5 := ninjalog.Step{Out: "5", Start: 45, End: 65}
	edge5 := &Edge{
		Inputs:  []int64{4, 7, 8},
		Outputs: []int64{5},
		Step:    &step5,
	}

	step6 := ninjalog.Step{Out: "6", Start: 10, End: 25}
	edge6 := &Edge{
		Inputs:  []int64{1},
		Outputs: []int64{6},
		Step:    &step6,
	}

	step7 := ninjalog.Step{Out: "7", Start: 35, End: 40}
	edge7 := &Edge{
		Inputs:  []int64{3, 6},
		Outputs: []int64{7},
		Step:    &step7,
	}

	step8 := ninjalog.Step{Out: "8", Start: 10, End: 25}
	edge8 := &Edge{
		Inputs:  []int64{1},
		Outputs: []int64{8},
		Step:    &step8,
	}

	// Test graph based on https://en.wikipedia.org/wiki/Critical_path_drag#/media/File:SimpleAONwDrag3.png.
	g := Graph{
		Nodes: map[int64]*Node{
			1: {ID: 1, In: edge1, Outs: []*Edge{edge2, edge6, edge8}},
			2: {ID: 2, In: edge2, Outs: []*Edge{edge3}},
			3: {ID: 3, In: edge3, Outs: []*Edge{edge4, edge7}},
			4: {ID: 4, In: edge4, Outs: []*Edge{edge5}},

			5: {ID: 5, In: edge5},

			6: {ID: 6, In: edge6, Outs: []*Edge{edge7}},
			7: {ID: 7, In: edge7, Outs: []*Edge{edge5}},

			8: {ID: 8, In: edge8, Outs: []*Edge{edge5}},
		},
		Edges: []*Edge{edge1, edge2, edge3, edge4, edge5, edge6, edge7, edge8},
	}

	t.Log(`In graph:
    -----> 6 ----------> 7 --
  /                 /         \
 /                 /           \
1 -----> 2 -----> 3 -----> 4 ----->5
 \                             /
  \                           /
    -----------> 8 ----------`)

	wantCriticalPath := []ninjalog.Step{step1, step2, step3, step4, step5}

	got, err := g.CriticalPath()
	if err != nil {
		t.Fatalf("CriticalPath failed: %v", err)
	}
	if diff := cmp.Diff(wantCriticalPath, got); diff != "" {
		t.Errorf("CriticalPath got: %v, want: %v, diff (-want +got):\n%s", got, wantCriticalPath, diff)
	}

	got, err = g.CriticalPathV2()
	if err != nil {
		t.Fatalf("CriticalPathV2 failed: %v", err)
	}
	if diff := cmp.Diff(wantCriticalPath, got); diff != "" {
		t.Errorf("CriticalPathV2 got: %v, want: %v, diff (-want +got):\n%s", got, wantCriticalPath, diff)
	}

	for _, want := range []struct {
		input           *Edge
		parallelizables []*Edge
	}{
		{input: edge1},
		{input: edge2, parallelizables: []*Edge{edge6, edge8}},
		{input: edge3, parallelizables: []*Edge{edge6, edge8}},
		{input: edge4, parallelizables: []*Edge{edge7, edge8}},
		{input: edge5},
	} {
		got, err := g.parallelizableEdges(want.input)
		if err != nil {
			t.Errorf("parallelizableEdges(edge outputting %v) error: %v", want.input.Outputs, err)
		}
		if !cmp.Equal(got, want.parallelizables, cmpopts.IgnoreUnexported(Edge{})) {
			t.Errorf("parallelizableEdges(edge outputting %v) = %s, want: %s", want.input.Outputs, edgesToStr(got), edgesToStr(want.parallelizables))
		}
	}

	for _, want := range []struct {
		input          *Edge
		earliestStart  time.Duration
		earliestFinish time.Duration
		latestStart    time.Duration
		latestFinish   time.Duration
		totalFloat     time.Duration
	}{
		{
			input:          edge1,
			earliestStart:  0,
			earliestFinish: 10,
			latestStart:    0,
			latestFinish:   10,
			totalFloat:     0,
		},
		{
			input:          edge2,
			earliestStart:  10,
			earliestFinish: 30,
			latestStart:    10,
			latestFinish:   30,
			totalFloat:     0,
		},
		{
			input:          edge3,
			earliestStart:  30,
			earliestFinish: 35,
			latestStart:    30,
			latestFinish:   35,
			totalFloat:     0,
		},
		{
			input:          edge4,
			earliestStart:  35,
			earliestFinish: 45,
			latestStart:    35,
			latestFinish:   45,
			totalFloat:     0,
		},
		{
			input:          edge5,
			earliestStart:  45,
			earliestFinish: 65,
			latestStart:    45,
			latestFinish:   65,
			totalFloat:     0,
		},
		{
			input:          edge6,
			earliestStart:  10,
			earliestFinish: 25,
			latestStart:    25,
			latestFinish:   40,
			totalFloat:     15,
		},
		{
			input:          edge7,
			earliestStart:  35,
			earliestFinish: 40,
			latestStart:    40,
			latestFinish:   45,
			totalFloat:     5,
		},
		{
			input:          edge8,
			earliestStart:  10,
			earliestFinish: 25,
			latestStart:    30,
			latestFinish:   45,
			totalFloat:     20,
		},
	} {
		for _, fn := range []struct {
			name string
			f    func(*Edge) (time.Duration, error)
			want time.Duration
		}{
			{name: "earliestStart", f: g.earliestStart, want: want.earliestStart},
			{name: "earliestFinish", f: g.earliestFinish, want: want.earliestFinish},
			{name: "latestStart", f: g.latestStart, want: want.latestStart},
			{name: "latestFinish", f: g.latestFinish, want: want.latestFinish},
			{name: "totalFloat", f: g.totalFloat, want: want.totalFloat},
		} {
			if got := mustDuration(t, want.input, fn.f); got != fn.want {
				t.Errorf("%s(edge outputting %v) = %s, want: %s", fn.name, want.input.Outputs, got, fn.want)
			}
		}
	}

	for _, want := range []struct {
		input *Edge
		err   bool
		drag  time.Duration
	}{
		{input: edge1, drag: 10},
		{input: edge2, drag: 15},
		{input: edge3, drag: 5},
		{input: edge4, drag: 5},
		{input: edge5, drag: 20},
		{input: edge6, err: true},
		{input: edge7, err: true},
		{input: edge8, err: true},
	} {
		got, err := g.drag(want.input)
		if (err != nil) != want.err {
			t.Errorf("drag(edge outputting %v) got error: %v, want error: %t", want.input.Outputs, err, want.err)
		}
		if got != want.drag {
			t.Errorf("drag(edge outputting %v) = %s, want: %s", want.input.Outputs, got, want.drag)
		}
	}

	wantSteps := []ninjalog.Step{
		withFloatDrag(step1, floatDrag{float: 0, onCriticalPath: true, drag: 10}),
		withFloatDrag(step2, floatDrag{float: 0, onCriticalPath: true, drag: 15}),
		withFloatDrag(step3, floatDrag{float: 0, onCriticalPath: true, drag: 5}),
		withFloatDrag(step4, floatDrag{float: 0, onCriticalPath: true, drag: 5}),
		withFloatDrag(step5, floatDrag{float: 0, onCriticalPath: true, drag: 20}),
		withFloatDrag(step6, floatDrag{float: 15, drag: 0}),
		withFloatDrag(step7, floatDrag{float: 5, drag: 0}),
		withFloatDrag(step8, floatDrag{float: 20, drag: 0}),
	}
	gotSteps, err := g.PopulatedSteps()
	if err != nil {
		t.Fatalf("StepsWithFloatDrag() got error: %v", err)
	}
	if diff := cmp.Diff(wantSteps, gotSteps); diff != "" {
		t.Errorf("StepsWithFloatDrag() got diff (-want, +got):\n%s", diff)
	}
}

type floatDrag struct {
	float, drag    time.Duration
	onCriticalPath bool
}

func withFloatDrag(s ninjalog.Step, fd floatDrag) ninjalog.Step {
	s.TotalFloat = fd.float
	s.Drag = fd.drag
	s.OnCriticalPath = fd.onCriticalPath
	return s
}

func edgesToStr(edges []*Edge) string {
	var ss []string
	for _, e := range edges {
		ss = append(ss, fmt.Sprintf("edge outputting %v", e.Outputs))
	}
	return "[" + strings.Join(ss, ", ") + "]"
}

func mustDuration(t *testing.T, edge *Edge, f func(*Edge) (time.Duration, error)) time.Duration {
	t.Helper()
	d, err := f(edge)
	if err != nil {
		t.Fatalf("Failed to get duration: %v", err)
	}
	return d
}
