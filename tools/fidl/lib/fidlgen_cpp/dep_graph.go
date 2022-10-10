// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"fmt"
	"sort"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

// DeclDepGraph represents the definitional dependency graph between
// declarations in a library.
//
// We do not create edges to nullable types. This makes it possible to write
// some recursive types in FIDL. However, we do not have comprehensive support
// for recursive types. See fxbug.dev/35218 for details.
//
// Similarly, we do not create edges to protocols via endpoint dependencies, as
// that same protocol may freely appear in the response type of one its
// methods.
type DeclDepGraph struct {
	nodes declDepNodeMap
	decls map[fidlgen.EncodedCompoundIdentifier]fidlgen.Decl
}

type declDepNode struct {
	// The name of the associated declaration.
	name fidlgen.EncodedCompoundIdentifier

	// The reverse dependencies (i.e., dependents) of the associated
	// declaration. This is tracked to facilitate a topological sort with
	// respect to dependency.
	revDeps declDepNodeMap
}

type declDepNodeMap map[fidlgen.EncodedCompoundIdentifier]*declDepNode

// NewDeclDepGraph computes the dependency graph for a given library.
func NewDeclDepGraph(r fidlgen.Root) DeclDepGraph {
	g := DeclDepGraph{
		nodes: make(declDepNodeMap),
		decls: make(map[fidlgen.EncodedCompoundIdentifier]fidlgen.Decl),
	}
	r.ForEachDecl(func(decl fidlgen.Decl) { g.addDecl(decl) })
	return g
}

// SortedDecls returns a deterministic, topologically sorted list of the
// associated declarations, so that a declaration always appears before those
// that depend on it. Beyond preserving this relation, the list attempts to
// prioritize declarations in terms of their original ordering in source.
func (g *DeclDepGraph) SortedDecls() []fidlgen.Decl {
	const (
		nodeProccesing int = iota
		nodeProcessed
	)

	var decls []fidlgen.Decl
	nodeState := make(map[fidlgen.EncodedCompoundIdentifier]int)
	var visit func(*declDepNode, []string)
	visit = func(node *declDepNode, depChain []string) {
		depChain = append(depChain, string(node.name))
		if state, ok := nodeState[node.name]; ok {
			switch state {
			case nodeProccesing:
				panic(fmt.Sprintf("cyclic dependency: %s", strings.Join(depChain, " <- ")))
			case nodeProcessed:
				return
			}
		}

		nodeState[node.name] = nodeProccesing
		for _, dep := range g.normalizeNodes(node.revDeps) {
			visit(dep, depChain)
		}
		if decl, ok := g.decls[node.name]; ok {
			decls = append([]fidlgen.Decl{decl}, decls...)
		}
		nodeState[node.name] = nodeProcessed
		depChain = depChain[:len(depChain)-1]
	}

	for _, node := range g.normalizeNodes(g.nodes) {
		visit(node, nil)
	}
	return decls
}

// GetDirectDependents returns the declarations that are directly dependent on
// a given one, referenced by name. The returned declarations are given in
// source order (lexicographically first on filename). A boolean is also
// returned indicating whether the provided declaration is contained in the
// graph.
func (g DeclDepGraph) GetDirectDependents(name fidlgen.EncodedCompoundIdentifier) ([]fidlgen.Decl, bool) {
	// First check whether the provided name represents a local declaration.
	if _, ok := g.decls[name]; !ok {
		return nil, false
	}
	node, ok := g.nodes[name]
	if !ok {
		return nil, false
	}
	var decls []fidlgen.Decl
	for _, revDep := range node.revDeps {
		// All direct dependents on a local declaration should be local declarations themselves.
		decls = append(decls, g.decls[revDep.name])
	}
	sort.Slice(decls, func(i, j int) bool {
		return fidlgen.LocationCmp(decls[i].GetLocation(), decls[j].GetLocation())
	})
	return decls, true
}

// Since `map` access is randomized, a normalization of `declDepNodeMap`s is
// needed to produce a deterministic list. Topological sorting alone is
// insufficient, as there are many possible orderings that preserve that
// relation.
//
// To approximate the desired order described on SortedDecls(), we sort nodes
// with regards to the *opposite* of the following relations (opposite since we
// will be *prepending* items in this order during topological sorting):
//
// * If both nodes represent local (i.e., non-imported) declarations, source
// ordering is preserved (falling back to a lexicographic comparison on
// filenames if the two declarations came from different files).
//
// * If both nodes represent imported declarations, the one with the
// lexicographically smaller name is prioritized. (This is an arbitrary choice.)
//
// * If only one node of the two represents a local declaration, the local one
// is prioritized. (This is also an arbitrary choice.)
func (g DeclDepGraph) normalizeNodes(m declDepNodeMap) []*declDepNode {
	var nodes []*declDepNode
	for _, node := range m {
		nodes = append(nodes, node)
	}
	sort.Slice(nodes, func(i, j int) bool {
		iName := nodes[i].name
		jName := nodes[j].name
		iDecl, iLocal := g.decls[iName]
		jDecl, jLocal := g.decls[jName]
		if iLocal && jLocal {
			return !fidlgen.LocationCmp(iDecl.GetLocation(), jDecl.GetLocation())
		}
		if iLocal != jLocal {
			return iLocal
		}
		return strings.Compare(string(iName), string(jName)) > 0
	})
	return nodes
}

func (g *DeclDepGraph) addDecl(decl fidlgen.Decl) {
	node := g.getNode(decl.GetName())
	g.decls[node.name] = decl

	switch decl := decl.(type) {
	case *fidlgen.Const:
		g.addDepsFromType(node, decl.Type)
		g.addDepsFromConstant(node, decl.Value)
	case *fidlgen.Bits:
		g.addDepsFromType(node, decl.Type)
		for _, m := range decl.Members {
			g.addDepsFromConstant(node, m.Value)
		}
	case *fidlgen.Enum:
		for _, m := range decl.Members {
			g.addDepsFromConstant(node, m.Value)
		}
	case *fidlgen.Resource:
		for _, prop := range decl.Properties {
			g.addDepsFromType(node, prop.Type)
		}
	case *fidlgen.Protocol:
		for _, comp := range decl.Composed {
			g.addDep(node, comp.Name)
		}
		for _, m := range decl.Methods {
			if req, ok := m.GetRequestPayloadIdentifier(); ok {
				g.addDep(node, req)
			}
			if resp, ok := m.GetResponsePayloadIdentifier(); ok {
				g.addDep(node, resp)
			}
		}
	case *fidlgen.Service:
		for _, m := range decl.Members {
			g.addDepsFromType(node, m.Type)
		}
	case *fidlgen.Struct:
		for _, m := range decl.Members {
			g.addDepsFromType(node, m.Type)
			if m.MaybeDefaultValue != nil {
				g.addDepsFromConstant(node, *m.MaybeDefaultValue)
			}
			if m.MaybeAlias != nil {
				g.addDepsFromTypeCtor(node, *m.MaybeAlias)
			}
		}
	case *fidlgen.Table:
		for _, m := range decl.Members {
			if m.Reserved {
				continue
			}
			g.addDepsFromType(node, *m.Type)
			if m.MaybeDefaultValue != nil {
				g.addDepsFromConstant(node, *m.MaybeDefaultValue)
			}
			if m.MaybeAlias != nil {
				g.addDepsFromTypeCtor(node, *m.MaybeAlias)
			}
		}
	case *fidlgen.Union:
		for _, m := range decl.Members {
			if m.Reserved {
				continue
			}
			g.addDepsFromType(node, *m.Type)
			if m.MaybeAlias != nil {
				g.addDepsFromTypeCtor(node, *m.MaybeAlias)
			}
		}
	case *fidlgen.Alias:
		g.addDepsFromTypeCtor(node, decl.PartialTypeConstructor)
	case *fidlgen.NewType:
		if decl.Alias != nil {
			g.addDepsFromTypeCtor(node, *decl.Alias)
		} else {
			g.addDepsFromType(node, decl.Type)
		}
	}
}

func (g *DeclDepGraph) getNode(decl fidlgen.EncodedCompoundIdentifier) *declDepNode {
	node, ok := g.nodes[decl]
	if !ok {
		node = &declDepNode{
			name:    decl,
			revDeps: make(declDepNodeMap),
		}
		g.nodes[decl] = node
	}
	return node
}

func (g *DeclDepGraph) addDep(node *declDepNode, dep fidlgen.EncodedCompoundIdentifier) {
	g.getNode(dep).revDeps[node.name] = node
}

func (g *DeclDepGraph) addDepsFromConstant(node *declDepNode, c fidlgen.Constant) {
	if c.Kind == fidlgen.IdentifierConstant {
		g.addDep(node, c.Identifier)
	}
}

func (g *DeclDepGraph) addDepsFromType(node *declDepNode, typ fidlgen.Type) {
	// As above, we do not create edges to nullable types or protocols via
	// endpoint dependencies.
	if typ.Nullable || typ.ProtocolTransport != "" {
		return
	}

	switch typ.Kind {
	case fidlgen.ArrayType, fidlgen.VectorType:
		g.addDepsFromType(node, *typ.ElementType)
	case fidlgen.HandleType:
		// TODO(fxbug.dev/7660): ResourceIdentifier should be an
		// `fidlgen.EncodedCompoundIdentifier`.
		g.addDep(node, fidlgen.EncodedCompoundIdentifier(typ.ResourceIdentifier))
	case fidlgen.IdentifierType:
		g.addDep(node, typ.Identifier)
	}
}

func (g *DeclDepGraph) addDepsFromTypeCtor(node *declDepNode, ctor fidlgen.PartialTypeConstructor) {
	// As above, we do not create edges to nullable types.
	if ctor.Nullable {
		return
	}
	if !ctor.Name.IsBuiltIn() {
		g.addDep(node, ctor.Name)
	}
	for _, arg := range ctor.Args {
		g.addDepsFromTypeCtor(node, arg)
	}
}
