// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package utils

import (
	"bytes"
	"fmt"
)

// This file contains an implementation of an algorithm to check the well-foundedness
// of two-sorted directed graphs. See the paper
// "Well-Founded Two-Sorted Directed Graphs" (https://goo.gl/YrLmfY) for background
// and detailed definitions. Here we give only a high-level overview.
//
// A two-sorted graph is a directed graph that contains two sorts of nodes called circle nodes and
// square nodes. A node in a two-sorted graph is *well-founded* iff it satisfies the
// following recursive definition:
// (i) Leaf nodes are well-founded
// (ii) A circle node is well-founded iff all of its children are well-founded
// (iii) A non-leaf square node is well-founded iff at least one of its children is well-founded.
// (See the paper for a more logically correct definition.)
//
// See the comments on the main function |CheckWellFounded| below for a description
// of the algorithm.

///////////////////////////////////////////////////
// Node type
//
// A |Node| is a node in a directed graph. A user of this library is responsible
// for implementing this interface. This library will use the Go equality operator
// to compare instances of |Node| and it is the responsibility of the user
// to ensure that x == y iff x and y represent the same logical node.
///////////////////////////////////////////////////

type Node interface {
	// Returns the list of children of this node.
	OutEdges() []OutEdge

	// Returns whether or not this node is a square node.
	IsSquare() bool

	// SetKnownWellFounded is invoked by the algorithm in order to set the
	// fact that the algorithm has determined that this node is well-founded.
	// An implementation of |Node| should cache this--even between different
	// invocations of the algorithm on different starting nodes--and return
	// |true| from the method |KnownWellFounded| just in case this method has
	// ever been invoked.  In this way any nodes verified to be well-founded during
	// one run of the algorithm do not need to be checked during a later run of the
	// algorithm on a different starting node.
	SetKnownWellFounded()

	// Returns whether or not the function |SetKnownWellFounded| has ever
	// been invoked on this node, during the lifetime of a program using
	// this library.
	KnownWellFounded() bool

	// Returns a human-readable name for this node. Not used by the algorithm
	// but used for generating debugging and error messages.
	Name() string
}

type OutEdge struct {
	// The Label on an edge is opaque data that may be used by the client however it wishes.
	// The algorithm never inspects this data.
	Label interface{}

	// The target node of the out edge.
	Target Node
}

///////////////////////////////////////////////////
// LeafNode type
//
// A |LeafNode| is an implementation of the Node interface
// that represents a leaf node. It is a convenience type
// that may be used by clients of this library in order to
// easily add a leaf node to a graph.
///////////////////////////////////////////////////

type LeafNode struct {
	name string
}

func NewLeafNode(name string) *LeafNode {
	return &LeafNode{name}
}

func (*LeafNode) OutEdges() []OutEdge {
	return nil
}

func (*LeafNode) IsSquare() bool {
	return false
}

func (*LeafNode) SetKnownWellFounded() {
}

func (*LeafNode) KnownWellFounded() bool {
	return true
}

func (ln *LeafNode) Name() string {
	return ln.name
}

///////////////////////////////////////////////////
// CheckWellFounded function
//
// This is the main public function.
///////////////////////////////////////////////////

// CheckWellFounded checks whether the sub-graph rooted at |root| contains any ill-founded nodes.
// If any ill-founded nodes are detected then a non-nil |CycleDescription| is returned containing
// a cycle of ill-founded nodes. If every node is well-founded then nil is returned.
//
// If the graph contains only circle nodes then well-foundedness is equivalent to
// acyclicality and so the returned cycle is a proof that the nodes contained in it
// are ill-founded. But in general (if the graph contains square nodes) the returned
// |CycleDescription| is only meant to serve as an example of some of the
// ill-foundedness contained in the subgraph. The cycle is guaranteed to contain only
// ill-founded nodes and the cycle may be considered part of a proof that thes nodes
// are in fact ill-founded. But the cycle does not necessarily contain every
// ill-founded node and it does not
// necessarily constitute a complete proof of the ill-foundedness
// because a graph that contains square nodes is allowed to contain some cycles and still be
// well-founded. The intended application of the returned CycleDescription is to be used to
// describe to a user the location of the ill-foundedness in order to allow the user to modify
// the graph in order to make it well-founded.
//
// This function may be invoked multiple times on different starting nodes during the life
// of a program. If the function is invoked once on node |x| and no ill-foundedness is
// found and then the function is invoked later on node |y| and if node |z| is reachable
// from both |x| and |y| then node |z| will be marked |KnownWellFounded| during the
// first run of the function and so the graph below |z| will not have to be inspected
// during the second run of the function.
//
// Our algorithm proceeds in three phases:
// (1) Phase 1 consists of a depth-first traversal of the graph whose purpose is two-fold:
//     (a) To prove directly that as many nodes as possible are well-founded and mark them
//         so by invoking SetKnownWellFounded().
//     (b) To prepare for phase 2 by constructing two sets of nodes called the |foundationSet| and
//         the |pendingSet|
//     See the comments at |checkWellFoundedPhase1| for more details.
//
// In many cases phase 1 will be able to prove that every node is well-founded and so the algorithm
// will terminate without entering phase 2. This is the case for example if the graph has no
// cycles at all.
//
// (2) The purpose of phase 2 is to propogate the |KnownWellFounded| property to the remaining well-founded
//     nodes (the ones that could not be verified as well-founded during phase 1.) Phase 2 proceeds in
//     multiple rounds. During each round the |KnownWellFounded| property is propogated from the
//     |foundationSet| to the |pendingSet|. See the comments at |checkWellFoundedPhase2| for more details.
//
//  If there are no ill-founded nodes then the algorithm terminates after phase 2.
//
// (3) Phase 3 of the algorithm consists of building a |CycleDescription| in the case that there are ill-founded
//     nodes.  See the method |findKnownIllFoundedCycle|.

func CheckWellFounded(root Node) *CycleDescription {
	return checkWellFounded(root, nil)
}

// checkWellFounded is a package-private version of CheckWellFounded intendend to be invoked by tests.
// It offers a second parameter |debugDataRequest|. If this is non-nil then its fields will be filled
// in with debugging data about the output of phase 1.
func checkWellFounded(root Node, debugDataRequest *debugData) *CycleDescription {
	if root == nil {
		return nil
	}
	finder := makeCycleFinder()
	finder.debugDataRequest = debugDataRequest
	holder := finder.holderForNode(root)
	return finder.checkWellFounded(holder)
}

///////////////////////////////////////////////////
/// cycleFinder type
///
/// A cycleFinder is an object that holds state during the execution of the algorithm.
///////////////////////////////////////////////////
type cycleFinder struct {

	// Maps each node seen by the computation to its holder. Note that this assumes
	// that the implementation of Node uses values that are comparable and
	// obey identity semantics.
	nodeToHolder map[Node]*nodeHolder

	foundationSet, pendingSet NodeSet

	visitationIndex int

	debugDataRequest *debugData
}

type debugData struct {
	initialPendingSet    []Node
	initialFoundationSet []Node
}

func makeCycleFinder() cycleFinder {
	finder := cycleFinder{}
	finder.nodeToHolder = make(map[Node]*nodeHolder)
	finder.foundationSet = MakeNodeSet()
	finder.pendingSet = MakeNodeSet()
	return finder
}

// checkWellFounded contains the top-level implementation of the algorithm.
func (finder *cycleFinder) checkWellFounded(nodeHolder *nodeHolder) *CycleDescription {
	if nodeHolder.node.KnownWellFounded() {
		// This node is already known to be well-founded because of an earlier
		// execution of the algorithm.
		return nil
	}

	// Perform phase 1.
	finder.checkWellFoundedPhase1(nodeHolder)

	// In tests we pass back some debugging information here.
	if finder.debugDataRequest != nil {
		finder.debugDataRequest.initialPendingSet = finder.pendingSet.ToNodeSlice()
		finder.debugDataRequest.initialFoundationSet = finder.foundationSet.ToNodeSlice()
	}

	// All nodes have been verified as well-founded.
	if finder.pendingSet.Empty() {
		return nil
	}

	// Perform phase 2.
	finder.checkWellFoundedPhase2()

	// All nodes have been verified as well-founded.
	if finder.pendingSet.Empty() {
		return nil
	}

	// If we are here then there is at least one ill-founded node.

	// In order to build a canonical cycle description, find the illfounded
	// node with the least visitation order.
	var minVisitationOrder int
	var minIllfoundedNode Node = nil
	for n, _ := range finder.pendingSet.elements {
		if minIllfoundedNode == nil || n.visitationOrder < minVisitationOrder {
			minVisitationOrder = n.visitationOrder
			minIllfoundedNode = n.node
		}
	}

	// Starting from the ill-founded node with the least visitation order,
	// build a canonical cycle.
	freshCycleFinder := makeCycleFinder()
	holder := freshCycleFinder.holderForNode(minIllfoundedNode)
	return freshCycleFinder.findKnownIllFoundedCycle(holder, nil)
}

// checkWellFoundedPhase1 is a recursive helper function that does a depth-first traversal
// of the graph rooted at |nodeHolder|. The goal of phase 1 is to mark as many
// of the nodes as possible as |KnownWellFounded| and to set up the |pendingSet|,
// the |foundationSet|, and the |parentsToBeNotified| sets so that the remaining
// well-founded nodes will be marked as |KnownWellFounded| in phase 2.
//
// In more detail the following steps are performed for each node x:
// (a1) If it can be verified during the traversal that x is well-founded then
// x will be marked as |KnownWellFounded|. This occurs if x is a leaf, or x
// is a circle and each child node of x is known well-founded before being
// visited as a child of x, or x is a square and at-least one child node of x
// is known well-founded before being visited as a child of x.
// (a2) Otherwise if it cannot be determined during traveral that x is well-founded then
//   (i) x is added to the |pendingSet|.
//   (ii) x is added to the |parentsToBeNotified| set of all of its children.
// (b) In step (a1) if at the time x is found to be well-founded x already has
// some parent node x' in its |parentsToBeNotified| set (meaning that step a2 occurred
// earlier for x' and so x' is in the |pendingSet|) then x is added to the |foundationSet|.
// In phase 2, the fact that x is in the foundation set and x' is in the pending set will be
// used to propogate known-wellfoundedness to x'.
func (finder *cycleFinder) checkWellFoundedPhase1(nodeHolder *nodeHolder) {
	if nodeHolder.node.KnownWellFounded() {
		// This node is known to be well-founded before being visited.
		// This occurs when the node was marked |KnownWellFounded| during a
		// previous run of the algorithm. It follows that all nodes reachable
		// from this node have also been so marked. We therefore don't need
		// to traverse the part of the graph below this node during this run
		// of the algorithm and so we treat this node as a leaf node.
		nodeHolder.state = vsVisitEnded
		return
	}

	// Mark the visit as started.
	nodeHolder.state = vsVisitStarted

	// Next we examine each of the children and recurse into the unvisited ones.
	sawUnverifiedChild := false
	for _, edge := range nodeHolder.node.OutEdges() {
		childHolder := finder.holderForNode(edge.Target)
		if childHolder.state == vsUnvisited {
			// Recursively visit this child.
			finder.checkWellFoundedPhase1(childHolder)
		}

		// After having visited a child we use the results to update the status of this node.
		// We could express the logic here more concisely, but  the logic is easier
		// to understand if we treat circles and squares seperately,
		if nodeHolder.node.IsSquare() {
			if nodeHolder.node.KnownWellFounded() {
				// This square node has already been marked |KnownWellFounded| becuase
				// of an earlier iteration through this loop. There is nothing else to do.
				continue
			}
			if childHolder.node.KnownWellFounded() {
				// We mark a square node as |KnownWellFounded| as soon as we can so
				// that if any of its descendants are also parents, the well-foundedness
				// has a chance to propogate to the descendant in a recursive call.
				nodeHolder.node.SetKnownWellFounded()
			} else {
				// This square node is not yet known to be well-founded and the child node
				// is not yet known to be well-founded. Set up a back link from the child.
				childHolder.parentsToBeNotified.Add(nodeHolder)
				sawUnverifiedChild = true
			}
			continue // Done handling the square case.
		}

		// Else the node is a circle. If the child is not yet known to be well-founded
		// set up a back link from the child to this node.
		if !childHolder.node.KnownWellFounded() {
			childHolder.parentsToBeNotified.Add(nodeHolder)
			sawUnverifiedChild = true
		}
	}

	// If a circle node has only well-founded children, or a square node has no children at all,
	// then the node is well-founded.
	if !sawUnverifiedChild && !nodeHolder.node.KnownWellFounded() {
		nodeHolder.node.SetKnownWellFounded()
	}

	// Possibly add this node to the |foundationSet| or the |pendingSet|.
	if nodeHolder.node.KnownWellFounded() {
		if !nodeHolder.parentsToBeNotified.Empty() {
			finder.foundationSet.Add(nodeHolder)
		}
	} else {
		finder.pendingSet.Add(nodeHolder)
	}

	// Mark the visit as ended.
	nodeHolder.state = vsVisitEnded
	return
}

// checkWellFoundedPhase2 performs phase 2 of the algorithm. The goal is to
// propogate known well-foundedness along the back-links that were established
// during phase 1. We have two sets of nodes: the |foundationSet| and the
// |pendingSet|. The |pendingSet| consists of all nodes that are not currently
// known to be well-founded. If the |pendingSet| is not empty when this method
// returns, then the nodes in the |pendingSet| are ill-founded. The |foundationSet|
// consists of the current frontier of the propogation. That is, the |foundationSet|
// consists of the nodes discovered to be well-founded in recent iterations and not yet
// used to propogate well-foundedness. (The |foundationSet| starts with the nodes discovered
// to be well-founded during phase 1, pruned to nodes that have parents that
// are in the |pendingSet|.) We iteratively remove a node n from the foundation set and
// for each of its parents p for which p is in the pending set and for which we can now verify
// well-foundedness, we remove p from the pending set and add it to the foundation set.
func (finder *cycleFinder) checkWellFoundedPhase2() {
	for n := finder.foundationSet.removeRandomElement(); n != nil; n = finder.foundationSet.removeRandomElement() {
		for p, _ := range n.parentsToBeNotified.elements {
			if finder.pendingSet.Contains(p) {
				knownWellFounded := true
				if !p.node.IsSquare() {
					for _, edge := range p.node.OutEdges() {
						child := edge.Target
						if child != p.node && !child.KnownWellFounded() {
							knownWellFounded = false
							break
						}
					}
				}
				if knownWellFounded {
					p.node.SetKnownWellFounded()
					finder.foundationSet.Add(p)
					finder.pendingSet.Remove(p)
				}
			}
		}
	}
}

// A pathElement is a node and one of its out-edges.
type pathElement struct {
	node Node
	edge OutEdge
}

type nodePath []pathElement

// findKnownIllFoundedCycle finds and returns a |CycleDescription| starting from a node that is known
// to be ill-founded. This proceeds by following edges from an ill-founded node to
// an ill-founded child node until a cycle is formed. We return a *canonical* cycle,
// meaning we start from the node with the least possible visit index and follow edges to
// the child node with the least possible visit index. This is done in order to make testing of the algorithm easier.
// We are not concerned with optimizing the performance of phase 3 because in the intended application
// phase 3 can occur at most once in the lifetime of a program: Once an ill-founded node is detected the
// program exits with a cycle description allowing the user to fix the ill-foundedness.
//
// This method is recursive. To initiate the recursion |nodeHolder| should be set to the starting node and
// |path| should be nil. In subsequent recursive calls |nodeHolder| is the current node of the walk and
// |path| is the path from the starting node to the current node.
func (finder *cycleFinder) findKnownIllFoundedCycle(nodeHolder *nodeHolder, path nodePath) *CycleDescription {
	// If this is the start of the recursion initialize the path.
	if path == nil {
		path = make(nodePath, 0)
	}

	// Mark the current node as started
	nodeHolder.state = vsVisitStarted
	for _, edge := range nodeHolder.node.OutEdges() {
		childHolder := finder.holderForNode(edge.Target)
		if childHolder.state == vsVisitStarted {
			// If the child has been started but not finished then we have found a cycle
			// from the child to the current node back to the child.
			path = append(path, pathElement{nodeHolder.node, edge})
			return newCycleDescription(path, childHolder.node, nodeHolder.node)
		} else if !childHolder.node.KnownWellFounded() {
			path = append(path, pathElement{nodeHolder.node, edge})
			return finder.findKnownIllFoundedCycle(childHolder, path)
		}
	}
	panic("Program logic error: Could not find a known ill-founded cycle.")
}

// Returns the nodeHolder for the given node.
func (finder *cycleFinder) holderForNode(node Node) *nodeHolder {
	if holder, found := finder.nodeToHolder[node]; found {
		return holder
	}

	// This is the first time we have seen this node. Assign it a new
	// visitor order.
	holder := newNodeHolder(node, finder.visitationIndex)
	finder.visitationIndex++
	finder.nodeToHolder[node] = holder
	return holder
}

////////////////////////////////////////////////////
// nodeHolder type
////////////////////////////////////////////////////

type visitationState int

const (
	vsUnvisited visitationState = iota
	vsVisitStarted
	vsVisitEnded
)

// A nodeHolder is an internal data structure used by the algorithm.
// It holds one node plus data about that node used by the algorithm.
type nodeHolder struct {
	// The node
	node Node

	parentsToBeNotified NodeSet

	visitationOrder int

	state visitationState
}

func newNodeHolder(node Node, visitationOrder int) *nodeHolder {
	nodeHolder := new(nodeHolder)
	nodeHolder.node = node
	nodeHolder.parentsToBeNotified = MakeNodeSet()
	nodeHolder.state = vsUnvisited
	nodeHolder.visitationOrder = visitationOrder
	return nodeHolder
}

///////////////////////////////////////////////////
/// NodeSet type
///////////////////////////////////////////////////

// A NodeSet is a set of nodeHolders.
type NodeSet struct {
	elements map[*nodeHolder]bool
}

// MakeNodeSet makes a new empty NodeSet.
func MakeNodeSet() NodeSet {
	nodeSet := NodeSet{}
	nodeSet.elements = make(map[*nodeHolder]bool)
	return nodeSet
}

// Add adds a Node to a NodeSet.
func (set *NodeSet) Add(node *nodeHolder) {
	set.elements[node] = true
}

// AddAll adds all the nodes from |otherSet| to |set|.
func (set *NodeSet) AddAll(otherSet NodeSet) {
	for e, _ := range otherSet.elements {
		set.elements[e] = true
	}
}

// Contains returns whether or not |node| is an element of |set|.
func (set *NodeSet) Contains(node *nodeHolder) bool {
	_, ok := set.elements[node]
	return ok
}

func (set *NodeSet) Remove(node *nodeHolder) {
	delete(set.elements, node)
}

func (set *NodeSet) Empty() bool {
	return len(set.elements) == 0
}

// doUntilEmpty repeatedly iterates through the elements of |set| removing
// them and invoking |f|. More precisely, for each element x of |set|,
// x is removed from |set| and then f(x) is invoked.
//
// The function |f| is allowed to mutate |set|. If |f| adds new elements to
// |set| those will also eventually be removed and operated on. Whether or
// not this process converges to an empty set depends entirely on the behavior
// of |f| and it is the caller's responsibility to ensure that it does.
func (set *NodeSet) doUntilEmpty(f func(node *nodeHolder)) {
	for !set.Empty() {
		for n, _ := range set.elements {
			set.Remove(n)
			f(n)
		}
	}
}

// removeRandomElement removes and returns an arbitrary element of |set|
// or nil of |set| is empty.
func (set *NodeSet) removeRandomElement() *nodeHolder {
	for n, _ := range set.elements {
		delete(set.elements, n)
		return n
	}
	return nil
}

func (set *NodeSet) ToNodeSlice() []Node {
	slice := make([]Node, 0, len(set.elements))
	for n, _ := range set.elements {
		slice = append(slice, n.node)
	}
	return slice
}

func (set *NodeSet) Size() int {
	return len(set.elements)
}

// compareNodeSets is a package-private method used in our tests. It returns
// a non-nil error in case expected is not equal to actual.
func compareNodeSets(expected, actual *NodeSet) error {
	for n, _ := range expected.elements {
		if !actual.Contains(n) {
			return fmt.Errorf("%s is in expected but not actual", n.node.Name())
		}
	}
	for n, _ := range actual.elements {
		if !expected.Contains(n) {
			return fmt.Errorf("%s is in actual but not expected", n.node.Name())
		}
	}
	return nil
}

// String returns a human readable string representation of |set|.
func (set *NodeSet) String() string {
	var buffer bytes.Buffer
	fmt.Fprintf(&buffer, "{")
	first := true
	for e, _ := range set.elements {
		if !first {
			fmt.Fprintf(&buffer, ", ")
		}
		fmt.Fprintf(&buffer, "%s", e.node.Name())
		first = false
	}
	fmt.Fprintln(&buffer, "}")
	return buffer.String()
}

///////////////////////////////////////////////////
// CycleDescription type
//
// A |CycleDescription| describes a cycle in a directed graph.
///////////////////////////////////////////////////

type CycleDescription struct {
	First, Last Node
	Path        []OutEdge
}

func (c *CycleDescription) String() string {
	var buffer bytes.Buffer
	fmt.Fprintf(&buffer, "first:%s", c.First.Name())
	fmt.Fprintf(&buffer, ", last:%s", c.Last.Name())
	fmt.Fprintf(&buffer, ", path:{")
	fmt.Fprintf(&buffer, "%s", c.First.Name())
	for _, edge := range c.Path {
		if edge.Target != c.First {
			fmt.Fprintf(&buffer, ", ")
			fmt.Fprintf(&buffer, "%s", edge.Target.Name())
		}
	}
	fmt.Fprintln(&buffer, "}")
	return buffer.String()
}

// newCycleDescription builds a CycleDescription based on the given data.
// |path| should be a nodePath that ends with a cycle. That is it should look like
//  {x1, ->x2}, {x2, ->x3}, {x3, ->x4}, {x4, ->x5}, {x5, ->x2}
//
// |cycleStart| should be the first node of |path| included in the cycle. In
// the above example it would be x2.
//
// |end| should be the last node of the path. In the above example it would be x5.
//
// The return value using our example would be:
// {First: x2, Last: x5, Path:[{x2, x2->x3}, {x3, x3->x4}, {x4, x4->x5}, {x5, x5->x2}]}
func newCycleDescription(path nodePath, cycleStart, end Node) *CycleDescription {
	lastNode := path[len(path)-1].node
	if lastNode != end {
		panic(fmt.Sprintf("%s != %s", lastNode.Name(), end.Name()))
	}
	lastTarget := path[len(path)-1].edge.Target
	if lastTarget != cycleStart {
		panic(fmt.Sprintf("(%T)%s != (%T)%s", lastTarget, lastTarget.Name(), cycleStart, cycleStart.Name()))
	}
	description := CycleDescription{}
	description.First = cycleStart
	description.Last = end
	description.Path = make([]OutEdge, 0, len(path))
	for _, element := range path {
		if len(description.Path) > 0 || element.node == cycleStart {
			description.Path = append(description.Path, element.edge)
		}
	}
	if description.Path[len(description.Path)-1].Target != cycleStart {
		panic(fmt.Sprintf("%s != %s", description.Path[len(description.Path)-1].Target.Name(), cycleStart.Name()))
	}
	return &description
}
