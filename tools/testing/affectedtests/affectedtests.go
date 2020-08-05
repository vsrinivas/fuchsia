// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package affectedtests

import (
	"container/list"
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
	for node, output := range graph.NodeToPath {
		// Cut leading "../../" to rebase output to root build dir
		if !strings.HasPrefix(output, buildDirRelativePrefix) {
			continue
		}
		_, exists := srcsSet[output[len(buildDirRelativePrefix):]]
		if exists {
			nodesToVisit.PushBack(node)
		}
	}

	nodesVisited := make(intSet)
	for e := nodesToVisit.Front(); e != nil; e = e.Next() {
		var node int64 = e.Value.(int64)
		nodesVisited[node] = member
		for _, node := range graph.Edges[node] {
			_, exists := nodesVisited[node]
			if !exists {
				nodesToVisit.PushBack(node)
			}
		}
	}

	affectedTestStamps := make(stringSet)
	for node := range nodesVisited {
		output := graph.NodeToPath[node]
		_, exists := testStampToNames[output]
		if exists {
			affectedTestStamps[output] = member
		}
	}

	affectedTestNames := []string{}
	for stamp := range affectedTestStamps {
		affectedTestNames = append(affectedTestNames, testStampToNames[stamp]...)
	}

	sort.Strings(affectedTestNames)
	return affectedTestNames, nil
}
