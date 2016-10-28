// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package core

import (
	"container/list"
	"errors"
	"fidl/compiler/lexer"
	"fidl/compiler/utils"
	"fmt"
)

// ComputeFinalData() should be invoked after Resolve() has completed
// successfully. It performs the final computations needed in order to
// populate all of the fields in the intermediate representation that is
// the output of the frontend of the compiler and is consumed by the backend.
// An example is the field packing and version data for struct fields that will
// be used by the code generators.
func (d *MojomDescriptor) ComputeFinalData() error {
	for _, userDefinedType := range d.TypesByKey {
		if err := userDefinedType.ComputeFinalData(); err != nil {
			return err
		}
	}
	return nil
}

////////////////////////  Structs  ////////////////////////

func (e *MojomStruct) ComputeFinalData() error {
	// Note(rudominer) computeFieldOffsets must be invoked before computeVersionInfo.
	if err := e.computeFieldOffsets(); err != nil {
		return err
	}
	if err := e.computeVersionInfo(); err != nil {
		return err
	}
	return nil
}

// computePadding returns the least non-negative integer |pad| such that
// |offset| + |pad| is a multiple of |alignment|.
func computePadding(offset, alignment uint32) uint32 {
	return (alignment - (offset % alignment)) % alignment
}

// computeFieldOffsets is invoked by ComputeFinalData after the
// parsing, resolution and type validation phases. It computes the |offset|
// and |bit| fields of each struct field. This function must be invoked
// before computeVersionInfo() because computeVersionInfo() consumes the
// |offset| and |size| fields that are computed by this method.
//
// The goal is to pack each field into the earliest possible position in the serialized
// struct, subject to the alignment and size constraints of its type.
// Additionally booleans are represented as a single bit. See go/mojo-archive-format.
//
// The algorithm is as follows: We iterate over |fieldsInOrdinalOrder|, building up
// |fieldsInPackingOrder|, and setting |field.offset| and |field.bit| as we go.
// |fieldsInPackingOrder| represents the fields whose serialization position has
// already been determined. For each |field| in |fieldsInOrdinalOrder| we search
// through |fieldsInPackingOrder| looking for a position in which |field| will fit,
// and we insert it into the first position we find where it will fit.
func (s *MojomStruct) computeFieldOffsets() error {
	fieldsInPackingOrder := list.New()
LoopOverFields:
	for i, field := range s.fieldsInOrdinalOrder {
		if i == 0 {
			// Field zero always goes first in packing order.
			field.offset = 0
			field.bit = 0
			fieldsInPackingOrder.PushBack(field)
			continue
		}

		// Search through |fieldsInPackingOrder| for two consecutive fields,
		// |before| and |after|, with enough space between them that we may
		// insert |field| between |before| and |after|.
		before := fieldsInPackingOrder.Front()
		if before.Next() != nil {
			for after := before.Next(); after != nil; after = after.Next() {
				// Tentatively assume that |field| will fit after |before|
				computeTentativeFieldOffset(field, before.Value.(*StructField))
				// Check if it actually fits before |after|.
				if field.Offset()+field.FieldType.SerializationSize() <= after.Value.(*StructField).Offset() {
					// It fits: insert it.
					fieldsInPackingOrder.InsertAfter(field, before)
					// We are finished processing this |field|.
					continue LoopOverFields
				}
				// It didn't fit: continue to search for a spot to fit it.
				before = after
			}
		}
		// If we are here then we did not find a hole and so |field| should be inserted
		// at the end.
		computeTentativeFieldOffset(field, before.Value.(*StructField))
		fieldsInPackingOrder.PushBack(field)
	}
	return nil
}

// computeTentativeFieldOffset computes and sets the |offset| and |bit| fields
// of |field|, assuming it will fit right after |previousField| in packing
// order. The setting is only tentative because it may turn out it does not fit
// before the field that is currently following |previousField| in packing order,
// in which case |field| needs to be moved forward to a different location.
// The |offset| and |bit| fields of |previousField| must already be set.
func computeTentativeFieldOffset(field, previousField *StructField) {
	if isBoolField(field) && isBoolField(previousField) && previousField.bit < 7 {
		field.offset = int64(previousField.Offset())
		field.bit = previousField.bit + 1
		return
	}
	offset := previousField.Offset() + previousField.FieldType.SerializationSize()
	alignment := field.FieldType.SerializationAlignment()
	field.offset = int64(offset + computePadding(offset, alignment))
	field.bit = 0
}

func isBoolField(field *StructField) bool {
	simpleType, ok := field.FieldType.(SimpleType)
	if ok {
		return simpleType == SimpleTypeBool
	}
	return false
}

var ErrMinVersionIllformed = errors.New("MinVersion attribute value illformed")
var ErrMinVersionOutOfOrder = errors.New("MinVersion attribute value out of order")
var ErrMinVersionNotNullable = errors.New("Non-Zero MinVersion attribute value on non-nullable field")

type StructFieldMinVersionError struct {
	// The containing struct
	containingStruct *MojomStruct

	// The field whose MinVersion is being set.
	field *StructField

	// The MinVersion of the previous field. Only used for ErrMinVersionOutOfOrder
	previousValue uint32

	// The LiteralValue of the attribute assignment.
	// NOTE: We use the following convention: literalValue.token == nil indicates that
	// there was no MinVersion attribute on the given field. This can only happen with
	// ErrMinVersionOutOfOrder
	literalValue LiteralValue

	// The type of error (ErrMinVersionIllfromed, ErrMinVersionOutOfOrder, ErrMinVersionNotNullable)
	err error
}

// StructFieldMinVersionError implements error.
func (e *StructFieldMinVersionError) Error() string {
	var message string
	var token lexer.Token
	fieldType := "field"
	switch e.containingStruct.structType {
	case StructTypeSyntheticRequest:
		fieldType = "parameter"
	case StructTypeSyntheticResponse:
		fieldType = "response parameter"
	}
	switch e.err {
	case ErrMinVersionIllformed:
		message = fmt.Sprintf("Invalid MinVersion attribute for %s %s: %s. "+
			"The value must be a non-negative 32-bit integer value.",
			fieldType, e.field.SimpleName(), e.literalValue)
		token = *e.literalValue.token
	case ErrMinVersionOutOfOrder:
		if e.literalValue.token == nil {
			message = fmt.Sprintf("Invalid missing MinVersion for %s %s. "+
				"The MinVersion must be non-decreasing as a function of the ordinal. "+
				"This %s must have a MinVersion attribute with a value at least %d.",
				fieldType, e.field.SimpleName(), fieldType, e.previousValue)
			token = e.field.NameToken()
		} else {
			message = fmt.Sprintf("Invalid MinVersion attribute for %s %s: %s. "+
				"The MinVersion must be non-decreasing as a function of the ordinal. "+
				"This %s's MinVersion must be at least %d.",
				fieldType, e.field.SimpleName(), e.literalValue.token.Text, fieldType, e.previousValue)
			token = *e.literalValue.token
		}
	case ErrMinVersionNotNullable:
		message = fmt.Sprintf("Invalid type for %s %s: %s. "+
			"Non-nullable reference %ss are only allowed in version 0 of of a struct. "+
			"This %s's MinVersion is %s.",
			fieldType, e.field.SimpleName(), e.field.FieldType.TypeName(), fieldType, fieldType, e.literalValue.token.Text)
		switch fieldType := e.field.FieldType.(type) {
		case *UserTypeRef:
			token = fieldType.token
		default:
			// It would be nice for the green carets in the snippit in the error message to point at
			// the type name, but other than for user type refs we don't store that token so
			// instead we use the field's name.
			token = e.field.NameToken()
		}
	}
	return UserErrorMessage(e.field.OwningFile(), token, message)
}

// computeVersionInfo is invoked by ComputeFinalData() after the
// computeFieldOffsets(). It examines the |MinVersion|
// attributes of all of the fields of the struct, validates the values, and
// sets up the versionInfo array.
//
// Note that computeFieldOffsets() must be invoked prior to this function becuase
// this function consumes the |offset| fields of each field. The accessor
// StructField.Offset() will panic if computeFieldOffsets() has not yet been invoked.
func (s *MojomStruct) computeVersionInfo() error {
	s.versionInfo = make([]StructVersion, 0)
	previousMinVersion := uint32(0)
	payloadSizeSoFar := structHeaderSize
	for i, field := range s.fieldsInOrdinalOrder {
		value, literalValue, found, ok := field.minVersionAttribute()
		if found == false {
			if previousMinVersion != 0 {
				return &StructFieldMinVersionError{
					containingStruct: s,
					field:            field,
					previousValue:    previousMinVersion,
					literalValue:     MakeStringLiteralValue("", nil),
					err:              ErrMinVersionOutOfOrder,
				}
			}
		} else {
			if !ok {
				return &StructFieldMinVersionError{
					containingStruct: s,
					field:            field,
					literalValue:     literalValue,
					err:              ErrMinVersionIllformed,
				}
			}
			if value < previousMinVersion {
				return &StructFieldMinVersionError{
					containingStruct: s,
					field:            field,
					previousValue:    previousMinVersion,
					literalValue:     literalValue,
					err:              ErrMinVersionOutOfOrder,
				}
			}
		}
		if value != 0 && !field.FieldType.AllowedInNonZeroStructVersion() {
			return &StructFieldMinVersionError{
				containingStruct: s,
				field:            field,
				literalValue:     literalValue,
				err:              ErrMinVersionNotNullable,
			}
		}
		field.minVersion = int64(value)
		if value > previousMinVersion {
			s.versionInfo = append(s.versionInfo, StructVersion{
				VersionNumber: previousMinVersion,
				NumFields:     uint32(i),
				NumBytes:      payloadSizeSoFar,
			})
			previousMinVersion = value
		}
		// Taking a max here is necessary since the ordinal order is not the same as packing order.
		payloadSizeSoFar = utils.MaximumUint32(payloadSizeSoFar, computePayloadSizeSoFar(field))
	}
	s.versionInfo = append(s.versionInfo, StructVersion{
		VersionNumber: previousMinVersion,
		NumFields:     uint32(len(s.fieldsInOrdinalOrder)),
		NumBytes:      payloadSizeSoFar,
	})

	return nil
}

const structHeaderSize = uint32(8)

func computePayloadSizeSoFar(field *StructField) uint32 {
	fieldEndOffset := field.Offset() + field.FieldType.SerializationSize()
	return structHeaderSize + fieldEndOffset + computePadding(fieldEndOffset, uint32(8))
}

////////////////////////  Interfaces  ////////////////////////

func (intrfc *MojomInterface) ComputeFinalData() error {
	for _, method := range intrfc.MethodsByOrdinal {
		if method.Parameters != nil {
			if err := method.Parameters.ComputeFinalData(); err != nil {
				return err
			}
		}
		if method.ResponseParameters != nil {
			if err := method.ResponseParameters.ComputeFinalData(); err != nil {
				return err
			}
		}
	}
	return intrfc.computeInterfaceVersion()
}

type MethodMinVersionError struct {
	// The method whose MinVersion is being set.
	method *MojomMethod

	// The MinVersion of the previous method. Only used for ErrMinVersionOutOfOrder
	previousValue uint32

	// The LiteralValue of the attribute assignment.
	// NOTE: We use the following convention: literalValue.token == nil indicates that
	// there was no MinVersion attribute on the given method. This can only happen with
	// ErrMinVersionOutOfOrder
	literalValue LiteralValue

	// The type of error (ErrMinVersionIllfromed, ErrMinVersionOutOfOrder, ErrMinVersionNotNullable)
	err error
}

// MethodMinVersionError implements error.
func (e *MethodMinVersionError) Error() string {
	var message string
	var token lexer.Token
	switch e.err {
	case ErrMinVersionIllformed:
		message = fmt.Sprintf("Invalid MinVersion attribute for method %s: %s. "+
			"The value must be a non-negative 32-bit integer value.",
			e.method.SimpleName(), e.literalValue)
		token = *e.literalValue.token
	case ErrMinVersionOutOfOrder:
		if e.literalValue.token == nil {
			message = fmt.Sprintf("Invalid missing MinVersion for method %s. "+
				"The MinVersion must be non-decreasing as a function of the ordinal. "+
				"This method must have a MinVersion attribute with a value at least %d.",
				e.method.SimpleName(), e.previousValue)
			token = e.method.NameToken()
		} else {
			message = fmt.Sprintf("Invalid MinVersion attribute for method %s: %s. "+
				"The MinVersion must be non-decreasing as a function of the ordinal. "+
				"This method's MinVersion must be at least %d.",
				e.method.SimpleName(), e.literalValue.token.Text, e.previousValue)
			token = *e.literalValue.token
		}
	default:
		panic("Unexpected type of MethodMinVersionError")
	}
	return UserErrorMessage(e.method.OwningFile(), token, message)
}

// computeInterfaceVersion computes and sets the |minVersion| field of each
// of each method in |intrfc| and the |versionNumber| field of |intrfc| itself.
func (intrfc *MojomInterface) computeInterfaceVersion() error {
	previousMinVersion := uint32(0)
	intrfc.versionNumber = 0
	// Iterate through the methods in ordinal order.
	for _, method := range intrfc.MethodsInOrdinalOrder() {
		// Look for |MinVersion| attributes and validate them.
		value, literalValue, found, ok := method.minVersionAttribute()
		if found == false {
			if previousMinVersion != 0 {
				return &MethodMinVersionError{
					method:        method,
					previousValue: previousMinVersion,
					literalValue:  MakeStringLiteralValue("", nil),
					err:           ErrMinVersionOutOfOrder,
				}
			}
		} else {
			if !ok {
				return &MethodMinVersionError{
					method:       method,
					literalValue: literalValue,
					err:          ErrMinVersionIllformed,
				}
			}
			if value < previousMinVersion {
				return &MethodMinVersionError{
					method:        method,
					previousValue: previousMinVersion,
					literalValue:  literalValue,
					err:           ErrMinVersionOutOfOrder,
				}
			}
		}
		method.minVersion = int64(value)
		previousMinVersion = value
		// The |versionNumber| for the interface is at least as big as the
		// |minVersions| of each of its methods.
		if method.minVersion > intrfc.versionNumber {
			intrfc.versionNumber = method.minVersion
		}
		// The |versionNumber| for the interface is at least as big as the
		// greatest version number of its parameter structs.
		if method.Parameters != nil {
			versions := method.Parameters.VersionInfo()
			paramsMaxVersion := uint32(versions[len(versions)-1].VersionNumber)
			if paramsMaxVersion > intrfc.Version() {
				intrfc.versionNumber = int64(paramsMaxVersion)
			}
		}
		if method.ResponseParameters != nil {
			versions := method.ResponseParameters.VersionInfo()
			paramsMaxVersion := uint32(versions[len(versions)-1].VersionNumber)
			if paramsMaxVersion > intrfc.Version() {
				intrfc.versionNumber = int64(paramsMaxVersion)
			}
		}
	}
	return nil
}

////////////////////////  Unions  ////////////////////////

func (u *MojomUnion) ComputeFinalData() error {
	return nil
}

////////////////////////  Enums  ////////////////////////
func (e *MojomEnum) ComputeFinalData() error {
	return e.ComputeEnumValueIntegers()
}

// ComputeEnumValueIntegers() computes the |ComputedIntValue| field of all
// the values in |enum|.
func (enum *MojomEnum) ComputeEnumValueIntegers() error {
	previousValue := int32(-1)
	for _, enumValue := range enum.Values {
		if enumValue.ValueRef() == nil {
			previousValue++
		} else {
			value, err := int32EnumValueFromValue(enum, enumValue.ValueRef())
			if err != nil {
				return err
			}
			previousValue = value
		}
		enumValue.ComputedIntValue = previousValue
		enumValue.IntValueComputed = true
	}
	return nil
}

// int32EnumValueFromValue() extracts an int32 value from |valueRef| to be used to set
// the |ComputedIntValue| field of an EnumValue from |enum|. It is assumed
// that |valueRef| is of one of three types:
// (i) A LiteralValue that contains an integer value that can be represented as an int32
// (ii) A UserValueRef whose resolved concrete value is as in (i)
// (iii) A UserValueRef whose resolved concrete value is an EnumValue
//   (*) in which |IntValueComputed| is true.
//
// It is the responsibility of the parsing and resolution phases to ensure that all EnumValues
// satisfy (i), (ii) or (iii) (excluding (*)). This function will panic otherwise.
// In case (iii)(*) is not true this function will not panic but will return a
// non-nil error.
//
// Because of case (ii) an EnumValue is allowed to be initialized to a user defined constant
// of integer type.
//
// Because of case (iii) an EnumValue is allowed to be initialized to a different EnumValue
// but because of (iii)(*) that EnumValue must have had its |ComputedIntValue| computed
// prior to the current attempt. In practice this means it is safe for an EnumValue
// to be initialized in terms of an earlier EnumValue from the same Enum. Future enhancements
// may allow more general patterns.
func int32EnumValueFromValue(enum *MojomEnum, valueRef ValueRef) (int32, error) {
	var int32EnumValue int32
	switch specifiedValue := valueRef.(type) {
	case LiteralValue:
		value, ok := int32Value(specifiedValue)
		if !ok {
			// Panic because this was supposed to be caught by an earlier layer.
			panic(fmt.Sprintf("Illegal literal value '%v' in initializer for enum %v",
				specifiedValue.Value(), enum))
		}
		int32EnumValue = value
	case *UserValueRef:
		if specifiedValue.resolvedConcreteValue == nil {
			// Panic because this was supposed to be caught by an earlier layer.
			panic(fmt.Sprintf("Unresolved value reference %v in initializer for enum %v",
				specifiedValue, enum))
		}
		switch resolvedConcreteValue := specifiedValue.resolvedConcreteValue.(type) {
		case LiteralValue:
			value, ok := int32Value(resolvedConcreteValue)
			if !ok {
				// Panic because this was supposed to be caught by an earlier layer.
				panic(fmt.Sprintf("Illegal literal value '%v' as concrete value of %v in initializer for enum %v",
					resolvedConcreteValue, specifiedValue, enum))
			}
			int32EnumValue = value
		case *EnumValue:
			if resolvedConcreteValue.IntValueComputed {
				int32EnumValue = resolvedConcreteValue.ComputedIntValue
			} else {
				// TODO(rudominer) Allow enum values to be initialized to other enum values as long
				// as the assignment chain is well-founded.
				message := fmt.Sprintf("The reference %s is being used as an enum value initializer but it has resolved to a "+
					"different enum value that itself does not yet have an integer value.", specifiedValue.identifier)
				message = UserErrorMessage(specifiedValue.scope.file, specifiedValue.token, message)
				return 0, fmt.Errorf(message)

			}
		default:
			panic(fmt.Sprintf("Unexpected resolvedConcreteValue type %T", resolvedConcreteValue))
		}

	default:
		panic(fmt.Sprintf("Unexpected ValueRef type %T", specifiedValue))
	}
	return int32EnumValue, nil
}
