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
	"regexp"
	"runtime"
	"strconv"
	"strings"
	"sync"

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
}

// Edge is an edge in Ninja's build graph. It links nodes with a build rule.
type Edge struct {
	Inputs  []int64
	Outputs []int64
	Rule    string
}

// Graph is a Ninja build graph.
type Graph struct {
	// Nodes maps from node IDs to nodes.
	Nodes map[int64]*Node
	// Edges contains all edges in the graph.
	Edges []*Edge
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

	for _, output := range e.Outputs {
		n, ok := g.Nodes[output]
		if !ok {
			// TODO(jayzhuang): there are plenty of edges, in the test ninja.dot file
			// at least, pointing to non-existent nodes. Figure out why and whether we
			// should return error here.
			continue
		}
		if n.In != nil {
			return fmt.Errorf("multiple edges claim to produce %x as output", output)
		}
		n.In = e
	}

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
				es = []*Edge{}
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
				es = []*Edge{}
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
