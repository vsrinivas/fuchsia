// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package mojom

import (
	"fmt"
	"mojom/mojom_tool/lexer"
	"strings"
)

/*
This file contains data structures and functions used to describe scopes. A scope
is a named container in which named entities may be defined and in which the entity
corresponding to a name is looked up during the resolution phase of the parser.

The collection of all scopes form a tree in which each scope other than the
root scope is contained in a parent scope. Scopes are named using dotted
identifiers (like foo.bar.baz) and the name of a parent scope is always equal
to the name of the child scope minus the final element. So the parent of
a scope named "foo.bar.baz" must be named "foo.bar".

The lookup procedure for a name starts with a scope associated with the use of
the name and proceeds by searching up through the chain of parents until a
match is found. For details see the comments at RegisterType, RegisterValue,
LookupType and LookupValue.

There are two categories of scopes: lexical scopes and abstract module scopes.
Lexical scopes are associated with a lexical structure in a .mojom file. These
are the types of scopes in which a named entity may be defined. There
are four kinds of lexical scopes:
(1) A mojom file
(2) An interface
(3) A struct
(4) An enum

Not every type of named entity may be defined in every type of scope. For
example in an enum scope the only type of named entity allowed is an enum value.

An abstract module scope is not associated with any concrete lexical element of
a .mojom file but rather is used to represent the hierarchy of scopes that
exist implicitly becuase of the dotted names of mojom file modules. For example
if a .mojom file declares a module namespace of "foo.bar" this implicitly
creates three abstract module scopes named "foo.bar", "bar" and "". Abstract
module scopes are uniquely identified within a MojomDescriptor by their names.
The abstract module scope named "" is the unique root scope.

We also register some named objects into a scope even though these objects
will never be looked up because they are non-referents, meaning they cannot
be referred to. For example we register methods within an interface
scope even though we will never need to lookup of the method by name because
it is not possible to refer to a method from within a .mojom file. The reason
we do this is that it *is* possible to refer to a method in the generated
code. Thus it is important to disallow, for example, a method and an enum
in the same interface to have the same name.
*/

type ScopeKind int

const (
	ScopeAbstractModule ScopeKind = iota
	ScopeFileModule
	ScopeInterface
	ScopeStruct
	ScopeEnum
)

func (k ScopeKind) String() string {
	switch k {
	case ScopeAbstractModule:
		return "abstract module"
	case ScopeFileModule:
		return "file module"
	case ScopeInterface:
		return "interface"
	case ScopeStruct:
		return "struct"
	case ScopeEnum:
		return "enum"
	default:
		panic(fmt.Sprintf("Unrecognized ScopeKind %d", k))
	}
}

type Scope struct {
	kind               ScopeKind
	shortName          string
	fullyQualifiedName string
	parentScope        *Scope
	declaredObjects    map[string]DeclaredObject
	// file is nil for abstract module scopes
	file *MojomFile
	// If this is an Interface, Struct or Enum scope then |containingType|
	// is the corresponding Interface, Struct or Enum.
	containingType UserDefinedType
	descriptor     *MojomDescriptor
}

func buildDottedName(prefix, suffix string) string {
	if len(prefix) == 0 {
		return suffix
	}
	return fmt.Sprintf("%s.%s", prefix, suffix)
}

// init is invoked by NewLexicalScope and NewAbstractModuleScope
func (scope *Scope) init(kind ScopeKind, shortName string,
	fullyQualifiedName string, parentScope *Scope, containingType UserDefinedType, descriptor *MojomDescriptor) {
	scope.kind = kind
	scope.shortName = shortName
	scope.fullyQualifiedName = fullyQualifiedName
	scope.parentScope = parentScope
	scope.declaredObjects = make(map[string]DeclaredObject)
	scope.containingType = containingType
	scope.descriptor = descriptor
}

// NewLexicalScope creates a new LexicalScope. The scopeKind must be
// one of the lexical kinds, not ScopeAbstractModule. The file must not be nil
// and it must not have a nil descriptor because the new scope will be
// embedded into the tree of scopes for that descriptor.
// The parent scope must be appropriate for the type of scope being created.
// When creating a ScopeFileModule parentScope should be nil: The
// parent scope will be set to an abstract module scope automatically.
//
// |containingType| must be non-nil just in case an Interface, Struct  or Enum scope
// is being created and in that case it should be the Interface, Struct or Enum.
func NewLexicalScope(kind ScopeKind, parentScope *Scope, shortName string,
	file *MojomFile, containingType UserDefinedType) *Scope {
	scope := new(Scope)
	if file == nil {
		panic("The file must not be nil for a lexical scope.")
	}
	scope.file = file
	var fullyQualifiedName string

	switch kind {
	case ScopeFileModule:
		if parentScope != nil {
			panic("A file module lexical scope cannot have a parent lexical scope.")
		}
		if containingType != nil {
			panic("A file scope does not have a containing type.")
		}
		fullyQualifiedName = file.ModuleNamespace.Identifier
		parentScope = file.Descriptor.getAbstractModuleScope(fullyQualifiedName)
	case ScopeInterface:
		if parentScope == nil || parentScope.kind != ScopeFileModule {
			panic("An interface lexical scope must have a parent lexical scope of type FILE_MODULE.")
		}
		if containingType == nil {
			panic("An interface scope must have a containing type.")
		}
		fullyQualifiedName = buildDottedName(parentScope.fullyQualifiedName, shortName)
	case ScopeStruct:
		if containingType == nil {
			panic("A struct scope must have a containing type.")
		}
		containingStruct := containingType.(*MojomStruct)
		switch containingStruct.structType {
		case StructTypeRegular:
			if parentScope == nil || parentScope.kind != ScopeFileModule {
				panic("A struct lexical scope must have a parent lexical scope of type FILE_MODULE.")
			}
		default:
			if parentScope == nil || parentScope.kind != ScopeInterface {
				panic("A synthetic parameter struct lexical scope must have a parent lexical scope of type INTERFACE.")
			}
		}

		fullyQualifiedName = buildDottedName(parentScope.fullyQualifiedName, shortName)
	case ScopeEnum:
		if parentScope == nil || parentScope.kind == ScopeAbstractModule {
			panic("An enum lexical scope must have a parent lexical scope not an ABSTRACT_MODULE scope.")
		}
		if containingType == nil {
			panic("An enum scope must have a containing type.")
		}
		fullyQualifiedName = buildDottedName(parentScope.fullyQualifiedName, shortName)
	case ScopeAbstractModule:
		panic("The type of a lexical scope cannot be ABSTRACT_MODULE.")
	default:
		panic(fmt.Sprintf("Unrecognized ScopeKind %d", kind))
	}

	scope.init(kind, shortName, fullyQualifiedName, parentScope, containingType, file.Descriptor)

	return scope
}

// Creates a new abstract module scope. This is not invoked directly by the parsing
// code but rather is invoked by the methods of MojomDescriptor to lazily create
// abstract module scopes when they are needed.
func NewAbstractModuleScope(fullyQualifiedName string, descriptor *MojomDescriptor) *Scope {
	scope := new(Scope)
	var parentScope *Scope = nil
	shortName := fullyQualifiedName
	if len(fullyQualifiedName) > 0 {
		splitName := strings.Split(fullyQualifiedName, ".")
		numSegments := len(splitName)
		shortName = splitName[numSegments-1]

		if numSegments > 1 {
			parentFullyQualifiedName := strings.Join(splitName[0:numSegments-1], ".")
			parentScope = descriptor.getAbstractModuleScope(parentFullyQualifiedName)
		} else {
			parentScope = descriptor.getGlobalScobe()
		}
	}
	scope.init(ScopeAbstractModule, shortName, fullyQualifiedName, parentScope, nil, descriptor)
	return scope
}

func (s *Scope) Parent() *Scope {
	return s.parentScope
}

func (s *Scope) String() string {
	fileNameString := ""
	if s.file != nil {
		fileNameString = fmt.Sprintf(" in %s", s.file.CanonicalFileName)
	}
	return fmt.Sprintf("%s %s%s", s.kind, s.shortName, fileNameString)
}

type DuplicateNameError interface {
	error

	// NameToken() returns the token corresponding to the name of the
	// duplicate object.
	NameToken() lexer.Token

	// The MojomFile containing the duplicate definition.
	OwningFile() *MojomFile
}

type DuplicateNameErrorBase struct {
	nameToken  lexer.Token
	owningFile *MojomFile
}

func (e *DuplicateNameErrorBase) NameToken() lexer.Token {
	return e.nameToken
}

func (e *DuplicateNameErrorBase) OwningFile() *MojomFile {
	return e.owningFile
}

type DuplicateDeclaredNameError struct {
	DuplicateNameErrorBase
	existingObject DeclaredObject
}

func (e *DuplicateDeclaredNameError) Error() string {
	message := fmt.Sprintf("Duplicate definition for %q. "+
		"Previous definition with the same fully-qualified name: %s %s at %s.",
		e.nameToken.Text, e.existingObject.KindString(), e.existingObject.FullyQualifiedName(),
		FullLocationString(e.existingObject))
	return UserErrorMessage(e.owningFile, e.nameToken, message)
}

func newDuplicateDeclaredNameError(duplicateObject, existingObject DeclaredObject) DuplicateNameError {
	return &DuplicateDeclaredNameError{
		DuplicateNameErrorBase{nameToken: duplicateObject.NameToken(), owningFile: duplicateObject.Scope().file},
		existingObject}
}

// registerObjectWithNamePrefix is a recursive helper method used by RegisterType, RegisterValue,
// RegisterMethod, and RegisterStructField
func (scope *Scope) registerObjectWithNamePrefix(declaredObject DeclaredObject, namePrefix string) DuplicateNameError {
	if scope == nil {
		panic("scope is nil")
	}
	if declaredObject.Scope() == nil {
		panic(fmt.Sprintf("scope is nil for %s", declaredObject))
	}
	if scope.declaredObjects == nil {
		panic("Init() must be called for this Scope before this method may be invoked.")
	}
	registrationName := namePrefix + declaredObject.SimpleName()
	if existingObject := scope.declaredObjects[registrationName]; existingObject != nil {
		return newDuplicateDeclaredNameError(declaredObject, existingObject)
	}
	scope.declaredObjects[registrationName] = declaredObject
	if scope.parentScope != nil {
		if scope.kind == ScopeFileModule {
			if scope.parentScope.kind != ScopeAbstractModule {
				panic("The parent scope of a file module should always be an abstract module.")
			}

		} else {
			// We extend the name prefix by prepending the name of the current
			// scope. But notice that we do not do this in the special case that
			// the current scope is a ScopeFileModule. This is becuase the
			// parent scope of a file module scope is an abstract module
			// scope with the same name.
			namePrefix = buildDottedName(scope.shortName, namePrefix)
		}
		if err := scope.parentScope.registerObjectWithNamePrefix(declaredObject,
			namePrefix); err != nil {
			return err
		}
	}
	return nil
}

// RegisterType registers a UserDefinedType in this scope and the chain of
// its ancestor scopes. This registration process is the key to our name
// resolution algorithm. The UserDefinedType is registered in this scope
// using its given short name, and then it is registered in the ancestor
// scopes using progressively longer qualified names obtained by prepending
// the simple name of the child scope. The UserDefinedType will be registered in
// the root scope using its fully qualified name.
//
// For example, suppose we are registering an EnumType named Joe in an interface
// named Baz in a module named foo.bar. The following chart describes the five
// scopes into which Joe will be registered and the names with which it
// will be registered in each of the scopes.
//
// root scope:              ""             registration name: foo.bar.Baz.Joe
// abstract module scope:   "foo"          registration name: bar.Baz.Joe
// abstract module scope:   "foo.bar"      registration name: Baz.Joe
// file lexical scope:      "foo.bar"      registration name: Baz.Joe
// interface lexical scope: "foo.bar.Baz"  registration name: Joe
func (scope *Scope) RegisterType(userDefinedType UserDefinedType) DuplicateNameError {
	return scope.registerObjectWithNamePrefix(userDefinedType, "")
}

// RegisterValue registers a UserDefinedValue in this scope and the chain of
// its ancestor scopes. This registration process is the key to our name
// resolution algorithm. The UserDefinedValue is registered in this scope
// using its given short name, and then it is registered in the ancestor
// scopes using progressively longer qualified names obtained by prepending
// the simple name of the child scope. The UserDefinedValue will be registered in
// the root scope using its fully qualified name.
//
// For example, suppose we are registering an EnumValue named FROG in an Enum
// named Joe in an interface named Baz in a module named foo.bar. The following
// chart describes the six scopes into which FROG will be registered and the
// names with which it will be registered in each of the scopes.
//
// root scope:              ""                 registration name: foo.bar.Baz.Joe.FROG
// abstract module scope:   "foo"              registration name: bar.Baz.Joe.FROG
// abstract module scope:   "foo.bar"          registration name: Baz.Joe.FROG
// file lexical scope:      "foo.bar"          registration name: Baz.Joe.FROG
// interface lexical scope: "foo.bar.Baz"      registration name: Joe.FROG
// enum lexical scope:      "foo.bar.Baz.Joe"  registration name: FROG
func (scope *Scope) RegisterValue(value UserDefinedValue) DuplicateNameError {
	return scope.registerObjectWithNamePrefix(value, "")
}

func (scope *Scope) RegisterMethod(method *MojomMethod) DuplicateNameError {
	return scope.registerObjectWithNamePrefix(method, "")
}

func (scope *Scope) RegisterStructField(field *StructField) DuplicateNameError {
	return scope.registerObjectWithNamePrefix(field, "")
}

// LookupAccept is a datatype that is used as a bitmask of options for the kind of
// objects to be accepted during a name lookup.
type LookupAccept uint32

// The various kinds of LookupAccepts correspond to the Go types that implement
// |DeclaredObject|.
const (
	LookupAcceptType LookupAccept = 2 << iota
	LookupAcceptValue
	LookupAcceptMethod
	LookupAcceptField
)

const LookupAcceptAll LookupAccept = LookupAcceptType | LookupAcceptValue | LookupAcceptMethod | LookupAcceptField
const LookupAcceptNone LookupAccept = 0

func acceptType(acceptFilter LookupAccept) bool {
	return acceptFilter&LookupAcceptType != 0
}

func acceptValue(acceptFilter LookupAccept) bool {
	return acceptFilter&LookupAcceptValue != 0
}

func acceptMethod(acceptFilter LookupAccept) bool {
	return acceptFilter&LookupAcceptMethod != 0
}

func acceptField(acceptFilter LookupAccept) bool {
	return acceptFilter&LookupAcceptField != 0
}

// LookupObject searches for a DeclaredObject registered in this scope with the
// given |name| and then if one is not found, searches recursively in the
// parent scope. Returns nil if the lookp fails.
//
// |acceptFilter| is a filter that modifies the lookup procedure by specifying the
// kinds of objects being looked up. If acceptFilter = |LookupAcceptAll| then all
// kinds of DeclaredObjects are accepted and the first object with a matching name
// will be returned. But if, for example, acceptType(acceptFilter) is true but
// acceptValue(acceptFilter) is false then, if a value with a matching name is
// found the lookup procedure will not return that value but rather will continue
// looking for a matching type.
//
// The purpose of the |acceptFilter| argument is to allow different lookup
// algorithms to be implemented. For example when looking for a type named
// "Foo", suppose there is a value named "Foo" in the local scope. The question
// is: should we say that the name "Foo" refers to the value Foo and then emit
// a compilation error because a type was expected? Or should we instead ignore
// the value Foo because we know a type is expected and keep looking in the parent
// scope for a type Foo. This layer does not make that decision and instead
// offers the flexibility of the |acceptFilter| argument. It is the caller
// of this method that must make that decision.
func (scope *Scope) LookupObject(name string, acceptFilter LookupAccept) DeclaredObject {
	if acceptFilter == LookupAcceptNone {
		panic("acceptFilter may not be zero.")
	}
	if declaredObject, ok := scope.declaredObjects[name]; ok {
		if acceptFilter == LookupAcceptAll {
			return declaredObject
		}
		switch declaredObject := declaredObject.(type) {
		case UserDefinedType:
			if acceptType(acceptFilter) {
				return declaredObject
			}
		case UserDefinedValue:
			if acceptValue(acceptFilter) {
				return declaredObject
			}
		case *MojomMethod:
			if acceptMethod(acceptFilter) {
				return declaredObject
			}
		case *StructField:
			if acceptField(acceptFilter) {
				return declaredObject
			}
		default:
			panic(fmt.Sprintf("Unexpected type of declared object %T", declaredObject))
		}
	}
	if scope.parentScope == nil {
		return nil
	}
	return scope.parentScope.LookupObject(name, acceptFilter)
}
