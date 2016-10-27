// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package mojom

import (
	"bytes"
	"fmt"
	"mojom/mojom_tool/lexer"
	"sort"
	"strings"
)

// This file contains the types MojomFile and MojomDescriptor. These are the
// structures that are generated during parsing and then serialized and
// passed on to the backend of the Mojom Compiler.

///////////////////////////////////////////////////////////////////////
/// Type MojomFile
/// //////////////////////////////////////////////////////////////////

// A MojomFile represents the result of parsing a single .mojom file.
type MojomFile struct {
	// The associated MojomDescriptor
	Descriptor *MojomDescriptor

	// The |CanonicalFileName| is the unique identifier for this module
	// within the |MojomFilesByName| field of |Descriptor|
	CanonicalFileName string

	// The |SpecifiedFileName| will be empty if and only if |ImportedFrom|
	// is not nil. This indicates that this file is included in the graph not
	// because it was explicitly requested, but rather because it is imported
	// from a different file in the graph. If |SpecifiedFileName| is not empty
	// it indicates that this file was explicitly requested using the name
	// in  this field.
	SpecifiedFileName string

	// The module namespace is the identifier declared via the "module"
	// declaration in the .mojom file.
	ModuleNamespace *ModuleNamespace

	// Attributes declared in the Mojom file at the module level.
	Attributes *Attributes

	// The set of other MojomFiles imported by this one. The corresponding
	// MojomFile may be obtained from the |MojomFilesByName| field of
	// |Descriptor| using the |CanonicalFileName| field of ImportedFile.
	Imports []*ImportedFile

	// importsBySpecifiedName facilitates the lookup of an ImportedFile given its |SpecifiedName|
	importsBySpecifiedName map[string]*ImportedFile

	// If this file was imported from one or more files in the MojomFileGraph
	// and this file was not a top-level file then this field will contain a
	// pointer to one of the importing files. Thus this field is a partial
	// inverse to |Imports|. The value of this field is dependent on the order
	// in which the files in the import graph were traversed. See also |SpecifiedFileName|.
	ImportedFrom *MojomFile

	// The lexical scope corresponding to this file.
	FileScope *Scope

	// These are lists of *top-level* types  and constants defined in the file;
	// they do not include enums and constants defined within structs
	// and interfaces. The contained enums and constant may be found in the
	// |Enums| and |Constants|  fields of their containing object.
	Interfaces []*MojomInterface
	Structs    []*MojomStruct
	Unions     []*MojomUnion
	Enums      []*MojomEnum
	Constants  []*UserDefinedConstant

	// The contents of the .mojom file.
	fileContents string

	// DeclaredObjects is a list of all user declared objects in order of
	// occurrence in the source.
	// It is the union of Interfaces, Structs, Unions, Enums and Constants.
	DeclaredObjects []DeclaredObject

	// FinalComments is the list of comments at the end of the file after all
	// declarations.
	FinalComments []lexer.Token
}

// An ImportedFile represents an element of the "import" list of a .mojom file.
type ImportedFile struct {
	CommentsAttachment
	// The name as specified in the import statement.
	SpecifiedName string

	// The canonical file name of the imported file. This string is the unique identifier for the
	// corresponding MojomFile object. Note that when a .mojom file is first parsed
	// only the |SpecifiedFileName| of each of its imports is populated because we don't yet know the
	// canonical file names of the imported files. It is only when an imported
	// file itself is processed that |CanonicalFileName| field is populated within each of the
	// importing MojomFiles.
	CanonicalFileName string

	// NameToken is the StringLiteral token that contains the imported file name.
	NameToken lexer.Token
}

func (f *ImportedFile) String() string {
	return fmt.Sprintf("%q, ", f.SpecifiedName)
}

// NewImportedFile is a constructor for the ImportedFile type.
func NewImportedFile(specifiedName string, nameToken *lexer.Token) (importedFile *ImportedFile) {
	importedFile = new(ImportedFile)
	importedFile.SpecifiedName = specifiedName
	// This condition is there to make it easier to generate imported files in tests.
	if nameToken != nil {
		importedFile.NameToken = *nameToken
	}
	return
}

func (f *ImportedFile) MainToken() *lexer.Token {
	return &f.NameToken
}

func newMojomFile(fileName, specifiedName string, descriptor *MojomDescriptor,
	importedFrom *MojomFile, fileContents string) *MojomFile {
	mojomFile := new(MojomFile)
	mojomFile.CanonicalFileName = fileName
	mojomFile.Descriptor = descriptor
	mojomFile.ImportedFrom = importedFrom
	if importedFrom != nil {
		if importedFrom.Descriptor != descriptor {
			panic("A MojomFile may only be imported from another MojomFile with the same MojomDescriptor.")
		}
		if specifiedName != "" {
			panic(fmt.Sprintf("A MojomFile may not have both a non-empty specfiedName and a non-nil "+
				"|importedFrom|. specifiedName: %q, importedFrom: %q", specifiedName, importedFrom.CanonicalFileName))
		}
	} else {
		if specifiedName == "" {
			panic("A MojomFile must have either a non-empty specfiedName or a non-nil |importedFrom|.")
		}
	}
	mojomFile.SpecifiedFileName = specifiedName
	mojomFile.ModuleNamespace = nil
	mojomFile.Imports = make([]*ImportedFile, 0)
	mojomFile.importsBySpecifiedName = make(map[string]*ImportedFile)
	mojomFile.Interfaces = make([]*MojomInterface, 0)
	mojomFile.Structs = make([]*MojomStruct, 0)
	mojomFile.Unions = make([]*MojomUnion, 0)
	mojomFile.Enums = make([]*MojomEnum, 0)
	mojomFile.Constants = make([]*UserDefinedConstant, 0)
	mojomFile.fileContents = fileContents
	return mojomFile
}

// ImportedFromessage() returns a string that describes a chain of imports
// leading from |f| to a top-level file in the import file graph. It is intended
// to be used in user-facing messages in order to explain why a Mojom file
// is included in the import graph. The string will be of the form
// ... imported from foo.bar
// ... imported from baz.bing
// ... imported from fab.buzz
func (f *MojomFile) ImportedFromMessage() string {
	// TODO(rudominer) Consider making the value of 100 below overridable
	// by a user flag.
	return f.boundedImportedFromMessage(100)
}

// boundedImportedFromMessage() is a recursive helper function for
// |ImportedFromMessage| that ensures that the recursive chaining
// of "imported-from" links terminates within |numLevels| of recursion.
func (f *MojomFile) boundedImportedFromMessage(numLevels int) string {
	if f.ImportedFrom == nil || numLevels <= 0 {
		return ""
	}
	return fmt.Sprintf("\n... imported from %s.%s",
		RelPathIfShorter(f.ImportedFrom.CanonicalFileName),
		f.ImportedFrom.boundedImportedFromMessage(numLevels-1))
}

func (f *MojomFile) String() string {
	s := fmt.Sprintf("file name: %s\n", f.CanonicalFileName)
	s += fmt.Sprintf("module: %s\n", f.ModuleNamespace)
	s += fmt.Sprintf("attributes: %s\n", f.Attributes)
	s += fmt.Sprintf("imports: %s\n", f.Imports)
	s += fmt.Sprintf("scope: %s\n", f.FileScope)
	s += fmt.Sprintf("interfaces: %s\n", f.Interfaces)
	s += fmt.Sprintf("structs: %s\n", f.Structs)
	s += fmt.Sprintf("unions: %s\n", f.Unions)
	s += fmt.Sprintf("enums: %s\n", f.Enums)
	s += fmt.Sprintf("constants: %s\n", f.Constants)
	return s
}

// InitializeFileScope must be invoked before any of the Add*
// methods below may invoked. |moduleNamespace| should not be nil.
func (f *MojomFile) InitializeFileScope(moduleNamespace *ModuleNamespace) *Scope {
	f.ModuleNamespace = moduleNamespace
	f.FileScope = NewLexicalScope(ScopeFileModule, nil, moduleNamespace.Identifier, f, nil)
	return f.FileScope
}

func (f *MojomFile) AddImport(importedFile *ImportedFile) {
	f.Imports = append(f.Imports, importedFile)
	f.importsBySpecifiedName[importedFile.SpecifiedName] = importedFile
}

// SetCanonicalImportName sets the |CanonicalFileName| field of the |ImportedFile|
// with the given |specifiedName|. This method will usually be invoked later than
// the other methods in this file because it is only when the imported file itself
// is processed that we discover its canonical name.
func (f *MojomFile) SetCanonicalImportName(specifiedName, canoncialName string) {
	importedFile, ok := f.importsBySpecifiedName[specifiedName]
	if !ok {
		panic(fmt.Sprintf("There is no imported file with the specifiedName '%s'.", specifiedName))
	}
	importedFile.CanonicalFileName = canoncialName
}

func (f *MojomFile) AddInterface(mojomInterface *MojomInterface) DuplicateNameError {
	f.DeclaredObjects = append(f.DeclaredObjects, mojomInterface)
	f.Interfaces = append(f.Interfaces, mojomInterface)
	f.checkInit()
	return mojomInterface.RegisterInScope(f.FileScope)
}

func (f *MojomFile) AddStruct(mojomStruct *MojomStruct) DuplicateNameError {
	f.DeclaredObjects = append(f.DeclaredObjects, mojomStruct)
	f.Structs = append(f.Structs, mojomStruct)
	f.checkInit()
	return mojomStruct.RegisterInScope(f.FileScope)
}

func (f *MojomFile) AddEnum(mojomEnum *MojomEnum) DuplicateNameError {
	f.DeclaredObjects = append(f.DeclaredObjects, mojomEnum)
	f.Enums = append(f.Enums, mojomEnum)
	f.checkInit()
	return mojomEnum.RegisterInScope(f.FileScope)
}

func (f *MojomFile) AddUnion(mojomUnion *MojomUnion) DuplicateNameError {
	f.DeclaredObjects = append(f.DeclaredObjects, mojomUnion)
	f.Unions = append(f.Unions, mojomUnion)
	f.checkInit()
	return mojomUnion.RegisterInScope(f.FileScope)
}

func (f *MojomFile) AddConstant(declaredConst *UserDefinedConstant) DuplicateNameError {
	f.DeclaredObjects = append(f.DeclaredObjects, declaredConst)
	f.Constants = append(f.Constants, declaredConst)
	f.checkInit()
	return declaredConst.RegisterInScope(f.FileScope)
}

func (f *MojomFile) FileContents() string {
	return f.fileContents
}

func (f *MojomFile) checkInit() {
	if f.FileScope == nil {
		panic("InitializeFileScope must be invoked first.")
	}
}

//////////////////////////////////////////////////////////////////
/// type MojomDescriptor
/// //////////////////////////////////////////////////////////////

// A MojomDescriptor is the central object being populated by the frontend of
// the Mojom compiler. The same instance of MojomDescriptor is passed to each
// of the instances of Parser that are created by the ParseDriver while parsing
// a graph of Mojom files.  The output of ParserDriver.ParseFiles() is a
// ParseResult the main field of which is a MojomDescriptor. The MojomDescriptor
// is then serialized and passed to the backend of the Mojom compiler.
type MojomDescriptor struct {
	// All of the UserDefinedTypes keyed by type key
	TypesByKey map[string]UserDefinedType

	// All of the UserDefinedValues keyed by value key
	ValuesByKey map[string]UserDefinedValue

	// All of the MojomFiles in the order they were visited.
	mojomFiles []*MojomFile

	// All of the MojomFiles keyed by CanonicalFileName
	MojomFilesByName map[string]*MojomFile

	// The abstract module namespace scopes keyed by scope name. These are
	// the scopes that are not lexical scopes (i.e. files, structs, interfaces)
	// but rather the ancestor's of the file scopes that are implicit in the
	// dotted name of the file's module namespace. For example if a file's
	// module namespace is "foo.bar" then the chain of ancestors of the
	// file scope is as follows:
	// [file "foo.bar"] -> [module "foo.bar"] -> [module "foo"] -> [module ""]
	// The field |abstractScopesByName| will contain the last three of these
	// scopes but not the first. The last scope [module ""] is called the
	// global scope. The reason for both a [file "foo.bar"] and a
	// [module "foo.bar"] is that multiple files might have a module namespace
	// that is a descendent of [module "foo.bar"].
	abstractScopesByName map[string]*Scope

	// When new type and value references are encountered during parsing they
	// are added to these slices. After parsing completes the resolution
	// step attempts to resolve all of these references. If these slices are
	// not empty by the time ParserDriver.ParseFiles() completes it means that
	// there are unresolved references in the .mojom files.
	unresolvedTypeReferences  []*UserTypeRef
	unresolvedValueReferences []*UserValueRef

	// In testing mode we iterate through some maps in deterministic order.
	testingMode bool
}

func NewMojomDescriptor() *MojomDescriptor {
	descriptor := new(MojomDescriptor)

	descriptor.TypesByKey = make(map[string]UserDefinedType)
	descriptor.ValuesByKey = make(map[string]UserDefinedValue)
	descriptor.mojomFiles = make([]*MojomFile, 0)
	descriptor.MojomFilesByName = make(map[string]*MojomFile)
	descriptor.abstractScopesByName = make(map[string]*Scope)
	// The global namespace scope.
	descriptor.abstractScopesByName[""] = NewAbstractModuleScope("", descriptor)

	descriptor.unresolvedTypeReferences = make([]*UserTypeRef, 0)
	descriptor.unresolvedValueReferences = make([]*UserValueRef, 0)
	return descriptor
}

// SetTestingMode is used to enable or disable testing mode. It is disabled by
// default. In testing mode some operations will iterate through maps in a
// deterministic order. This is less performant but enables deterministic testing.
func (d *MojomDescriptor) SetTestingMode(testingMode bool) {
	d.testingMode = testingMode
}

func (d *MojomDescriptor) getAbstractModuleScope(fullyQualifiedName string) *Scope {
	if scope, ok := d.abstractScopesByName[fullyQualifiedName]; ok {
		return scope
	}
	scope := NewAbstractModuleScope(fullyQualifiedName, d)
	d.abstractScopesByName[fullyQualifiedName] = scope
	return scope
}

func (d *MojomDescriptor) getGlobalScobe() *Scope {
	return d.abstractScopesByName[""]
}

func (d *MojomDescriptor) AddMojomFile(fileName, specifiedName string, importedFrom *MojomFile, fileContents string) *MojomFile {
	mojomFile := newMojomFile(fileName, specifiedName, d, importedFrom, fileContents)
	mojomFile.Descriptor = d
	d.mojomFiles = append(d.mojomFiles, mojomFile)
	if _, ok := d.MojomFilesByName[mojomFile.CanonicalFileName]; ok {
		panic(fmt.Sprintf("The file %v has already been processed.", mojomFile.CanonicalFileName))
	}
	d.MojomFilesByName[mojomFile.CanonicalFileName] = mojomFile
	return mojomFile
}

func (d *MojomDescriptor) RegisterUnresolvedTypeReference(typeReference *UserTypeRef) {
	d.unresolvedTypeReferences = append(d.unresolvedTypeReferences, typeReference)
}

func (d *MojomDescriptor) RegisterUnresolvedValueReference(valueReference *UserValueRef) {
	d.unresolvedValueReferences = append(d.unresolvedValueReferences, valueReference)
}

func (d *MojomDescriptor) ContainsFile(fileName string) bool {
	_, ok := d.MojomFilesByName[fileName]
	return ok
}

// DetectIllFoundedTypes() should be invoked after Resolve() has completed
// successfully. It performs an analysis of the type graph in order to detect
// ill-founded types. An ill-founded type is one for which it is impossible
// to create an instance that would be legally serializable using Mojo
// serialization. This method returns a non-nil error just in case an
// ill-founded type is detected. The contained error message is appropriate
// for display to an end-user.
//
// An example of an ill-founded type is a struct |Foo| with a field whose type
// is a non-nullable Foo. Thus ill-foundedness may be due to a cycle in the
// type graph. But not all cycles cause ill-foundedness. Firstly nullable
// fields do not lead to ill-foundedness as the potential cycle can be broken
// by setting the field to null. Also the situation with unions is more complicated:
// Type graphs involving unions are only ill-founded if every possible way of
// chosing a value for all of the unions still leads to an unbreakable cycle.
func (d *MojomDescriptor) DetectIllFoundedTypes() error {
	// We capture the keys of the map TypesByKey in a list and iterate over that list
	// rather than iterating over the map. This is so that in testing mode
	// we can first sort the list so as to iterate in a deterministic order.
	keyList := make([]string, 0, len(d.TypesByKey))
	for key, _ := range d.TypesByKey {
		keyList = append(keyList, key)
	}
	if d.testingMode {
		sort.Strings(keyList)
	}
	for _, key := range keyList {
		udt := d.TypesByKey[key]
		if err := udt.CheckWellFounded(); err != nil {
			return err
		}
	}
	return nil
}

/////////////////////////////////////////
/// Type and Value Resolution
////////////////////////////////////////

// Resolve() should be invoked after all of the parsing has been done. It
// attempts to resolve all of the entries in |d.unresolvedTypeReferences| and
// |d.unresolvedValueReferences|. Returns a non-nil error if there are any
// remaining unresolved references or if after resolution it was discovered
// that a type or value was used in an inappropriate way.
func (d *MojomDescriptor) Resolve() error {
	// Resolve the types
	unresolvedTypeReferences, err := d.resolveTypeReferences()
	if err != nil {
		// For one of the type references, we discovered after resolution that
		// the resolved type was used in an inappropriate way.
		return err
	}
	numUnresolvedTypeReferences := len(unresolvedTypeReferences)

	// Resolve the values
	unresolvedValueReferences, err := d.resolveValueReferences()
	if err != nil {
		// For one of the value references, we discovered after resolution that
		// the resolved value was used in an inappropriate way.
		return err
	}
	numUnresolvedValueReferences := len(unresolvedValueReferences)
	// Because values may be defined in terms of user-defined constants which
	// may themselves be defined in terms of other user-defined constants,
	// we may have to perform the value resolution step multiple times in
	// order to propogage concrete values to all declarations. To make sure that
	// this process terminates we keep iterating only as long as the number
	// of unresolved value references decreases.
	for numUnresolvedValueReferences > 0 {
		unresolvedValueReferences, _ = d.resolveValueReferences()
		if len(unresolvedValueReferences) < numUnresolvedValueReferences {
			numUnresolvedValueReferences = len(unresolvedValueReferences)
		} else {
			break
		}
	}

	d.unresolvedTypeReferences = unresolvedTypeReferences[0:numUnresolvedTypeReferences]
	d.unresolvedValueReferences = unresolvedValueReferences[0:numUnresolvedValueReferences]

	if numUnresolvedTypeReferences+numUnresolvedValueReferences == 0 {
		return nil
	}

	var messageBuffer bytes.Buffer
	for _, ref := range d.unresolvedTypeReferences {
		message := fmt.Sprintf("Undefined type: %q", ref.Identifier())
		messageBuffer.WriteString(UserErrorMessage(ref.scope.file, ref.token, message))
	}
	for _, ref := range d.unresolvedValueReferences {
		var message string
		if ref.ResolvedDeclaredValue() == nil {
			message = fmt.Sprintf("Undefined value: %q", ref.Identifier())
		} else if ref.ResolvedConcreteValue() == nil {
			message = fmt.Sprintf("Use of unresolved value: %q", ref.Identifier())
		} else {
			panic("Internal error.")
		}
		messageBuffer.WriteString(UserErrorMessage(ref.scope.file, ref.token, message))
	}

	return fmt.Errorf(messageBuffer.String())
}

func (d *MojomDescriptor) resolveTypeReferences() (unresolvedReferences []*UserTypeRef, postResolutionValidationError error) {
	unresolvedReferences = make([]*UserTypeRef, len(d.unresolvedTypeReferences))
	numUnresolved := 0
	for _, ref := range d.unresolvedTypeReferences {
		if ref != nil {
			if err := d.resolveTypeRef(ref); err != nil {
				if err.notResolved {
					unresolvedReferences[numUnresolved] = ref
					numUnresolved++
				} else {
					postResolutionValidationError = err
					return
				}
			} else {
				if postResolutionValidationError = ref.validateAfterResolution(); postResolutionValidationError != nil {
					break
				}
			}
		}
	}
	unresolvedReferences = unresolvedReferences[0:numUnresolved]
	return
}

func (d *MojomDescriptor) resolveValueReferences() (unresolvedReferences []*UserValueRef, postResolutionValidationError error) {
	unresolvedReferences = make([]*UserValueRef, len(d.unresolvedValueReferences))
	numUnresolved := 0
	for _, ref := range d.unresolvedValueReferences {
		if ref != nil {
			if err := d.resolveValueRef(ref); err != nil {
				if err.notResolved {
					unresolvedReferences[numUnresolved] = ref
					numUnresolved++
				} else {
					postResolutionValidationError = err
					return
				}
			} else {
				if postResolutionValidationError = ref.validateAfterResolution(); postResolutionValidationError != nil {
					break
				}
			}
		}
	}
	unresolvedReferences = unresolvedReferences[0:numUnresolved]
	return
}

// resolveReferenceError is a type of error returned by resolveTypeRef()
// and resolveValueRef() when either the given reference could not be resolved
// or else it is resolved but to an object of the wrong kind.
type resolveReferenceError struct {
	// Is the problem that the identifier could not be resolved?
	notResolved bool
	// This will contain a user-facing error message if and only if
	// notResolved is false. This means that the identifier did resolve to
	// some object but it was not of the expected kind.
	message string
}

func (e *resolveReferenceError) Error() string {
	return e.message
}

// resolveTypeRef attempts to resolve the given UserTypeRef. Returns nil on success and in
// that case the |resolvedType| field of |ref| has been set. Otherwise
// returns a |resolvedReferenceError| indicating either that the name could
// not be resolved at all, or else containing a user-facing error message
// explaining that the name resolved to something other than a type.
func (d *MojomDescriptor) resolveTypeRef(ref *UserTypeRef) *resolveReferenceError {
	// Our lookup algorithm is: Search for any declared object regardless of kind
	// and emit an error if the kind of object found is not a type.
	lookupResult := ref.scope.LookupObject(ref.identifier, LookupAcceptAll)
	if lookupResult == nil {
		// The name could not be resolved at all.
		return &resolveReferenceError{notResolved: true}
	}
	switch lookupResult := lookupResult.(type) {
	case UserDefinedType:
		ref.resolvedType = lookupResult
		// The name resolved to a type. Success!
		return nil
	}
	// The name resolved to something other than a type.
	message := fmt.Sprintf("%q does not refer to a type. It refers to the %s %s at %s.",
		ref.identifier, lookupResult.KindString(), lookupResult.FullyQualifiedName(),
		FullLocationString(lookupResult))
	message = UserErrorMessage(ref.scope.file, ref.token, message)
	return &resolveReferenceError{message: message}
}

// resolveValueRef attempts to resolve the given UserValueRef. Returns nil on success and in
// that case the |resolvedDeclaredValue| and |resolvedConcreteValue| fields of
// |ref| have been set. Otherwise returns a |resolvedReferenceError| indicating
// either that the reference could not be fully resolved to a concrete value,
// or else containing a user-facing error message explaining that the name
// resolved to something other than a value.
//
// There are two steps to fully resolving a value. First resolve the identifier
// to to a target declaration, then resolve the target declaration to a
// concrte value. If either of these steps fail then a resolvedReferenceError
// with |notResolved| = true will be returned.
func (d *MojomDescriptor) resolveValueRef(ref *UserValueRef) *resolveReferenceError {
	// Step 1: Find resolvedDeclaredValue
	if ref.resolvedDeclaredValue == nil {

		// Our lookup algorithm is: Search for any declared object regardless of kind
		// and emit an error if the kind of object found is not a value.
		if lookupResult := ref.scope.LookupObject(ref.identifier, LookupAcceptAll); lookupResult != nil {
			switch lookupResult := lookupResult.(type) {
			case UserDefinedValue:
				ref.resolvedDeclaredValue = lookupResult
			default:
				// The name resolved to something other than a value.
				message := fmt.Sprintf("%q does not refer to a value. It refers to the %s %s at %s.",
					ref.identifier, lookupResult.KindString(), lookupResult.FullyQualifiedName(),
					FullLocationString(lookupResult))
				message = UserErrorMessage(ref.scope.file, ref.token, message)
				return &resolveReferenceError{message: message}
			}
		}

		// If we have not resolved the identifier yet, maybe it uses a special
		// convention for referring to an enum value.
		if ref.resolvedDeclaredValue == nil && resolveSpecialEnumValueAssignment(ref) {
			return nil
		}

		// If we have not resolved the identifier yet, maybe it refers to a built-in constant.
		if ref.resolvedDeclaredValue == nil {
			builtInValue, ok := LookupBuiltInConstantValue(ref.identifier)
			if ok {
				ref.resolvedDeclaredValue = builtInValue
			}
		}

		// If we have not resolved the identifier yet, return an error.
		if ref.resolvedDeclaredValue == nil {
			return &resolveReferenceError{notResolved: true}
		}
	}

	// Step 2: Find resolvedConcreteValue.
	switch value := ref.resolvedDeclaredValue.(type) {
	case *UserDefinedConstant:
		// The identifier resolved to a user-declared constant. We use
		// the (possibly nil) resolved value of that constant as
		// resolvedConcreteValue. Since this may be nil it is possible that
		// ref is still unresolved.
		ref.resolvedConcreteValue = value.valueRef.ResolvedConcreteValue()
	case *EnumValue:
		// The identifier resolved to an enum value. We use the enum value
		// itself (not the integer value of the enum value) as the
		// resolvedConcreteValue. Since this cannot be nil we know that
		// ref is now fully resolved.
		ref.resolvedConcreteValue = value
	case BuiltInConstantValue:
		ref.resolvedConcreteValue = value
	default:
		panic(fmt.Sprintf("ref.resolvedDeclaredValue is neither a DelcaredConstant, an EnumValue, "+
			"nor a BuiltInConstantValue: %T", ref.resolvedDeclaredValue))
	}

	// If we have still not resolved the concrete value, return an error indicating so.
	// We may try again in a later pass.
	if ref.resolvedConcreteValue == nil {
		return &resolveReferenceError{notResolved: true}
	}

	// success!
	return nil
}

// resolveSpecialEnumValueAssignment handles a special case of value lookup:
// There is an assignment being made where the variable on the left-hand-side
// is of an enum type and the identifier on the right-hand-side is a simple name.
//
// enum Color {
//	RED, BLUE
// };
// const Color my_favorite_color = BLUE;
//
// In the above example the simple name "BLUE" is being assigned to a variable
// of enum type Color. In this case our lookup procedure includes a
// step in which we search for a value named "BLUE" in the enum type
// Color.
func resolveSpecialEnumValueAssignment(ref *UserValueRef) bool {
	// If the reference is not a simple name, return false.
	name := ref.identifier
	if strings.ContainsRune(name, '.') {
		return false
	}

	// If the assignee type is not some user-defined type, return false.
	assigneeType := ref.assigneeSpec.Type
	if assigneeType == nil || assigneeType.TypeRefKind() != TypeKindUserDefined {
		return false
	}

	// If the assignee type is not an enum type, return false.
	userTypeRef, ok := assigneeType.(*UserTypeRef)
	if !ok {
		panic(fmt.Sprintf("Type of assigneeType is %T", assigneeType))
	}
	if userTypeRef.ResolvedType() == nil || userTypeRef.ResolvedType().Kind() != UserDefinedTypeKindEnum {
		return false
	}
	assigneeEnumType := userTypeRef.ResolvedType()

	// Attempt to lookup a value using the fully qualified name formed by concatenating the
	// fully-qualified name of the enum type with the simple name.
	name = userTypeRef.ResolvedType().FullyQualifiedName() + "." + name
	lookupResult := ref.scope.LookupObject(name, LookupAcceptValue)
	if lookupResult == nil {
		// That name could not be resolved at all.
		return false
	}

	// The lookupResult must be a UserDefinedValue because we passed the
	// flag LookupAcceptValue.
	userDefinedValue, ok := lookupResult.(UserDefinedValue)
	if !ok {
		panic(fmt.Sprintf("lookupResult is not a UserDefinedValue: %v", lookupResult))
	}

	switch enumValue := userDefinedValue.(type) {
	case *EnumValue:
		if enumValue.EnumType() == assigneeEnumType {
			// The name we manufactured corresponds to an enum value from the right enum. Success!
			ref.resolvedDeclaredValue = userDefinedValue
			ref.resolvedConcreteValue = enumValue
			return true
		}
	default:
	}
	return false
}

// ModuleNamespace represents the identifier declared via the "module"
// declaration in the .mojom file.
type ModuleNamespace struct {
	CommentsAttachment
	// Identifier is the name of the namespace. The identifier from the "module" declaration.
	Identifier string
	// Token is the token from which the Identifier was extracted.
	Token *lexer.Token
}

// NewModuleNamespace creates a module namespace and returns a pointer to it.
func NewModuleNamespace(identifier string, token *lexer.Token) (moduleNamespace *ModuleNamespace) {
	moduleNamespace = new(ModuleNamespace)
	moduleNamespace.Identifier = identifier
	moduleNamespace.Token = token
	return
}

func (ns *ModuleNamespace) MainToken() *lexer.Token {
	return ns.Token
}

func (ns *ModuleNamespace) String() string {
	return ns.Identifier
}

//////////////////////////////////////
// Debug printing of a MojomDescriptor
//////////////////////////////////////

func (d *MojomDescriptor) debugPrintMojomFiles() (s string) {
	for _, f := range d.mojomFiles {
		s += fmt.Sprintf("\n%s\n", f)
	}
	return
}

func (d *MojomDescriptor) debugPrintUnresolvedTypeReference() (s string) {
	for _, r := range d.unresolvedTypeReferences {
		s += fmt.Sprintf("%s\n", r.LongString())
	}
	return
}

func debugPrintTypeMap(m map[string]UserDefinedType) (s string) {
	for key, value := range m {
		s += fmt.Sprintf("%s : %s %s\n", key, value.SimpleName(), value.Kind())
	}
	return
}

func debugPrintValueMap(m map[string]UserDefinedValue) (s string) {
	for key, value := range m {
		s += fmt.Sprintf("%s : %s\n", key, value.SimpleName())
	}
	return
}

func (d *MojomDescriptor) String() string {
	message :=
		`
TypesByKey:
----------------
%s
ValuesByKey:
----------------
%s
Files:
---------------
%s
`
	return fmt.Sprintf(message, debugPrintTypeMap(d.TypesByKey), debugPrintValueMap(d.ValuesByKey), d.debugPrintMojomFiles())
}

///////////////////////////////////////////////////////////////////////
/// Miscelleneous utilities
/// //////////////////////////////////////////////////////////////////

func ComputeTypeKey(fullyQualifiedName string) (typeKey string) {
	if typeKey, ok := fqnToTypeKey[fullyQualifiedName]; ok == true {
		return typeKey
	}
	// TODO(rudominer) What are the requirements for a type key?
	// Until we understand better what the requirements are for a type key
	// let's just use the fully-qualified name itself, with a prefix prepended, as the type key.
	// The reason for the prefix is pragmatic: When debugging we can tell whether a string
	// is a type key or a fully-qualified-name.
	typeKey = fmt.Sprintf("TYPE_KEY:%s", fullyQualifiedName)
	fqnToTypeKey[fullyQualifiedName] = typeKey
	return
}

var fqnToTypeKey map[string]string

func init() {
	fqnToTypeKey = make(map[string]string)
}
