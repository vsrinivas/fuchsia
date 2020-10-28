// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ninjagraph provides utilities to parse the DOT output from Ninja's `-t graph`
// tool to a Go native format.
package ninjagraph

import (
	"bufio"
	"fmt"
	"io"
	"math"
	"regexp"
	"runtime"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"

	"go.fuchsia.dev/fuchsia/tools/build/ninjago/ninjalog"
	"golang.org/x/sync/errgroup"
)

var (
	// Example: "0x1234567" [label="../../path/to/myfile.cc"]
	nodePattern = regexp.MustCompile(`^"0x([0-9a-f"]+)" \[label="([^"]+)"`)
	// Example:
	//   "0x1234567" -> "0x7654321"
	//   or
	//   "0x1234567" -> "0x7654321" [label=" host_x64_cxx"]
	//
	// Labels for edges currently have a leading space. Optionally match it just
	// in case it's removed in the future.
	// https://github.com/ninja-build/ninja/blob/6c5e886aacd98766fe43539c2c8ae7f3ca2af2aa/src/graphviz.cc#L53
	edgePattern = regexp.MustCompile(`^"0x([0-9a-f]+)" -> "0x([0-9a-f]+)"(?: \[label=" ?([^"]+)")?`)
)

const ninjagoArtificialSinkRule = "🏁ninjago_artificial_sink_rule"

// graphvizNode is a node in Ninja's Graphviz output.
//
// It is possible for a graphviz node to represent an edge with multiple
// inputs/outputs in Ninja's build graph.
type graphvizNode struct {
	id    int64
	label string
	// isNinjaEdge is true if this Graphviz node represents a Ninja build edge.
	isNinjaEdge bool
}

// graphvizEdge is an edge in Ninja's Graphviz output.
type graphvizEdge struct {
	from, to int64
	// If label is non-empty, this edge represents a build edge in Ninja's build
	// graph, and label is the rule for this build edge.
	label string
}

func graphvizNodeFrom(line string) (graphvizNode, bool, error) {
	match := nodePattern.FindStringSubmatch(line)
	if match == nil {
		return graphvizNode{}, false, nil
	}

	id, err := strconv.ParseInt(match[1], 16, 64)
	if err != nil {
		return graphvizNode{}, false, err
	}

	return graphvizNode{
		id:    id,
		label: match[2],
		// A Graphviz node can represent a Ninja build edge with multiple inputs and
		// outputs, in this case Ninja draws a ellipse instead of a rectangle.
		isNinjaEdge: strings.HasSuffix(line, "shape=ellipse]"),
	}, true, nil
}

func graphvizEdgeFrom(line string) (graphvizEdge, bool, error) {
	match := edgePattern.FindStringSubmatch(line)
	if match == nil {
		return graphvizEdge{}, false, nil
	}

	from, err := strconv.ParseInt(match[1], 16, 64)
	if err != nil {
		return graphvizEdge{}, false, err
	}
	to, err := strconv.ParseInt(match[2], 16, 64)
	if err != nil {
		return graphvizEdge{}, false, err
	}
	var label string
	if len(match) > 3 {
		label = match[3]
	}

	return graphvizEdge{
		from:  from,
		to:    to,
		label: label,
	}, true, nil
}

// Node is a node in Ninja's build graph. It represents an input/ouput file in
// the build.
type Node struct {
	// ID is an unique ID of this node.
	ID int64
	// Path is the path to the input/output file this node represents.
	Path string
	// In is the edge that produced this node. Nil if this node is not produced by
	// any rule.
	In *Edge
	// Outs contains all edges that use this node as input.
	Outs []*Edge

	// Fields below are used to memoize search results while looking up the
	// critical path.

	// criticalInput points to the one of inputs used to produce this node, which
	// took the longest time to build.
	criticalInput *Node
	// criticalBuildDuration is the sum of build durations of all edges along the
	// critical path that produced this output.
	criticalBuildDuration *time.Duration
}

// Edge is an edge in Ninja's build graph. It links nodes with a build rule.
type Edge struct {
	Inputs  []int64
	Outputs []int64
	Rule    string

	// Fields below are populated after `PopulateEdges`.

	// Step is a build step associated with this edge. It can be derived from
	// joining with ninja_log. After the join, all steps on non-phony edges are
	// populated.
	Step *ninjalog.Step

	// Fields to memoize earliest start and latest finish for critical path
	// calculation based on float.
	//
	// https://en.wikipedia.org/wiki/Critical_path_method
	earliestStart, latestFinish *time.Duration
}

// Graph is a Ninja build graph.
type Graph struct {
	// Nodes maps from node IDs to nodes.
	Nodes map[int64]*Node
	// Edges contains all edges in the graph.
	Edges []*Edge

	// sink is an edge that marks the completion of the build.
	//
	// This edge will only be populated after `addSink`.
	sink *Edge
}

// addEdge adds an edge to the graph and updates the related nodes accordingly.
func (g *Graph) addEdge(e *Edge) error {
	for _, input := range e.Inputs {
		n, ok := g.Nodes[input]
		if !ok {
			return fmt.Errorf("node %x not found, while an edge claims to use it as input", input)
		}
		n.Outs = append(n.Outs, e)
	}

	var outputs []int64
	for _, output := range e.Outputs {
		n, ok := g.Nodes[output]
		if !ok {
			// Skip this output because it is not included in the build graph.
			//
			// This is possible when a rule produces multiple outputs, and some of
			// those outputs are not explicitly included in the build graph. For
			// example, some rule produces both stripped and unstripped versions of a
			// binary, and the unstripped ones are not included in the build graph.
			continue
		}
		outputs = append(outputs, output)
		if n.In != nil {
			return fmt.Errorf("multiple edges claim to produce %x as output", output)
		}
		n.In = e
	}

	e.Outputs = outputs
	g.Edges = append(g.Edges, e)
	return nil
}

// FromDOT reads all lines from the input reader and parses it into a `Graph`.
//
// This function expects the content to be parsed to follow the Ninja log format,
// otherwise the results is undefined.
func FromDOT(r io.Reader) (Graph, error) {
	var gNodes []graphvizNode
	var gEdges []graphvizEdge

	gNodesCh := make(chan []graphvizNode)
	gEdgesCh := make(chan []graphvizEdge)

	// If a Ninja build edge contains zero or multiple inputs and multiple outputs,
	// it is represented as a Graphviz node + edges connected to input and output
	// nodes. These two indexes are useful later when we convert the Graphviz
	// representation back into a Ninja build edge.
	edgesBySrc := make(map[int64][]graphvizEdge)
	edgesByDst := make(map[int64][]graphvizEdge)

	wg := sync.WaitGroup{}
	go func() {
		wg.Add(1)
		defer wg.Done()
		for ns := range gNodesCh {
			gNodes = append(gNodes, ns...)
		}
	}()
	go func() {
		wg.Add(1)
		defer wg.Done()
		for es := range gEdgesCh {
			gEdges = append(gEdges, es...)
			for _, e := range es {
				edgesBySrc[e.from] = append(edgesBySrc[e.from], e)
				edgesByDst[e.to] = append(edgesByDst[e.to], e)
			}
		}
	}()

	eg := errgroup.Group{}
	sem := make(chan struct{}, runtime.GOMAXPROCS(0)-2)
	s := bufio.NewScanner(r)
	for s.Scan() {
		sem <- struct{}{}

		// Chunk the lines to reduce channel IO, this significantly reduces runtime.
		lines := []string{s.Text()}
		for i := 0; i < 10_000 && s.Scan(); i++ {
			lines = append(lines, s.Text())
		}

		eg.Go(func() error {
			defer func() { <-sem }()

			var ns []graphvizNode
			var es []graphvizEdge

			for _, line := range lines {
				n, ok, err := graphvizNodeFrom(line)
				if err != nil {
					return err
				}
				if ok {
					ns = append(ns, n)
					continue
				}

				e, ok, err := graphvizEdgeFrom(line)
				if err != nil {
					return err
				}
				if ok {
					es = append(es, e)
					continue
				}
			}

			gNodesCh <- ns
			gEdgesCh <- es
			return nil
		})
	}

	if err := eg.Wait(); err != nil {
		return Graph{}, err
	}
	close(gNodesCh)
	close(gEdgesCh)
	wg.Wait()

	if err := s.Err(); err != nil && err != io.EOF {
		return Graph{}, err
	}

	// We've done parsing of the Graphviz representation, now map it back to the
	// actual Ninja build graph.
	g := Graph{
		Nodes: make(map[int64]*Node),
	}

	// Collect all Graphviz nodes that represent Ninja build edges and assemble
	// them later.
	var edgeNodes []graphvizNode
	// Later steps require all nodes to be present in the graph, so this step
	// cannot be done in parallel. This is fine because the number of nodes is
	// usually much smaller than the number of edges.
	for _, n := range gNodes {
		if n.isNinjaEdge {
			edgeNodes = append(edgeNodes, n)
			continue
		}
		g.Nodes[n.id] = &Node{ID: n.id, Path: n.label}
	}

	wg.Add(2)
	edges := make(chan []*Edge)
	go func() {
		defer wg.Done()

		var es []*Edge
		for _, edge := range gEdges {
			if edge.label == "" {
				continue
			}
			es = append(es, &Edge{
				Inputs:  []int64{edge.from},
				Outputs: []int64{edge.to},
				Rule:    edge.label,
			})
			// Chunk the edges to reduce channel IO, this significantly reduces runtime.
			if len(es) > 10_000 {
				edges <- es
				es = nil
			}
		}
		edges <- es
	}()

	go func() {
		defer wg.Done()

		var es []*Edge
		for _, n := range edgeNodes {
			// If a Ninja build edge is represented as a Graphviz node, all the
			// Graphviz edges pointing to it are connected to inputs, and all the
			// Graphviz edges going out of it are connected to outputs.
			e := &Edge{Rule: n.label}
			for _, from := range edgesByDst[n.id] {
				e.Inputs = append(e.Inputs, from.from)
			}
			for _, to := range edgesBySrc[n.id] {
				e.Outputs = append(e.Outputs, to.to)
			}
			es = append(es, e)
			// Chunk the edges to reduce channel IO, this significantly reduces runtime.
			if len(es) > 10_000 {
				edges <- es
				es = nil
			}
		}
		edges <- es
	}()

	go func() {
		wg.Wait()
		close(edges)
	}()

	for es := range edges {
		for _, e := range es {
			if err := g.addEdge(e); err != nil {
				return Graph{}, err
			}
		}
	}
	return g, nil
}

// PopulateEdges joins the build steps from ninjalog with the graph and
// populates build time on edges.
//
// `steps` should be deduplicated, so they have a 1-to-1 mapping to edges.
func (g *Graph) PopulateEdges(steps []ninjalog.Step) error {
	stepByOut := make(map[string]ninjalog.Step)

	for _, step := range steps {
		// Index all outputs, otherwise the edges can miss steps due to missing
		// nodes. If steps are only indexed on their main output, and that output
		// node is missing from the graph, we can't draw a link between the step and
		// its corresponding edge.
		//
		// For example, if a step has its main output set to `foo`, and also
		// produces `bar` and `baz`, we want to index this step on all three of the
		// outputs. This way, if an edge missed `foo` in its output (possible when
		// the output node is not included in the build graph), it can still be
		// associated with the step since it can match on both other outputs.
		for _, out := range append(step.Outs, step.Out) {
			if _, ok := stepByOut[out]; ok {
				return fmt.Errorf("multiple steps claim to produce the same output %s", out)
			}
			stepByOut[out] = step
		}
	}

	for _, edge := range g.Edges {
		// Skip "phony" builds. For example "build default: phony obj/default.stamp"
		// can be included in the graph.
		if edge.Rule == "phony" {
			continue
		}

		var nodes []*Node
		for _, output := range edge.Outputs {
			node := g.Nodes[output]
			if node == nil {
				return fmt.Errorf("node %x not found, yet an edge claims to produce it, invalid graph", output)
			}
			nodes = append(nodes, node)
		}

		// Look for the corresponding ninjalog step for this edge. We do this by
		// matching outputs: for each output we look for the build step that
		// produced it, and associate that with the edge. Along the way we also
		// check all the outputs claimed by this edge is produced by the same step,
		// so there is a 1-to-1 mapping between them.
		var step *ninjalog.Step
		for _, node := range nodes {
			s, ok := stepByOut[node.Path]
			if !ok {
				return fmt.Errorf("no steps are producing output %s, yet an edge claims to produce it", node.Path)
			}
			if step != nil && step.CmdHash != s.CmdHash {
				return fmt.Errorf("multiple steps match the same edge on outputs %v, previous step: %#v, this step: %#v", pathsOf(nodes), step, s)
			}
			step = &s
		}
		if step == nil {
			return fmt.Errorf("no build steps found for build edge with output(s): %v", pathsOf(nodes))
		}
		edge.Step = step
	}
	return nil
}

func pathsOf(nodes []*Node) []string {
	var paths []string
	for _, n := range nodes {
		paths = append(paths, n.Path)
	}
	return paths
}

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

		// TODO(jayzhuang): validate whether the checks below are good enough to get
		// all parallelizable edges.

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
