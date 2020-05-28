package affectedtests

import (
	"container/list"
	"go.fuchsia.dev/fuchsia/tools/build/lib"
	"regexp"
	"sort"
	"strconv"
	"strings"
)

var (
	// Example: "0x12345678" [label="../../path/to/myfile.cc"]
	nodePattern = regexp.MustCompile(`(?m)^"0x([^"]*)" \[label="([^"]*)"`)

	// Example: "0x12345678" -> "0xdeadbeef"
	edgePattern = regexp.MustCompile(`(?m)^"0x([^"]*)" -> "0x([^"]*)"`)
)

const buildDirRelativePrefix = "../../"

type void struct{}
type stringSet map[string]void
type intSet map[int64]void

var member void

// AffectedTests finds tests out of `testSpecs` that are affected by changes to `srcs` according to the build graph `dotBytes`.
func AffectedTests(srcs []string, testSpecs []build.TestSpec, dotBytes []byte) []string {
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

	dotStr := string(dotBytes)
	nodeToOutput := make(map[int64]string)
	for _, match := range nodePattern.FindAllStringSubmatch(dotStr, -1) {
		node, _ := strconv.ParseInt(match[1], 16, 0)
		nodeToOutput[node] = match[2]
	}

	edges := make(map[int64][]int64)
	for _, match := range edgePattern.FindAllStringSubmatch(dotStr, -1) {
		src, _ := strconv.ParseInt(match[1], 16, 0)
		dst, _ := strconv.ParseInt(match[2], 16, 0)
		edges[src] = append(edges[src], dst)
	}

	srcsSet := make(stringSet)
	for _, src := range srcs {
		srcsSet[src] = member
	}

	nodesToVisit := list.New()
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
