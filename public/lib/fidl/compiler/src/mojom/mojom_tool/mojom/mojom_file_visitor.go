// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package mojom

import (
	"mojom/mojom_tool/lexer"
)

type MojomElement interface {
	// AttachedComments returns the AttachedComments object stored by the
	// MojomElement. If no AttachedComments object is currently stored in
	// MojomElement, return nil.
	AttachedComments() *AttachedComments
	// NewAttachedComments creates a new AttachedComments object, stores it in
	// the MojomElement and returns a pointer to it.
	// If NewAttachedComments is called again, the old AttachedComments object is
	// discarded and a new one created.
	NewAttachedComments() *AttachedComments
	// MainToken returns a pointer to the token which best describes the location
	// of the MojomElement in the mojom file.
	MainToken() *lexer.Token
}

// MojomElementStream is a stream that yields MojomElements in the order
// that the corresponding elements occur in a mojom file.
type MojomElementStream interface {
	// Returns the current element and moves the stream to the next element.
	Next() MojomElement

	// Returns the current element without affecting the state of the stream.
	Peek() MojomElement
}

// *mojomFileVisitor implements MojomElementStream.
// The visitation order is:
// Module namespace
// Imports in the order they occur in the source mojom file.
// All declarations in the order they occur in the source mojom file.
// For ever visited element, its attributes are visited prior to the element itself.
//
// The overarching goal is that elements of the mojom file are visited in the
// order in which they occur in the source mojom file.
type mojomFileVisitor struct {
	curNode     MojomElement
	elementChan chan MojomElement
}

// NewMojomFileVisitor is a constructor for mojomFileVisitor, a MojomElementStream.
func NewMojomFileVisitor(mojomFile *MojomFile) MojomElementStream {
	visitor := new(mojomFileVisitor)
	visitor.elementChan = make(chan MojomElement)
	go visitor.visitMojomFile(mojomFile)
	// Initialize visitor.curNode.
	visitor.Next()
	return visitor
}

// See MojomElementStream.
func (v *mojomFileVisitor) Next() MojomElement {
	curNode := v.curNode

	if element, open := <-v.elementChan; open {
		v.curNode = element
	} else {
		v.curNode = nil
	}
	return curNode
}

// See MojomElementStream.
func (v *mojomFileVisitor) Peek() MojomElement {
	return v.curNode
}

// visitDeclaredObject visits a DeclaredObject. With the exception of import
// statements, attributes and the module statement, every element of a mojom file
// is a DeclaredObject.
// The object's Attributes are visited first, followed by the object itself
// and finally by the contents of the object in question.
func (v *mojomFileVisitor) visitDeclaredObject(object DeclaredObject) {
	if object.Attributes() != nil {
		v.visitAttributes(object.Attributes())
	}
	v.elementChan <- object

	switch t := object.(type) {
	case DeclaredObjectsContainer:
		v.visitDeclaredObjectsContainer(t)
	case *MojomMethod:
		v.visitMojomMethodParams(t)
	}
}

// visitDeclaredObjectContainer visits the contents of a DeclaredObjectsContainer.
// DeclaredObjectContains are MojomInterface, MojomStruct, MojomUnion and MojomEnum.
func (v *mojomFileVisitor) visitDeclaredObjectsContainer(object DeclaredObjectsContainer) {
	for _, subobject := range object.GetDeclaredObjects() {
		v.visitDeclaredObject(subobject)
	}
}

// visitMojomMethodParams visits both the parameters and response parameters of
// a MojomMethod.
// The input Parameters are visited first, followed by the ResponseParameters.
func (v *mojomFileVisitor) visitMojomMethodParams(method *MojomMethod) {
	if method.Parameters != nil {
		v.visitDeclaredObjectsContainer(method.Parameters)
	}
	if method.ResponseParameters != nil {
		v.visitDeclaredObjectsContainer(method.ResponseParameters)
	}
}

// visitAttributes visits Attributes.
// First, the Attributes element itself is visited, then the individual Attribute
// elements are visited.
func (v *mojomFileVisitor) visitAttributes(attributes *Attributes) {
	v.elementChan <- attributes
	for i, _ := range attributes.List {
		v.elementChan <- &attributes.List[i]
	}
}

// visitMojomFile visits a MojomFile.
// The order of visitation is file-level Attributes, ModuleNamespace, Imports,
// DeclaredObjects.
func (v *mojomFileVisitor) visitMojomFile(file *MojomFile) {
	if file.Attributes != nil {
		v.visitAttributes(file.Attributes)
	}

	if file.ModuleNamespace != nil && file.ModuleNamespace.Identifier != "" {
		v.elementChan <- file.ModuleNamespace
	}

	for _, importedFile := range file.Imports {
		v.elementChan <- importedFile
	}

	for _, object := range file.DeclaredObjects {
		v.visitDeclaredObject(object)
	}

	close(v.elementChan)
}
