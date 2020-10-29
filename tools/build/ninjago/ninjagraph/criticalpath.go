// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ninjagraph

import (
	"fmt"
	"math"
	"sort"
	"time"

	"go.fuchsia.dev/fuchsia/tools/build/ninjago/ninjalog"
)

const ninjagoArtificialSinkRule = "🏁ninjago_artificial_sink_rule"

// buildTimeOf returns the total amount of time spent to produce the output.
//
// This function also memoizes results on the nodes to avoid repeated work.
func (g *Graph) buildTimeOf(id int64) (time.Duration, error) {
	node, ok := g.Nodes[id]
	if !ok {
		return 0, fmt.Errorf("node %x not found", id)
	}
	// Root nodes take no time to build since they are readily available at the
	// beginning of the build.
	if node.In == nil {
		return 0, nil
	}

	// Return memoized results immediate to avoid repeated traversal of
	// overlapping parts of the graph.
	if node.criticalBuildDuration != nil {
		return *node.criticalBuildDuration, nil
	}

	if node.In.Rule != "phony" && node.In.Step == nil {
		return 0, fmt.Errorf("input edge to node %q has not step associated to it, this should not happen if edges are correctly populated", node.Path)
	}

	var maxBuildTime time.Duration
	for _, input := range node.In.Inputs {
		d, err := g.buildTimeOf(input)
		if err != nil {
			return 0, fmt.Errorf("failed to get build time of input node %x: %v", input, err)
		}
		if d >= maxBuildTime {
			node.criticalInput = g.Nodes[input]
			maxBuildTime = d
		}
	}

	if node.In.Step != nil {
		maxBuildTime += node.In.Step.Duration()
	}
	node.criticalBuildDuration = &maxBuildTime

	return maxBuildTime, nil
}

// CriticalPath returns the build path that takes the longest time to finish.
// That is, the sum of build durations on edges along this path is the largest
// among all build paths.
//
// `Step`s on all non-phony edges must be populated before this function is
// called, otherwise an error is returned.
func (g *Graph) CriticalPath() ([]ninjalog.Step, error) {
	var lastCriticalNode *Node
	var maxBuildDuration time.Duration
	for id, node := range g.Nodes {
		d, err := g.buildTimeOf(id)
		if err != nil {
			return nil, fmt.Errorf("failed to construct critical path: %v", err)
		}
		if d >= maxBuildDuration {
			maxBuildDuration = d
			lastCriticalNode = node
		}
	}

	if lastCriticalNode == nil {
		return nil, nil
	}

	var criticalPath []ninjalog.Step
	for lastCriticalNode != nil {
		n := lastCriticalNode
		lastCriticalNode = lastCriticalNode.criticalInput

		// Pure input files (for example source code files) don't have In edges on them.
		if n.In == nil {
			continue
		}
		// Phony edges don't have steps associated with them.
		if n.In.Step == nil {
			continue
		}
		criticalPath = append(criticalPath, *n.In.Step)
	}
	// Reverse the critical path to follow chronological order.
	for left, right := 0, len(criticalPath)-1; left < right; left, right = left+1, right-1 {
		criticalPath[left], criticalPath[right] = criticalPath[right], criticalPath[left]
	}
	return criticalPath, nil
}

// addSink adds a sink edge to the graph.
//
// The added edge takes all pure outputs (output files that are not inputs to
// any existing edges) as input, and outputs nothing. This edge marks the
// completion of the build. It is necessary for calculating critical path using
// float, because floats of actions are calculated against the completion of the
// whole build.
func (g *Graph) addSink() error {
	if len(g.Edges) == 0 || g.sink != nil {
		return nil
	}

	var pureOutputs []int64
	for id, n := range g.Nodes {
		if len(n.Outs) == 0 {
			pureOutputs = append(pureOutputs, id)
		}
	}

	if len(pureOutputs) == 0 {
		return fmt.Errorf("the build graph doesn't output anything")
	}

	sink := Edge{
		Inputs: pureOutputs,
		Rule:   ninjagoArtificialSinkRule,
		// A step with 0 duration is necessary to facilitate drag calculation.
		Step: &ninjalog.Step{},
	}
	for _, id := range pureOutputs {
		g.Nodes[id].Outs = []*Edge{&sink}
	}
	g.Edges = append(g.Edges, &sink)
	g.sink = &sink
	return nil
}

// totalFloat calculates total float of an edge. Float is the amount of time an
// action can be delayed without affecting the completion time of the build.
//
// https://en.wikipedia.org/wiki/Float_(project_management)
func (g *Graph) totalFloat(edge *Edge) (time.Duration, error) {
	es, err := g.earliestStart(edge)
	if err != nil {
		return 0, fmt.Errorf("calculating earliest start: %w", err)
	}
	ls, err := g.latestStart(edge)
	if err != nil {
		return 0, fmt.Errorf("calculating latest start: %w", err)
	}
	return ls - es, nil
}

// earliestStart returns the earliest time an edge can start in the build.
func (g *Graph) earliestStart(edge *Edge) (time.Duration, error) {
	if edge.earliestStart != nil {
		return *edge.earliestStart, nil
	}

	// Earliest start of this action is the latest earliest finish of all its
	// dependencies.
	var es time.Duration
	for _, input := range edge.Inputs {
		n, ok := g.Nodes[input]
		if !ok {
			return 0, fmt.Errorf("node %x not found", input)
		}
		in := n.In
		if in == nil {
			// The input node is a pure input (for example source code file), so
			// there's no action generating that node.
			continue
		}
		ef, err := g.earliestFinish(in)
		if err != nil {
			return 0, fmt.Errorf("calculating earliest start for action generating %s: %v", n.Path, err)
		}
		if ef > es {
			es = ef
		}
	}
	edge.earliestStart = &es
	return es, nil
}

// earliestFinish returns the earliest time an edge can finish in the build.
func (g *Graph) earliestFinish(edge *Edge) (time.Duration, error) {
	es, err := g.earliestStart(edge)
	if err != nil {
		return 0, err
	}
	// Phony rules don't actually run anything, so they have a duration of 0.
	if edge.Rule == "phony" {
		return es, nil
	}
	if edge.Step == nil {
		return 0, fmt.Errorf("step is missing on edge, step can be populated by calling `PopulateEdges`")
	}
	return es + edge.Step.Duration(), nil
}

// latestStart returns the latest time an edge can start in the build.
func (g *Graph) latestStart(edge *Edge) (time.Duration, error) {
	lf, err := g.latestFinish(edge)
	if err != nil {
		return 0, err
	}
	// Phony rules don't actually run anything, so they have a duration of 0.
	if edge.Rule == "phony" {
		return lf, nil
	}
	if edge.Step == nil {
		return 0, fmt.Errorf("step is missing on edge, step can be populated by calling `PopulateEdges`")
	}
	return lf - edge.Step.Duration(), nil
}

// latestFinish returns the latest time an edge can finish in the build.
func (g *Graph) latestFinish(edge *Edge) (time.Duration, error) {
	if edge.latestFinish != nil {
		return *edge.latestFinish, nil
	}
	if len(edge.Outputs) == 0 {
		return g.earliestFinish(edge)
	}

	// Latest finish of this action is the earliest latest start of all its
	// dependents.
	lf := time.Duration(math.MaxInt64)
	for _, output := range edge.Outputs {
		n, ok := g.Nodes[output]
		if !ok {
			return 0, fmt.Errorf("node %x not found", output)
		}
		for _, out := range n.Outs {
			ls, err := g.latestStart(out)
			if err != nil {
				return 0, err
			}
			if ls < lf {
				lf = ls
			}
		}
	}
	edge.latestFinish = &lf
	return lf, nil
}

// CriticalPathV2 calculates critical path by looking for all actions with zero
// float.
//
// A critical path is the path through the build that results in the latest
// completion of the build.
func (g *Graph) CriticalPathV2() ([]ninjalog.Step, error) {
	if len(g.Edges) == 0 {
		return nil, nil
	}

	if err := g.addSink(); err != nil {
		return nil, fmt.Errorf("adding sink to graph: %w", err)
	}

	criticalEdge := g.sink
	var criticalPath []ninjalog.Step
	for criticalEdge != nil {
		if criticalEdge.Rule != "phony" && criticalEdge.Rule != ninjagoArtificialSinkRule {
			if criticalEdge.Step == nil {
				return nil, fmt.Errorf("step is missing on edge, step can be populated by calling `PopulateEdges`")
			}
			criticalPath = append(criticalPath, *criticalEdge.Step)
		}

		var nextCriticalEdge *Edge
		for _, id := range criticalEdge.Inputs {
			n, ok := g.Nodes[id]
			if !ok {
				return nil, fmt.Errorf("node %x not found", id)
			}
			if n.In == nil {
				continue
			}
			tf, err := g.totalFloat(n.In)
			if err != nil {
				return nil, fmt.Errorf("calculating total float: %w", err)
			}
			if tf != 0 {
				continue
			}

			if nextCriticalEdge == nil {
				nextCriticalEdge = n.In
				continue
			}
			// Among all the inputs, pick the one that finishes the last, so we don't
			// accidentally skip actions when multiple inputs of the same node are on
			// the critical path.
			//
			// For example, imagine a graph:
			// 1 -> 2 -------> 4
			//       \     /
			//        -> 3
			// When we are at 4, we want to pick up 3 as the next step, not 2.
			lfNew, err := g.latestFinish(n.In)
			if err != nil {
				return nil, fmt.Errorf("calculating latest finish: %w", err)
			}
			lfCur, err := g.latestFinish(nextCriticalEdge)
			if err != nil {
				return nil, fmt.Errorf("calculating latest finish: %w", err)
			}
			if lfNew > lfCur {
				nextCriticalEdge = n.In
			}
		}
		criticalEdge = nextCriticalEdge
	}

	sort.Slice(criticalPath, func(i, j int) bool { return criticalPath[i].Start < criticalPath[j].Start })
	return criticalPath, nil
}

// parallelizableEdges returns all edges that can be run in parallel with the
// input edge.
func (g *Graph) parallelizableEdges(edge *Edge) ([]*Edge, error) {
	if edge.Step == nil {
		return nil, fmt.Errorf("input edge has no step associated to it, this should not happen if edges are correctly populated")
	}

	latestFinish, err := g.latestFinish(edge)
	if err != nil {
		return nil, err
	}

	var ret []*Edge
	for _, e := range g.Edges {
		if e == edge || e.Rule == "phony" || e.Rule == ninjagoArtificialSinkRule {
			continue
		}
		if e.Step == nil {
			return nil, fmt.Errorf("step is missing on edge, step can be populated by calling `PopulateEdges`")
		}

		es, err := g.earliestStart(e)
		if err != nil {
			return nil, err
		}
		lf, err := g.latestFinish(e)
		if err != nil {
			return nil, err
		}

		// This check covers 2 possibilities:
		//
		// edge:     |oooooooo|
		// e:    |-----------------|
		//
		// or
		//
		// edge: |ooooooooo|
		// e:       |------------|
		if lf >= latestFinish && es < latestFinish {
			ret = append(ret, e)
			continue
		}
	}
	return ret, nil
}

// drag returns drag of the input edge. Drag is the amount of time an action
// on the critical path is adding to the total build time.
//
// NOTE: drag calculation assumes unconstrained parallelism, such that actions
// can always begin as soon as their inputs are satisfied and never wait to be
// scheduled on an available machine resources (CPU, RAM, I/O).
//
// https://en.wikipedia.org/wiki/Critical_path_drag
func (g *Graph) drag(edge *Edge) (time.Duration, error) {
	if edge.Step == nil {
		return 0, fmt.Errorf("step is missing on edge, step can be populated by calling `PopulateEdges`")
	}

	tf, err := g.totalFloat(edge)
	if err != nil {
		return 0, fmt.Errorf("calculating total float: %w", err)
	}
	if tf != 0 {
		// This edge is not on the critical path, so drag = 0.
		return 0, fmt.Errorf("drag called for non-critical edge")
	}

	edges, err := g.parallelizableEdges(edge)
	if err != nil {
		return 0, fmt.Errorf("getting parallelizable edges: %w", err)
	}

	drag := edge.Step.Duration()
	for _, e := range edges {
		tf, err := g.totalFloat(e)
		if err != nil {
			return 0, fmt.Errorf("calculating total float of parallel edges: %w", err)
		}
		if tf < drag {
			drag = tf
		}
	}
	return drag, nil
}

// PopulatedSteps returns all steps previously associated with the build
// graph through `PopulateEdges`, but with `OnCriticalPath`, `TotalFloat` and
// `Drag` set.
func (g *Graph) PopulatedSteps() ([]ninjalog.Step, error) {
	if len(g.Edges) == 0 {
		return nil, nil
	}

	criticalPath, err := g.CriticalPathV2()
	if err != nil {
		return nil, fmt.Errorf("calculating critical path: %v", err)
	}

	// NOTE: this assumes all outputs have unique names, which should always be
	// true for a real build because the same output cannot be generated by
	// multiple actions.
	onCriticalPath := make(map[string]bool)
	for _, step := range criticalPath {
		onCriticalPath[step.Out] = true
	}

	var steps []ninjalog.Step
	for _, e := range g.Edges {
		if e.Step == nil || e.Rule == ninjagoArtificialSinkRule {
			continue
		}
		s := *e.Step
		tf, err := g.totalFloat(e)
		if err != nil {
			return nil, fmt.Errorf("calculating total float: %w", err)
		}
		s.TotalFloat = tf

		if tf == 0 {
			drag, err := g.drag(e)
			if err != nil {
				return nil, fmt.Errorf("calculating drag: %w", err)
			}
			s.Drag = drag
		}

		// TODO(jayzhuang): if multiple parallel actions are on the critical path,
		// we currently only include one of them. Consider including them all and
		// update rest of the tooling, for example update flow events in ninjatrace.
		s.OnCriticalPath = onCriticalPath[s.Out]

		steps = append(steps, s)
	}
	return steps, nil
}
