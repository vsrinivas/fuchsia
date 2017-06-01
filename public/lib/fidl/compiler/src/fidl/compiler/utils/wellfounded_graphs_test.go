package utils

import (
	"bytes"
	"fmt"
	"strings"
	"testing"
)

////////////////////////////////////////////////////
// SimpleNode type
////////////////////////////////////////////////////

// SimpleNode is a simple implementation of the |Node| interface used
// for testing.
type SimpleNode struct {
	// The value of this node
	value int

	// Is this node a square node?
	isSquare bool

	// The list of out-edges.
	edges []OutEdge

	// Cache the fact that |SetKnownWellFounded| has been invoked.
	knownWellFounded bool
}

func (node *SimpleNode) Name() string {
	return fmt.Sprintf("Node%d", node.value)
}

func (node *SimpleNode) OutEdges() []OutEdge {
	return node.edges
}

func (node *SimpleNode) IsSquare() bool {
	return node.isSquare
}

func (node *SimpleNode) KnownWellFounded() bool {
	return node.knownWellFounded
}

func (node *SimpleNode) SetKnownWellFounded() {
	if node.knownWellFounded {
		panic(fmt.Sprintf("SetKnownWellFounded invoked twice on %s", node.Name()))
	}
	node.knownWellFounded = true
}

//Creates a new SimpleNode.
func NewNode(value, numEdges int, isSquare bool) *SimpleNode {
	node := new(SimpleNode)
	node.value = value
	node.edges = make([]OutEdge, numEdges)
	node.isSquare = isSquare
	return node
}

////////////////////////////////////////////////////
// Test Utilities
////////////////////////////////////////////////////

// Builds a test graph containing only circle nodes.
// The connection matrix should have the following form:
// connectionMatrix[i][j] = k indicates that there is an edge in the graph
// from Node i to Node K. k must be less than len(connectionMatrix).
func buildCircleTestGraph(connectionMatrix [][]int) (nodes []*SimpleNode) {
	return buildTestGraph(connectionMatrix, nil)
}

// Builds a test graph. The connection matrix should have the following form:
// connectionMatrix[i][j] = k indicates that there is an edge in the graph
// from Node i to Node K. k must be less than len(connectionMatrix).
// If |squares| is not nil then it must contain the list of indices of the nodes that are squares.
func buildTestGraph(connectionMatrix [][]int, squares []int) (nodes []*SimpleNode) {
	squareMap := make(map[int]bool)
	if squares != nil {
		for _, n := range squares {
			squareMap[n] = true
		}
	}
	nodes = make([]*SimpleNode, len(connectionMatrix))
	for index, connectionList := range connectionMatrix {
		_, isSquare := squareMap[index]
		nodes[index] = NewNode(index, len(connectionList), isSquare)
	}
	for index, connectionList := range connectionMatrix {
		for i, target := range connectionList {
			nodes[index].edges[i] = OutEdge{"", nodes[target]}
		}
	}
	return
}

// expectedDebugData is used to store an expectation for the value of the |debugData| populated
// by the ethod |checkWellFounded|. This data captures the result of phase 1 of the algorithm.
type expectedDebugData struct {
	initialPendingSet    []string
	initialFoundationSet []string
}

func compareDebugData(expected *expectedDebugData, actual *debugData) error {
	if expected == nil {
		panic("expected cannot be nil")
		return nil
	}
	if err := compareNodeSlice(expected.initialPendingSet, actual.initialPendingSet, "initialPendingSet"); err != nil {
		return err
	}
	return compareNodeSlice(expected.initialFoundationSet, actual.initialFoundationSet, "initialFoundationSet")
}

func compareNodeSlice(expected []string, actual []Node, name string) error {
	expectedMap := make(map[string]bool)
	actualMap := make(map[string]bool)
	for _, n := range expected {
		expectedMap[n] = true
	}
	for _, n := range actual {
		actualMap[n.Name()] = true
	}
	for _, n := range expected {
		if _, ok := actualMap[n]; !ok {
			return fmt.Errorf("%s: missing %s, actual: %s", name, n, NodeSliceToString(actual))
		}
	}
	for _, n := range actual {
		if _, ok := expectedMap[n.Name()]; !ok {
			return fmt.Errorf("%s: unexpected: %s, expected: %v, actual: %s", name, n.Name(), expected, NodeSliceToString(actual))
		}
	}
	return nil
}

func NodeSliceToString(nodeSlice []Node) string {
	var buffer bytes.Buffer
	fmt.Fprintf(&buffer, "[")
	first := true
	for _, n := range nodeSlice {
		if !first {
			fmt.Fprintf(&buffer, ", ")
		}
		fmt.Fprintf(&buffer, "%s", n.Name())
		first = false
	}
	fmt.Fprintln(&buffer, "]")
	return buffer.String()
}

////////////////////////////////////////////////////
// Tests
////////////////////////////////////////////////////

type WellFoundedGraphTestCase struct {
	// The connection matrix describing the digraph for this case.
	connectionMatrix [][]int

	// If this is non-nil it should be the list of indices of the square nodes.
	squares []int

	// The expected result when searching from Node0
	expectedFirstA, expectedLastA, expectedPathA string

	// The expected result when searching from Node1
	expectedFirstB, expectedLastB, expectedPathB string

	expectedDebugDataA, expectedDebugDataB expectedDebugData
}

// TestWellFoundedIsWellFoundedCirclesOnly tests the function |checkWellFounded|
// on graphs that are well-founded and that contain only circles. For
// graphs that contain only circles being well-founded is equivalent to
// being acyclic.
func TestWellFoundedIsWellFoundedCirclesOnly(t *testing.T) {
	cases := []WellFoundedGraphTestCase{

		// Case: Single node, no self-loop
		{
			connectionMatrix: [][]int{
				{},
			},
		},

		// Case: Linear chain of length 4
		{
			connectionMatrix: [][]int{
				{1}, // 0 -> 1
				{2}, // 1 -> 2
				{3}, // 2 -> 3
				{},  // 3 -> ||
			},
		},

		// Case: Diamond
		//
		//      0
		//    1   2
		//      3
		{
			connectionMatrix: [][]int{
				{1, 2}, // 0
				{3},    // 1
				{3},    // 2
				{},     // 3
			},
		},

		// Case: Complex, double diamond with a tail
		//
		//     0
		//   1   2
		// 6   3    4
		//        5
		{
			connectionMatrix: [][]int{
				{1, 2}, // 0
				{3, 6}, // 1
				{3, 4}, // 2
				{5},    // 3
				{5},    // 4
				{},     // 5
				{},     // 6
			},
		},

		// Case: 0 and 1 are independent roots
		//
		//     0    1
		//   7   2  |
		// 6   3    4
		//        5
		{
			connectionMatrix: [][]int{
				{7, 2}, // 0
				{4},    // 1
				{3, 4}, // 2
				{5},    // 3
				{5},    // 4
				{},     // 5
				{},     // 6
				{3, 6}, // 7
			},
		},
	}

	for i, c := range cases {
		graph := buildCircleTestGraph(c.connectionMatrix)
		dbgData := debugData{}
		cycleDescription := checkWellFounded(graph[0], &dbgData)
		if cycleDescription != nil {
			t.Fatalf("Case %dA: Detected a cycle: %s", i, cycleDescription)
		}
		if err := compareDebugData(&c.expectedDebugDataA, &dbgData); err != nil {
			t.Fatalf("Case %dA: %s", i, err.Error())
		}
		if len(graph) > 1 {
			dbgData := debugData{}
			cycleDescription = checkWellFounded(graph[1], &dbgData)
			if cycleDescription != nil {
				t.Fatalf("Case %dB: Detected a cycle: %s", i, cycleDescription)
			}
			if err := compareDebugData(&c.expectedDebugDataB, &dbgData); err != nil {
				t.Fatalf("Case %dB: %s", i, err.Error())
			}
		}
	}
}

// TestWellFoundedNotWellFoundedCirclesOnly tests the function |checkWellFounded|
// on graphs that are not well-founded and that contain only circles. For
// graphs that contain only circles being well-founded is equivalent to
// being acyclic.
func TestWellFoundedNotWellFoundedCirclesOnly(t *testing.T) {

	cases := []WellFoundedGraphTestCase{

		// Case: Single node with self-loop
		{
			connectionMatrix: [][]int{
				{0},
			},

			expectedFirstA: "Node0",
			expectedLastA:  "Node0",
			expectedPathA:  "Node0",
			expectedDebugDataA: expectedDebugData{
				initialPendingSet: []string{"Node0"},
			},
		},

		// Case: Loop of length 4
		{
			connectionMatrix: [][]int{
				{1}, // 0 -> 1
				{2}, // 1 -> 2
				{3}, // 2 -> 3
				{0}, // 3 -> 0
			},

			expectedFirstA: "Node0",
			expectedLastA:  "Node3",
			expectedPathA:  "Node0, Node1, Node2, Node3",
			expectedDebugDataA: expectedDebugData{
				initialPendingSet: []string{"Node0", "Node1", "Node2", "Node3"},
			},

			expectedFirstB: "Node1",
			expectedLastB:  "Node0",
			expectedPathB:  "Node1, Node2, Node3, Node0",
			expectedDebugDataB: expectedDebugData{
				initialPendingSet: []string{"Node0", "Node1", "Node2", "Node3"},
			},
		},

		// Case: Diamond with a loop-back from bottom to top
		//
		//      0  <--
		//    1   2  |
		//      3 ---|
		{
			connectionMatrix: [][]int{
				{1, 2}, // 0
				{3},    // 1
				{3},    // 2
				{0},    // 3
			},
			expectedFirstA: "Node0",
			expectedLastA:  "Node3",
			expectedPathA:  "Node0, Node1, Node3",
			expectedDebugDataA: expectedDebugData{
				initialPendingSet: []string{"Node0", "Node1", "Node2", "Node3"},
			},

			expectedFirstB: "Node1",
			expectedLastB:  "Node0",
			expectedPathB:  "Node1, Node3, Node0",
			expectedDebugDataB: expectedDebugData{
				initialPendingSet: []string{"Node0", "Node1", "Node2", "Node3"},
			},
		},

		// Case: Complex, double diamond with a tail and a bridge and
		// a loop back to the top
		//
		//           0
		//       1       2   <----
		//    6      3      4    |
		//               5 ------|
		{
			connectionMatrix: [][]int{
				{1, 2}, // 0
				{3, 6}, // 1
				{3, 4}, // 2
				{5},    // 3
				{5},    // 4
				{2},    // 5
				{},     // 6
			},
			expectedFirstA: "Node3",
			expectedLastA:  "Node2",
			expectedPathA:  "Node3, Node5, Node2",
			expectedDebugDataA: expectedDebugData{
				initialPendingSet: []string{"Node0", "Node1", "Node2", "Node3", "Node4", "Node5"},
			},

			expectedFirstB: "Node3",
			expectedLastB:  "Node2",
			expectedPathB:  "Node3, Node5, Node2",
			expectedDebugDataB: expectedDebugData{
				initialPendingSet: []string{"Node1", "Node2", "Node3", "Node4", "Node5"},
			},
		},

		// Case: More complex
		//
		//
		//             0
		//        1    2    3  <------
		//           4    5          |
		//              6   8  7     |
		//             11   9 -------|
		//             12   10
		{
			connectionMatrix: [][]int{
				{1, 2, 3}, // 0
				{4},       // 1
				{4, 5},    // 2
				{5},       // 3
				{6},       // 4
				{6, 7, 8}, // 5
				{11},      // 6
				{},        // 7
				{9},       // 8
				{10, 3},   // 9
				{},        // 10
				{12},      // 11
				{},        // 12
			},
			expectedFirstA: "Node5",
			expectedLastA:  "Node3",
			expectedPathA:  "Node5, Node8, Node9, Node3",
			expectedDebugDataA: expectedDebugData{
				initialPendingSet: []string{"Node0", "Node2", "Node3", "Node5", "Node8", "Node9"},
			},
		},

		// Case: No loop from 0 but loop from 1
		//
		//
		//             1
		//        0    2    3  <----
		//          4    5        |
		//            6  8   7    |
		//          11     9 ------|
		//          12     10
		{
			connectionMatrix: [][]int{
				{4},       // 0
				{0, 2, 3}, // 1
				{4, 5},    // 2
				{5},       // 3
				{6},       // 4
				{6, 7, 8}, // 5
				{11},      // 6
				{},        // 7
				{9},       // 8
				{10, 3},   // 9
				{},        // 10
				{12},      // 11
				{},        // 12
			},
			expectedFirstB: "Node5",
			expectedLastB:  "Node3",
			expectedPathB:  "Node5, Node8, Node9, Node3",
			expectedDebugDataB: expectedDebugData{
				initialPendingSet: []string{"Node1", "Node2", "Node3", "Node5", "Node8", "Node9"},
			},
		},
	}

	for i, c := range cases {
		graph := buildCircleTestGraph(c.connectionMatrix)
		// Test from root=Node0
		dbgData := debugData{}
		cycleDescription := checkWellFounded(graph[0], &dbgData)
		if err := compareDebugData(&c.expectedDebugDataA, &dbgData); err != nil {
			t.Fatalf("Case %dA: %s", i, err.Error())
		}
		if len(c.expectedPathA) == 0 {
			if cycleDescription != nil {
				t.Fatalf("Case %dA: Detected a cycle: %s", i, cycleDescription)
			}
		} else {
			if cycleDescription == nil {
				t.Fatalf("Case %d: Expected a cycle.", i)
			}
			if !strings.Contains(cycleDescription.String(), fmt.Sprintf("first:%s", c.expectedFirstA)) {
				t.Fatalf("Case %d: got=%s expectedFirstA=%q", i, cycleDescription.String(), c.expectedFirstA)
			}
			if !strings.Contains(cycleDescription.String(), fmt.Sprintf("last:%s", c.expectedLastA)) {
				t.Fatalf("Case %d: got=%s expectedLastA=%q", i, cycleDescription.String(), c.expectedLastA)
			}
			if !strings.Contains(cycleDescription.String(), fmt.Sprintf("{%s}", c.expectedPathA)) {
				t.Fatalf("Case %d: got=%s expectedPathA=%q", i, cycleDescription.String(), c.expectedPathA)
			}
		}

		// Test from root=Node1
		if len(graph) > 1 {
			dbgData := debugData{}
			cycleDescription := checkWellFounded(graph[1], &dbgData)
			if err := compareDebugData(&c.expectedDebugDataB, &dbgData); err != nil {
				t.Fatalf("Case %dB: %s", i, err.Error())
			}
			if len(c.expectedPathB) == 0 {
				if cycleDescription != nil {
					t.Fatalf("Case %dB: Detected a cycle: %s", i, cycleDescription)
				}
			} else {
				if cycleDescription == nil {
					t.Fatalf("Case %dB: Expected a cycle.", i)
				}
				if !strings.Contains(cycleDescription.String(), fmt.Sprintf("first:%s", c.expectedFirstB)) {
					t.Fatalf("Case %dB: got=%s expectedFirstB=%q", i, cycleDescription.String(), c.expectedFirstB)
				}
				if !strings.Contains(cycleDescription.String(), fmt.Sprintf("last:%s", c.expectedLastB)) {
					t.Fatalf("Case %dB: got=%s expectedLastB=%q", i, cycleDescription.String(), c.expectedLastB)
				}
				if !strings.Contains(cycleDescription.String(), fmt.Sprintf("{%s}", c.expectedPathB)) {
					t.Fatalf("Case %dB: got=%s expectedPathB=%q", i, cycleDescription.String(), c.expectedPathB)
				}
			}
		}
	}
}

// TestWellFoundedIsWellFounded tests the function |checkWellFounded| on graphs
// that contain both circles and squares and are well-founded.
func TestWellFoundedIsWellFounded(t *testing.T) {
	cases := []WellFoundedGraphTestCase{

		// Case: Single square node, no self-loop
		{
			connectionMatrix: [][]int{
				{},
			},
			squares: []int{0},
		},

		// Case: Loop through right branch but not through left branch.
		//
		//      [0] <--|
		//    1     2--|
		{
			connectionMatrix: [][]int{
				{1, 2}, // 0
				{},     // 1
				{0},    // 2
			},
			squares: []int{0},
		},

		// Case: Loop through left branch but not through rigt branch.
		//
		//      [0] <--|
		//    2     1--|
		{
			connectionMatrix: [][]int{
				{1, 2}, // 0
				{0},    // 1
				{},     // 2
			},
			squares: []int{0},
			expectedDebugDataA: expectedDebugData{
				initialPendingSet:    []string{"Node1"},
				initialFoundationSet: []string{"Node0"},
			},
		},

		// Case: Loop through right branch below the root
		//
		//       0 <---|
		//      [1]    |
		//    2     3--|
		{
			connectionMatrix: [][]int{
				{1},    // 0
				{2, 3}, // 1
				{},     // 2
				{0},    // 3
			},
			squares: []int{1},
			expectedDebugDataA: expectedDebugData{
				initialPendingSet:    []string{"Node3"},
				initialFoundationSet: []string{"Node0"},
			},
		},

		// Case: Loop through left branch below the root
		//
		//       0 <---|
		//      [1]    |
		//    3     2--|
		{
			connectionMatrix: [][]int{
				{1},    // 0
				{2, 3}, // 1
				{0},    // 2
				{},     // 3
			},
			squares: []int{1},
			expectedDebugDataA: expectedDebugData{
				initialPendingSet:    []string{"Node2"},
				initialFoundationSet: []string{"Node0"},
			},
		},

		// Case: We know 0 is well-founded before we see the loop from 4 to 0
		//
		//
		//   1  <-- [0] <--|
		//          [2]    |
		//        3     4--|
		{
			connectionMatrix: [][]int{
				{1, 2}, // 0
				{},     // 1
				{3, 4}, // 2
				{},     // 3
				{0},    // 4
			},
			squares: []int{0, 2},
		},

		// Case: Well-founded becuase of 8
		//         0
		//         1
		//         2 <--
		//         3   |
		//  8 <-- [4]  |
		//         5   |
		//         6---|
		//         7
		{
			connectionMatrix: [][]int{
				{1},    // 0
				{2},    // 1
				{3},    // 2
				{4},    // 3
				{5, 8}, // 4
				{6},    // 5
				{2, 7}, // 6
				{},     // 7
				{},     // 8
			},
			squares: []int{4},
			expectedDebugDataA: expectedDebugData{
				initialPendingSet:    []string{"Node5", "Node6"},
				initialFoundationSet: []string{"Node2"},
			},
		},

		// Case: Double loops through right branches
		//
		//       0 <---|
		//      [1]<---|-----
		//    2     3--|    |
		//         [4]      |
		//      5   6   7 --|
		//      8   9   10
		{
			connectionMatrix: [][]int{
				{1},       // 0
				{2, 3},    // 1
				{},        // 2
				{0, 4},    // 3
				{5, 6, 7}, // 4
				{8},       // 5
				{9},       // 6
				{1, 10},   // 7
				{},        // 8
				{},        // 9
				{},        // 10
			},
			squares: []int{1, 4},
			expectedDebugDataA: expectedDebugData{
				initialPendingSet:    []string{"Node3"},
				initialFoundationSet: []string{"Node0"},
			},
		},

		// Case: Double loops through left branches
		//
		//       0 <---|
		//      [1]<---|-----
		//    3     2--|    |
		//         [4]      |
		//      7   6   5 --|
		//      8   9   10
		{
			connectionMatrix: [][]int{
				{1},       // 0
				{2, 3},    // 1
				{0, 4},    // 2
				{},        // 3
				{5, 6, 7}, // 4
				{1, 10},   // 5
				{9},       // 6
				{8},       // 7
				{},        // 8
				{},        // 9
				{},        // 10
			},
			squares: []int{1, 4},
			expectedDebugDataA: expectedDebugData{
				initialPendingSet:    []string{"Node2", "Node5"},
				initialFoundationSet: []string{"Node0", "Node1"},
			},
		},

		// Case: Crossing up and down on right
		//
		//         0 -----|
		//         1      |
		//        [2] <---|-----
		//      3     4 <-|    |
		//            5        |
		//            6        |
		//            7 -------|
		//
		{
			connectionMatrix: [][]int{
				{1, 4}, // 0
				{2},    // 1
				{3, 4}, // 2
				{},     // 3
				{5},    // 4
				{6},    // 5
				{7},    // 6
				{2},    // 7
			},
			squares: []int{2},
		},

		// Case: Crossing up and down on left
		//
		//         0 -----|
		//         1      |
		//        [2] <---|-----
		//      4     3 <-|    |
		//            5        |
		//            6        |
		//            7 -------|
		//
		{
			connectionMatrix: [][]int{
				{1, 3}, // 0
				{2},    // 1
				{3, 4}, // 2
				{5},    // 3
				{},     // 4
				{6},    // 5
				{7},    // 6
				{2},    // 7
			},
			squares: []int{2},
			expectedDebugDataA: expectedDebugData{
				initialPendingSet:    []string{"Node0", "Node3", "Node5", "Node6", "Node7"},
				initialFoundationSet: []string{"Node2"},
			},
		},

		// Case:
		//
		//            [0]  <------|
		//   |---->1        2     |
		//   |<---[3]-------------|
		//
		//
		//
		{
			connectionMatrix: [][]int{
				{1, 2}, // 0
				{3},    // 1
				{},     // 2
				{0, 1}, // 3
			},
			squares: []int{0, 3},
			expectedDebugDataA: expectedDebugData{
				initialPendingSet:    []string{"Node1", "Node3"},
				initialFoundationSet: []string{"Node0"},
			},
		},

		// Case:
		//
		//  |-----> 0  <------\
		//  |   [1] --> [2]---|--> 4
		//  |    ^      \
		//  |----|-------3
		//
		//
		{
			connectionMatrix: [][]int{
				{1, 2},    // 0
				{2},       // 1
				{0, 3, 4}, // 2
				{0, 1},    // 3
				{},        // 4
			},
			squares: []int{1, 2},
			expectedDebugDataA: expectedDebugData{
				initialPendingSet:    []string{"Node3"},
				initialFoundationSet: []string{"Node0", "Node1"},
			},
		},

		// Case:
		//
		//     |-->[1]
		//    [2]< -|    0
		//     ^    |
		//     |----|
		//
		//
		{
			connectionMatrix: [][]int{
				{},     // 0
				{0, 2}, // 1
				{1, 2}, // 2
			},
			squares: []int{1, 2},
		},

		// Case: Double loops through left branches
		//
		//       0 <---|
		//      [1]<---|-----
		//    3     2--|    |
		//         [4]      |
		//      7   6   5 --|
		//      8   9   10
		{
			connectionMatrix: [][]int{
				{1},       // 0
				{2, 3},    // 1
				{0, 4},    // 2
				{},        // 3
				{5, 6, 7}, // 4
				{1, 10},   // 5
				{9},       // 6
				{8},       // 7
				{},        // 8
				{},        // 9
				{},        // 10
			},
			squares: []int{1, 4},
			expectedDebugDataA: expectedDebugData{
				initialPendingSet:    []string{"Node2", "Node5"},
				initialFoundationSet: []string{"Node0", "Node1"},
			},
		},
	}

	for i, c := range cases {
		graph := buildTestGraph(c.connectionMatrix, c.squares)
		dbgData := debugData{}
		cycleDescription := checkWellFounded(graph[0], &dbgData)
		if err := compareDebugData(&c.expectedDebugDataA, &dbgData); err != nil {
			t.Fatalf("Case %dA: %s", i, err.Error())
		}
		if cycleDescription != nil {
			t.Fatalf("Case %dA: Detected a cycle: %s", i, cycleDescription)
		}
		if len(graph) > 1 {
			dbgData := debugData{}
			cycleDescription := checkWellFounded(graph[1], &dbgData)
			if err := compareDebugData(&c.expectedDebugDataB, &dbgData); err != nil {
				t.Fatalf("Case %dB: %s", i, err.Error())
			}
			if cycleDescription != nil {
				t.Fatalf("Case %dB: Detected a cycle: %s", i, cycleDescription)
			}
		}
	}
}

// TestWellFoundedNotWellFounded tests the function |checkWellFounded| on graphs
// that contain both circles and squares and are not well-founded.
func TestWellFoundedNotWellFounded(t *testing.T) {

	cases := []WellFoundedGraphTestCase{

		// Case: Single square node with self-loop
		{
			connectionMatrix: [][]int{
				{0},
			},

			squares: []int{0},

			expectedFirstA: "Node0",
			expectedLastA:  "Node0",
			expectedPathA:  "Node0",
			expectedDebugDataA: expectedDebugData{
				initialPendingSet:    []string{"Node0"},
				initialFoundationSet: []string{},
			},
		},

		// Case: Loop of length 4 with 1 square at the root
		{
			connectionMatrix: [][]int{
				{1}, // 0 -> 1
				{2}, // 1 -> 2
				{3}, // 2 -> 3
				{0}, // 3 -> 0
			},

			squares: []int{0},

			expectedFirstA: "Node0",
			expectedLastA:  "Node3",
			expectedPathA:  "Node0, Node1, Node2, Node3",
			expectedDebugDataA: expectedDebugData{
				initialPendingSet:    []string{"Node0", "Node1", "Node2", "Node3"},
				initialFoundationSet: []string{},
			},

			expectedFirstB: "Node1",
			expectedLastB:  "Node0",
			expectedPathB:  "Node1, Node2, Node3, Node0",
			expectedDebugDataB: expectedDebugData{
				initialPendingSet:    []string{"Node0", "Node1", "Node2", "Node3"},
				initialFoundationSet: []string{},
			},
		},

		// Case: Loop of length 4 with 2 squares
		{
			connectionMatrix: [][]int{
				{1}, // 0 -> 1
				{2}, // 1 -> 2
				{3}, // 2 -> 3
				{0}, // 3 -> 0
			},

			squares: []int{1, 2},

			expectedFirstA: "Node0",
			expectedLastA:  "Node3",
			expectedPathA:  "Node0, Node1, Node2, Node3",
			expectedDebugDataA: expectedDebugData{
				initialPendingSet:    []string{"Node0", "Node1", "Node2", "Node3"},
				initialFoundationSet: []string{},
			},

			expectedFirstB: "Node1",
			expectedLastB:  "Node0",
			expectedPathB:  "Node1, Node2, Node3, Node0",
			expectedDebugDataB: expectedDebugData{
				initialPendingSet:    []string{"Node0", "Node1", "Node2", "Node3"},
				initialFoundationSet: []string{},
			},
		},

		// Case: Loop through left and right branches
		//
		//     |--> [0] <--|
		//     |--1     2--|
		{
			connectionMatrix: [][]int{
				{1, 2}, // 0
				{0},    // 1
				{0},    // 2
			},
			squares: []int{0},

			expectedFirstA: "Node0",
			expectedLastA:  "Node1",
			expectedPathA:  "Node0, Node1",
			expectedDebugDataA: expectedDebugData{
				initialPendingSet:    []string{"Node0", "Node1", "Node2"},
				initialFoundationSet: []string{},
			},

			expectedFirstB: "Node1",
			expectedLastB:  "Node0",
			expectedPathB:  "Node1, Node0",
			expectedDebugDataB: expectedDebugData{
				initialPendingSet:    []string{"Node0", "Node1", "Node2"},
				initialFoundationSet: []string{},
			},
		},

		// Case: Loop through left and right branches below the root
		//
		//  |----> 0 <---|
		//  |     [1]    |
		//  |-- 2     3--|
		{
			connectionMatrix: [][]int{
				{1},    // 0
				{2, 3}, // 1
				{0},    // 2
				{0},    // 3
			},
			squares: []int{1},

			expectedFirstA: "Node0",
			expectedLastA:  "Node2",
			expectedPathA:  "Node0, Node1, Node2",
			expectedDebugDataA: expectedDebugData{
				initialPendingSet:    []string{"Node0", "Node1", "Node2", "Node3"},
				initialFoundationSet: []string{},
			},

			expectedFirstB: "Node1",
			expectedLastB:  "Node0",
			expectedPathB:  "Node1, Node2, Node0",
			expectedDebugDataB: expectedDebugData{
				initialPendingSet:    []string{"Node0", "Node1", "Node2", "Node3"},
				initialFoundationSet: []string{},
			},
		},

		// Case: Self loop at one node below a square
		//
		//      [0]
		//    1     2--|
		//          ^  |
		//          |  |
		//          ----
		{
			connectionMatrix: [][]int{
				{1, 2}, // 0
				{},     // 1
				{2},    // 2
			},
			squares: []int{0},

			expectedFirstA: "Node2",
			expectedLastA:  "Node2",
			expectedPathA:  "Node2",
			expectedDebugDataA: expectedDebugData{
				initialPendingSet:    []string{"Node2"},
				initialFoundationSet: []string{},
			},
		},

		// Case: Self loop at one node below a square, other side
		//
		//      [0]
		//    2     1--|
		//          ^  |
		//          |  |
		//          ----
		{
			connectionMatrix: [][]int{
				{1, 2}, // 0
				{1},    // 1
				{},     // 2
			},
			squares: []int{0},

			expectedFirstA: "Node1",
			expectedLastA:  "Node1",
			expectedPathA:  "Node1",
			expectedDebugDataA: expectedDebugData{
				initialPendingSet:    []string{"Node1"},
				initialFoundationSet: []string{},
			},

			expectedFirstB: "Node1",
			expectedLastB:  "Node1",
			expectedPathB:  "Node1",
			expectedDebugDataB: expectedDebugData{
				initialPendingSet:    []string{"Node1"},
				initialFoundationSet: []string{},
			},
		},

		// Case: Self loop off of one branch of a square.
		//
		//             [0]
		//          1       2
		//          3 -|
		//          ^  |
		//          |---
		{
			connectionMatrix: [][]int{
				{1, 2}, // 0
				{3},    // 1
				{},     // 2
				{3},    // 3
			},
			squares: []int{0},

			expectedFirstA: "Node3",
			expectedLastA:  "Node3",
			expectedPathA:  "Node3",
			expectedDebugDataA: expectedDebugData{
				initialPendingSet:    []string{"Node1", "Node3"},
				initialFoundationSet: []string{},
			},

			expectedFirstB: "Node3",
			expectedLastB:  "Node3",
			expectedPathB:  "Node3",
			expectedDebugDataB: expectedDebugData{
				initialPendingSet:    []string{"Node1", "Node3"},
				initialFoundationSet: []string{},
			},
		},

		// Case: Hard loop off of one branch of a square.
		//
		//             [0]
		//      --> 1       2
		//      |   3
		//      |---4
		{
			connectionMatrix: [][]int{
				{1, 2}, // 0
				{3},    // 1
				{},     // 2
				{4},    // 3
				{1},    // 4
			},
			squares: []int{0},

			expectedFirstA: "Node1",
			expectedLastA:  "Node4",
			expectedPathA:  "Node1, Node3, Node4",
			expectedDebugDataA: expectedDebugData{
				initialPendingSet:    []string{"Node1", "Node3", "Node4"},
				initialFoundationSet: []string{},
			},

			expectedFirstB: "Node1",
			expectedLastB:  "Node4",
			expectedPathB:  "Node1, Node3, Node4",
			expectedDebugDataB: expectedDebugData{
				initialPendingSet:    []string{"Node1", "Node3", "Node4"},
				initialFoundationSet: []string{},
			},
		},

		// Case: Graph below 0 is well-founded and 1 is well-founded but not the graph below 1.
		//
		//         [1]
		//     2 -|    0
		//     ^  |
		//     |--|
		//
		//
		{
			connectionMatrix: [][]int{
				{},     // 0
				{0, 2}, // 1
				{2},    // 2
			},
			squares: []int{1},

			expectedFirstB: "Node2",
			expectedLastB:  "Node2",
			expectedPathB:  "Node2",
			expectedDebugDataB: expectedDebugData{
				initialPendingSet:    []string{"Node2"},
				initialFoundationSet: []string{},
			},
		},

		// Case: Different loops through both branches sharing nodes.
		//
		//        0 <------|
		//        1        |
		//       [2]       |
		//    3        4   |
		//     \       5   |
		//      \----> 6   |
		//             7 --|
		{
			connectionMatrix: [][]int{
				{1},    // 0
				{2},    // 1
				{3, 4}, // 2
				{6},    // 3
				{5},    // 4
				{6},    // 5
				{7},    // 6
				{0},    // 7
			},
			squares: []int{2},

			expectedFirstA: "Node0",
			expectedLastA:  "Node7",
			expectedPathA:  "Node0, Node1, Node2, Node3, Node6, Node7",
			expectedDebugDataA: expectedDebugData{
				initialPendingSet:    []string{"Node0", "Node1", "Node2", "Node3", "Node4", "Node5", "Node6", "Node7"},
				initialFoundationSet: []string{},
			},

			expectedFirstB: "Node1",
			expectedLastB:  "Node0",
			expectedPathB:  "Node1, Node2, Node3, Node6, Node7, Node0",
			expectedDebugDataB: expectedDebugData{
				initialPendingSet:    []string{"Node0", "Node1", "Node2", "Node3", "Node4", "Node5", "Node6", "Node7"},
				initialFoundationSet: []string{},
			},
		},

		// Case: Different loops through both branches sharing nodes--switch sides.
		//
		//        0 <------|
		//        1        |
		//       [2]       |
		//    4        3   |
		//     \       5   |
		//      \----> 6   |
		//             7 --|
		{
			connectionMatrix: [][]int{
				{1},    // 0
				{2},    // 1
				{3, 4}, // 2
				{5},    // 3
				{6},    // 4
				{6},    // 5
				{7},    // 6
				{0},    // 7
			},
			squares: []int{2},

			expectedFirstA: "Node0",
			expectedLastA:  "Node7",
			expectedPathA:  "Node0, Node1, Node2, Node3, Node5, Node6, Node7",
			expectedDebugDataA: expectedDebugData{
				initialPendingSet:    []string{"Node0", "Node1", "Node2", "Node3", "Node4", "Node5", "Node6", "Node7"},
				initialFoundationSet: []string{},
			},

			expectedFirstB: "Node1",
			expectedLastB:  "Node0",
			expectedPathB:  "Node1, Node2, Node3, Node5, Node6, Node7, Node0",
			expectedDebugDataB: expectedDebugData{
				initialPendingSet:    []string{"Node0", "Node1", "Node2", "Node3", "Node4", "Node5", "Node6", "Node7"},
				initialFoundationSet: []string{},
			},
		},

		// Case: Not Well-founded becuase of 8 becuase 4 is a circle
		//         0
		//         1
		//         2 <--
		//         3   |
		//  8 <--  4   |
		//         5   |
		//         6---|
		//         7
		{
			connectionMatrix: [][]int{
				{1},    // 0
				{2},    // 1
				{3},    // 2
				{4},    // 3
				{5, 8}, // 4
				{6},    // 5
				{2, 7}, // 6
				{},     // 7
				{},     // 8
			},
			squares: []int{},

			expectedFirstA: "Node2",
			expectedLastA:  "Node6",
			expectedPathA:  "Node2, Node3, Node4, Node5, Node6",
			expectedDebugDataA: expectedDebugData{
				initialPendingSet:    []string{"Node0", "Node1", "Node2", "Node3", "Node4", "Node5", "Node6"},
				initialFoundationSet: []string{},
			},

			expectedFirstB: "Node2",
			expectedLastB:  "Node6",
			expectedPathB:  "Node2, Node3, Node4, Node5, Node6",
			expectedDebugDataB: expectedDebugData{
				initialPendingSet:    []string{"Node1", "Node2", "Node3", "Node4", "Node5", "Node6"},
				initialFoundationSet: []string{},
			},
		},

		// Case: Not Well-founded becuase of 8 becuase there is no 8
		//         0
		//         1
		//         2 <--
		//         3   |
		//       [4]   |
		//         5   |
		//         6---|
		//         7
		{
			connectionMatrix: [][]int{
				{1},    // 0
				{2},    // 1
				{3},    // 2
				{4},    // 3
				{5},    // 4
				{6},    // 5
				{2, 7}, // 6
				{},     // 7
			},
			squares: []int{4},

			expectedFirstA: "Node2",
			expectedLastA:  "Node6",
			expectedPathA:  "Node2, Node3, Node4, Node5, Node6",
			expectedDebugDataA: expectedDebugData{
				initialPendingSet:    []string{"Node0", "Node1", "Node2", "Node3", "Node4", "Node5", "Node6"},
				initialFoundationSet: []string{},
			},

			expectedFirstB: "Node2",
			expectedLastB:  "Node6",
			expectedPathB:  "Node2, Node3, Node4, Node5, Node6",
			expectedDebugDataB: expectedDebugData{
				initialPendingSet:    []string{"Node1", "Node2", "Node3", "Node4", "Node5", "Node6"},
				initialFoundationSet: []string{},
			},
		},
	}

	for i, c := range cases {
		graph := buildTestGraph(c.connectionMatrix, c.squares)
		// Test from root=Node0
		dbgData := debugData{}
		cycleDescription := checkWellFounded(graph[0], &dbgData)
		if err := compareDebugData(&c.expectedDebugDataA, &dbgData); err != nil {
			t.Fatalf("Case %dA: %s", i, err.Error())
		}
		if len(c.expectedPathA) == 0 {
			if cycleDescription != nil {
				t.Fatalf("Case %dA: Detected a cycle: %s", i, cycleDescription)
			}
		} else {
			if cycleDescription == nil {
				t.Fatalf("Case %d: Expected a cycle.", i)
			}
			if !strings.Contains(cycleDescription.String(), fmt.Sprintf("first:%s", c.expectedFirstA)) {
				t.Fatalf("Case %d: got=%s expectedFirst=%q", i, cycleDescription.String(), c.expectedFirstA)
			}
			if !strings.Contains(cycleDescription.String(), fmt.Sprintf("last:%s", c.expectedLastA)) {
				t.Fatalf("Case %d: got=%s expectedLast=%q", i, cycleDescription.String(), c.expectedLastA)
			}
			if !strings.Contains(cycleDescription.String(), fmt.Sprintf("{%s}", c.expectedPathA)) {
				t.Fatalf("Case %d: got=%s expectedPath=%q", i, cycleDescription.String(), c.expectedPathA)
			}
		}

		// Test from root=Node1
		if len(graph) > 1 {
			dbgData := debugData{}
			cycleDescription := checkWellFounded(graph[1], &dbgData)
			if err := compareDebugData(&c.expectedDebugDataB, &dbgData); err != nil {
				t.Fatalf("Case %dB: %s", i, err.Error())
			}
			if len(c.expectedPathB) == 0 {
				if cycleDescription != nil {
					t.Fatalf("Case %dB: Detected a cycle: %s", i, cycleDescription)
				}
			} else {
				if cycleDescription == nil {
					t.Fatalf("Case %dB: Expected a cycle.", i)
				}
				if !strings.Contains(cycleDescription.String(), fmt.Sprintf("first:%s", c.expectedFirstB)) {
					t.Fatalf("Case %dB: got=%s expectedFirst=%q", i, cycleDescription.String(), c.expectedFirstB)
				}
				if !strings.Contains(cycleDescription.String(), fmt.Sprintf("last:%s", c.expectedLastB)) {
					t.Fatalf("Case %dB: got=%s expectedLast=%q", i, cycleDescription.String(), c.expectedLastB)
				}
				if !strings.Contains(cycleDescription.String(), fmt.Sprintf("{%s}", c.expectedPathB)) {
					t.Fatalf("Case %dB: got=%s expectedPath=%q", i, cycleDescription.String(), c.expectedPathB)
				}
			}
		}
	}
}
