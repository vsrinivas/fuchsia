// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package affectedtests

import (
	"container/list"
	"regexp"
	"sort"
	"strconv"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/build/lib"
)

var (
	// Example: "0x12345678" [label="../../path/to/myfile.cc"]
	nodePattern = regexp.MustCompile(`^"0x([0-9a-f"]+)" \[label="([^"]+)"`)
	// Example: "0x12345678" -> "0xdeadbeef"
	edgePattern = regexp.MustCompile(`^"0x([0-9a-f]+)" -> "0x([0-9a-f]+)"`)
)

const buildDirRelativePrefix = "../../"

type void struct{}
type stringSet map[string]void
type intSet map[int64]void

var member void

// AffectedTests finds tests out of `testSpecs` that are affected by changes to `srcs` according to the build graph `dotBytes`.
func AffectedTests(srcs []string, testSpecs []build.TestSpec, dotBytes []byte) []string {
	// Parsing the dotfile is the slowest part.
	// Do this first, and parse nodes and edges in parallel.
	dotLines := strings.Split(string(dotBytes), "\n")
	const chunkSize = 10_000
	chunksCount := len(dotLines) / chunkSize

	// Parse nodes
	nodeToOutputCh := make(chan map[int64]string)
	nodeToOutputChunkCh := make(chan map[int64]string, chunksCount)
	// Make chunks of nodes
	for i := 0; i < chunksCount; i++ {
		start := i * chunkSize
		end := start + chunkSize
		if end > len(dotLines) {
			end = len(dotLines)
		}
		go func(chunkLines []string) {
			chunk := make(map[int64]string)
			for _, line := range chunkLines {
				if match := nodePattern.FindStringSubmatch(line); match != nil {
					// Safe to ignore error because the regex only matches hex digits
					node, _ := strconv.ParseInt(match[1], 16, 0)
					chunk[node] = match[2]
				}
			}
			nodeToOutputChunkCh <- chunk
		}(dotLines[start:end])
	}
	// Join all node chunks
	go func() {
		nodeToOutput := make(map[int64]string)
		for i := 0; i < chunksCount; i++ {
			chunk := <-nodeToOutputChunkCh
			for k, v := range chunk {
				nodeToOutput[k] = v
			}
		}
		nodeToOutputCh <- nodeToOutput
	}()

	// Parse edges
	edgesCh := make(chan map[int64][]int64)
	edgesChunkCh := make(chan map[int64][]int64, chunksCount)
	// Make chunks of edges
	for i := 0; i < chunksCount; i++ {
		start := i * chunkSize
		end := start + chunkSize
		if end > len(dotLines) {
			end = len(dotLines)
		}
		go func(chunkLines []string) {
			chunk := make(map[int64][]int64)
			for _, line := range chunkLines {
				if match := edgePattern.FindStringSubmatch(line); match != nil {
					// Safe to ignore error because the regex only matches hex digits
					src, _ := strconv.ParseInt(match[1], 16, 0)
					dst, _ := strconv.ParseInt(match[2], 16, 0)
					chunk[src] = append(chunk[src], dst)
				}
			}
			edgesChunkCh <- chunk
		}(dotLines[start:end])
	}
	// Join all chunks
	go func() {
		edges := make(map[int64][]int64)
		for i := 0; i < chunksCount; i++ {
			chunk := <-edgesChunkCh
			for k, v := range chunk {
				edges[k] = append(edges[k], v...)
			}
		}
		edgesCh <- edges
	}()

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
	nodeToOutput := <-nodeToOutputCh
	for node, output := range nodeToOutput {
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
	edges := <-edgesCh
	for e := nodesToVisit.Front(); e != nil; e = e.Next() {
		var node int64 = e.Value.(int64)
		nodesVisited[node] = member
		for _, node := range edges[node] {
			_, exists := nodesVisited[node]
			if !exists {
				nodesToVisit.PushBack(node)
			}
		}
	}

	affectedTestStamps := make(stringSet)
	for node := range nodesVisited {
		output := nodeToOutput[node]
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
	return affectedTestNames
}
