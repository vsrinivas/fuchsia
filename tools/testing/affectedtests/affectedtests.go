// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package affectedtests

import (
	"container/list"
	"fmt"
	"io"
	"sort"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/build"
	"go.fuchsia.dev/fuchsia/tools/build/ninjagraph"
)

const buildDirRelativePrefix = "../../"

type void struct{}
type stringSet map[string]void
type intSet map[int64]void

var member void

// AffectedTests finds tests out of `testSpecs` that are affected by changes to `srcs` according to the build graph `dotBytes`.
func AffectedTests(srcs []string, testSpecs []build.TestSpec, dotFile io.Reader) ([]string, error) {
	graph, err := ninjagraph.FromDOT(dotFile)
	if err != nil {
		return nil, err
	}

	testLabelToName := make(map[string][]string)
	for _, testSpec := range testSpecs {
		test := testSpec.Test
		label := test.Label
		testLabelToName[label] = append(testLabelToName[label], test.Name)
	}

	testStampToNames := make(map[string][]string)
	for label, names := range testLabelToName {
		// //this/is/a:label(//some:toolchain) -> obj/this/is/a/label.stamp
		stamp := "obj/" + strings.Replace(label[2:strings.IndexByte(label, '(')], ":", "/", 1) + ".stamp"
		testStampToNames[stamp] = names
	}

	srcsSet := make(stringSet)
	for _, src := range srcs {
		srcsSet[src] = member
	}

	nodesToVisit := list.New()
	for id, node := range graph.Nodes {
		// Cut leading "../../" to rebase output to root build dir
		if !strings.HasPrefix(node.Path, buildDirRelativePrefix) {
			continue
		}
		if _, exists := srcsSet[node.Path[len(buildDirRelativePrefix):]]; exists {
			nodesToVisit.PushBack(id)
		}
	}

	nodesVisited := make(intSet)
	for e := nodesToVisit.Front(); e != nil; e = e.Next() {
		var nodeID int64 = e.Value.(int64)
		nodesVisited[nodeID] = member

		node := graph.Nodes[nodeID]
		if node == nil {
			return nil, fmt.Errorf("node %x not found in graph, this indicates a bug in graph traversal", nodeID)
		}
		for _, edge := range node.Outs {
			for _, outputNodeID := range edge.Outputs {
				if _, exists := nodesVisited[outputNodeID]; !exists {
					nodesToVisit.PushBack(outputNodeID)
				}
			}
		}
	}

	affectedTestStamps := make(stringSet)
	for nodeID := range nodesVisited {
		node := graph.Nodes[nodeID]
		if node == nil {
			return nil, fmt.Errorf("node %x not found in graph, this indicates a bug in graph traversal", nodeID)
		}
		if _, exists := testStampToNames[node.Path]; exists {
			affectedTestStamps[node.Path] = member
		}
	}

	affectedTestNames := []string{}
	for stamp := range affectedTestStamps {
		affectedTestNames = append(affectedTestNames, testStampToNames[stamp]...)
	}

	sort.Strings(affectedTestNames)
	return affectedTestNames, nil
}
