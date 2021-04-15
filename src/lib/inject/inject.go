// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package inject

import (
	"fmt"
	"reflect"
)

type ImplementedBy struct {
	Target, Impl reflect.Type
}

func Start(target interface{}, implementedBy ...ImplementedBy) (Stopper, error) {
	var (
		val  = reflect.ValueOf(target)
		root = &node{
			val: val,
			typ: val.Type(),
		}
		g = &graph{
			nodes: map[reflect.Type]*node{
				root.typ: root,
			},
		}
	)
	if !isPtrToStruct(root.typ) {
		return nil, fmt.Errorf(mustBePointerToStruct, target)
	}
	targetToImpl := make(map[reflect.Type]reflect.Type, len(implementedBy))
	for _, elem := range implementedBy {
		if elem.Target.Kind() != reflect.Interface {
			return nil, fmt.Errorf("%s is not an interface", elem.Target)
		}
		if !elem.Impl.Implements(elem.Target) {
			return nil, fmt.Errorf("%s does not implement %s", elem.Impl, elem.Target)
		}
		targetToImpl[elem.Target] = elem.Impl
	}
	if err := g.instantiate(root, targetToImpl); err != nil {
		return nil, err
	}
	if err := g.queueToStart(indexedNode{}, indexedNode{node: root}); err != nil {
		return nil, err
	}
	if err := g.start(); err != nil {
		return nil, err
	}
	return g, nil

}

type Starter interface {
	Start() error
}

type Stopper interface {
	Stop() error
}

type nodeStatus int

const (
	instantiated nodeStatus = iota
	queuedToStart
	readyToStart
	started
	stopped
)

type indexedNode struct {
	index []int
	node  *node
}

type node struct {
	status nodeStatus

	typ reflect.Type
	val reflect.Value

	// children is a slice of indexed nodes, where the index represents the
	// location of the child in the parent (this node).
	children []indexedNode

	// parents is a slice of indexed nodes, where the index represents the
	// location of the child (this node) in the parent.
	parents []indexedNode
}

type graph struct {
	nodes         map[reflect.Type]*node
	stoppingOrder []*node
}

var _ Stopper = (*graph)(nil)

func (g *graph) Stop() error {
	return g.stop()
}

func (g *graph) instantiate(root *node, targetToImpl map[reflect.Type]reflect.Type) error {
	var (
		current *node
		queue   = []*node{root}
	)
	for len(queue) > 0 {
		current, queue = queue[0], queue[1:]
		childrenFields, err := fieldsToInject(current.typ.Elem(), targetToImpl)
		if err != nil {
			return err
		}
		for _, childField := range childrenFields {
			child, ok := g.nodes[childField.typ]
			if !ok {
				child = &node{
					typ: childField.typ,
					val: reflect.New(childField.typ.Elem()),
				}
				g.nodes[childField.typ] = child
				queue = append(queue, child)
			}
			current.children = append(current.children, indexedNode{
				index: childField.index,
				node:  child,
			})
			child.parents = append(child.parents, indexedNode{
				index: childField.index,
				node:  current,
			})
		}
	}
	return nil
}

func (g *graph) queueToStart(parent, current indexedNode) error {
	switch current.node.status {
	case instantiated:
		g.stoppingOrder = append(g.stoppingOrder, current.node)
		current.node.status = queuedToStart
		for _, child := range current.node.children {
			if err := g.queueToStart(current, child); err != nil {
				// generates errors such as `a > b > c > a: cyclic dependency`
				return fmt.Errorf("%s > %s", current.node.typ, err)
			}
		}
		current.node.status = readyToStart
		return nil
	case queuedToStart:
		return fmt.Errorf("%s: cyclic dependency", current.node.typ)
	case readyToStart:
		return nil
	default:
		panic(fmt.Sprintf("unreachable; bad status: %d", current.node.status))
	}
}

var (
	starterTyp = reflect.TypeOf((*Starter)(nil)).Elem()
	stopperTyp = reflect.TypeOf((*Stopper)(nil)).Elem()
)

func (g *graph) start() error {
	for i := len(g.stoppingOrder) - 1; 0 <= i; i-- {
		current := g.stoppingOrder[i]
		if current.status != readyToStart {
			panic("unreachable; all nodes should be ready to start")
		}
		if err := current.ifAssignableToCall(starterTyp, "Start"); err != nil {
			return err
		}
		for _, parent := range current.parents {
			parent.node.val.Elem().FieldByIndex(parent.index).Set(current.val)
		}
		current.status = started
	}
	return nil
}

func (g *graph) stop() error {
	for i := 0; i < len(g.stoppingOrder); i++ {
		current := g.stoppingOrder[i]
		switch current.status {
		case stopped:
			return fmt.Errorf("cannot stop twice")
		case started:
			// good; expected
		default:
			panic("unreachable; all nodes should have started")
		}
		if err := current.ifAssignableToCall(stopperTyp, "Stop"); err != nil {
			return err
		}
		current.status = stopped
	}
	return nil
}

func (n *node) ifAssignableToCall(assignableTo reflect.Type, methodName string) error {
	if n.typ.AssignableTo(assignableTo) {
		m, ok := n.typ.MethodByName(methodName)
		if !ok {
			panic(fmt.Sprintf("unreachable; implements %s but missing %s method", assignableTo, methodName))
		}
		if errVal := m.Func.Call([]reflect.Value{n.val})[0]; !errVal.IsNil() {
			return errVal.Interface().(error)
		}
	}
	return nil
}

const (
	mustBePointerToStruct = "must be pointer to struct, found %T"
)

func isPtrToStruct(typ reflect.Type) bool {
	return typ.Kind() == reflect.Ptr && typ.Elem().Kind() == reflect.Struct
}

const tagKey = "inject"

type indexAndType struct {
	index []int
	typ   reflect.Type
}

func fieldsToInject(typ reflect.Type, targetToImpl map[reflect.Type]reflect.Type) ([]indexAndType, error) {
	var fields []indexAndType
	for i := 0; i < typ.NumField(); i++ {
		field := typ.Field(i)
		tagVal, ok := field.Tag.Lookup(tagKey)
		if !ok {
			continue
		}
		if tagVal != "" {
			return nil, fmt.Errorf("%s.%s: invalid tag value, must be empty", typ.Name(), field.Name)
		}
		var implTyp reflect.Type
		if toImpl, ok := targetToImpl[field.Type]; ok {
			implTyp = toImpl
		} else if !isPtrToStruct(field.Type) {
			return nil, fmt.Errorf("%s.%s: invalid type, "+mustBePointerToStruct, typ.Name(), field.Name, field.Type)
		} else {
			implTyp = field.Type
		}
		fields = append(fields, indexAndType{
			index: field.Index,
			typ:   implTyp,
		})
	}
	return fields, nil
}
