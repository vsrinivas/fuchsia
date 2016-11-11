package fidl_types

import (
	bindings "fidl/bindings"
	fmt "fmt"
	sort "sort"
)

type StringType struct {
	Nullable bool
}

func (s *StringType) Encode(encoder *bindings.Encoder) error {
	encoder.StartStruct(8, 0)
	if err := encoder.WriteBool(s.Nullable); err != nil {
		return err
	}

	if err := encoder.Finish(); err != nil {
		return err
	}
	return nil
}

var stringType_Versions []bindings.DataHeader = []bindings.DataHeader{
	bindings.DataHeader{16, 0},
}

func (s *StringType) Decode(decoder *bindings.Decoder) error {
	header, err := decoder.StartStruct()
	if err != nil {
		return err
	}

	index := sort.Search(len(stringType_Versions), func(i int) bool {
		return stringType_Versions[i].ElementsOrVersion >= header.ElementsOrVersion
	})
	if index < len(stringType_Versions) {
		if stringType_Versions[index].ElementsOrVersion > header.ElementsOrVersion {
			index--
		}
		expectedSize := stringType_Versions[index].Size
		if expectedSize != header.Size {
			return &bindings.ValidationError{bindings.UnexpectedStructHeader,
				fmt.Sprintf("invalid struct header size: should be %d, but was %d", expectedSize, header.Size),
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		value, err := decoder.ReadBool()
		if err != nil {
			return err
		}
		s.Nullable = value
	}

	if err := decoder.Finish(); err != nil {
		return err
	}
	return nil
}

type HandleType struct {
	Nullable bool
	Kind     HandleType_Kind
}

func (s *HandleType) Encode(encoder *bindings.Encoder) error {
	encoder.StartStruct(8, 0)
	if err := encoder.WriteBool(s.Nullable); err != nil {
		return err
	}
	if err := encoder.WriteInt32(int32(s.Kind)); err != nil {
		return err
	}

	if err := encoder.Finish(); err != nil {
		return err
	}
	return nil
}

var handleType_Versions []bindings.DataHeader = []bindings.DataHeader{
	bindings.DataHeader{16, 0},
}

func (s *HandleType) Decode(decoder *bindings.Decoder) error {
	header, err := decoder.StartStruct()
	if err != nil {
		return err
	}

	index := sort.Search(len(handleType_Versions), func(i int) bool {
		return handleType_Versions[i].ElementsOrVersion >= header.ElementsOrVersion
	})
	if index < len(handleType_Versions) {
		if handleType_Versions[index].ElementsOrVersion > header.ElementsOrVersion {
			index--
		}
		expectedSize := handleType_Versions[index].Size
		if expectedSize != header.Size {
			return &bindings.ValidationError{bindings.UnexpectedStructHeader,
				fmt.Sprintf("invalid struct header size: should be %d, but was %d", expectedSize, header.Size),
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		value, err := decoder.ReadBool()
		if err != nil {
			return err
		}
		s.Nullable = value
	}
	if header.ElementsOrVersion >= 0 {
		value, err := decoder.ReadInt32()
		if err != nil {
			return err
		}
		s.Kind = HandleType_Kind(value)
	}

	if err := decoder.Finish(); err != nil {
		return err
	}
	return nil
}

type HandleType_Kind int32

const (
	HandleType_Kind_Unspecified      HandleType_Kind = 0
	HandleType_Kind_Channel          HandleType_Kind = 1
	HandleType_Kind_DataPipeConsumer HandleType_Kind = 2
	HandleType_Kind_DataPipeProducer HandleType_Kind = 3
	HandleType_Kind_Vmo              HandleType_Kind = 4
	HandleType_Kind_Process          HandleType_Kind = 5
	HandleType_Kind_Thread           HandleType_Kind = 6
	HandleType_Kind_Event            HandleType_Kind = 7
	HandleType_Kind_Port             HandleType_Kind = 8
	HandleType_Kind_Job              HandleType_Kind = 9
	HandleType_Kind_Socket           HandleType_Kind = 10
	HandleType_Kind_EventPair        HandleType_Kind = 11
)

type ArrayType struct {
	Nullable    bool
	FixedLength int32
	ElementType Type
}

func (s *ArrayType) Encode(encoder *bindings.Encoder) error {
	encoder.StartStruct(24, 0)
	if err := encoder.WriteBool(s.Nullable); err != nil {
		return err
	}
	if err := encoder.WriteInt32(s.FixedLength); err != nil {
		return err
	}
	if s.ElementType == nil {
		return &bindings.ValidationError{bindings.UnexpectedNullUnion, "unexpected null union"}
	} else {
		if err := s.ElementType.Encode(encoder); err != nil {
			return err
		}
	}

	if err := encoder.Finish(); err != nil {
		return err
	}
	return nil
}

var arrayType_Versions []bindings.DataHeader = []bindings.DataHeader{
	bindings.DataHeader{32, 0},
}

func (s *ArrayType) Decode(decoder *bindings.Decoder) error {
	header, err := decoder.StartStruct()
	if err != nil {
		return err
	}

	index := sort.Search(len(arrayType_Versions), func(i int) bool {
		return arrayType_Versions[i].ElementsOrVersion >= header.ElementsOrVersion
	})
	if index < len(arrayType_Versions) {
		if arrayType_Versions[index].ElementsOrVersion > header.ElementsOrVersion {
			index--
		}
		expectedSize := arrayType_Versions[index].Size
		if expectedSize != header.Size {
			return &bindings.ValidationError{bindings.UnexpectedStructHeader,
				fmt.Sprintf("invalid struct header size: should be %d, but was %d", expectedSize, header.Size),
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		value, err := decoder.ReadBool()
		if err != nil {
			return err
		}
		s.Nullable = value
	}
	if header.ElementsOrVersion >= 0 {
		value, err := decoder.ReadInt32()
		if err != nil {
			return err
		}
		s.FixedLength = value
	}
	if header.ElementsOrVersion >= 0 {
		var err error
		s.ElementType, err = DecodeType(decoder)
		if err != nil {
			return err
		}
		if s.ElementType == nil {
			return &bindings.ValidationError{bindings.UnexpectedNullUnion, "unexpected null union"}
		}
	}

	if err := decoder.Finish(); err != nil {
		return err
	}
	return nil
}

type MapType struct {
	Nullable  bool
	KeyType   Type
	ValueType Type
}

func (s *MapType) Encode(encoder *bindings.Encoder) error {
	encoder.StartStruct(40, 0)
	if err := encoder.WriteBool(s.Nullable); err != nil {
		return err
	}
	if s.KeyType == nil {
		return &bindings.ValidationError{bindings.UnexpectedNullUnion, "unexpected null union"}
	} else {
		if err := s.KeyType.Encode(encoder); err != nil {
			return err
		}
	}
	if s.ValueType == nil {
		return &bindings.ValidationError{bindings.UnexpectedNullUnion, "unexpected null union"}
	} else {
		if err := s.ValueType.Encode(encoder); err != nil {
			return err
		}
	}

	if err := encoder.Finish(); err != nil {
		return err
	}
	return nil
}

var mapType_Versions []bindings.DataHeader = []bindings.DataHeader{
	bindings.DataHeader{48, 0},
}

func (s *MapType) Decode(decoder *bindings.Decoder) error {
	header, err := decoder.StartStruct()
	if err != nil {
		return err
	}

	index := sort.Search(len(mapType_Versions), func(i int) bool {
		return mapType_Versions[i].ElementsOrVersion >= header.ElementsOrVersion
	})
	if index < len(mapType_Versions) {
		if mapType_Versions[index].ElementsOrVersion > header.ElementsOrVersion {
			index--
		}
		expectedSize := mapType_Versions[index].Size
		if expectedSize != header.Size {
			return &bindings.ValidationError{bindings.UnexpectedStructHeader,
				fmt.Sprintf("invalid struct header size: should be %d, but was %d", expectedSize, header.Size),
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		value, err := decoder.ReadBool()
		if err != nil {
			return err
		}
		s.Nullable = value
	}
	if header.ElementsOrVersion >= 0 {
		var err error
		s.KeyType, err = DecodeType(decoder)
		if err != nil {
			return err
		}
		if s.KeyType == nil {
			return &bindings.ValidationError{bindings.UnexpectedNullUnion, "unexpected null union"}
		}
	}
	if header.ElementsOrVersion >= 0 {
		var err error
		s.ValueType, err = DecodeType(decoder)
		if err != nil {
			return err
		}
		if s.ValueType == nil {
			return &bindings.ValidationError{bindings.UnexpectedNullUnion, "unexpected null union"}
		}
	}

	if err := decoder.Finish(); err != nil {
		return err
	}
	return nil
}

type TypeReference struct {
	Nullable           bool
	IsInterfaceRequest bool
	Identifier         *string
	TypeKey            *string
}

func (s *TypeReference) Encode(encoder *bindings.Encoder) error {
	encoder.StartStruct(24, 0)
	if err := encoder.WriteBool(s.Nullable); err != nil {
		return err
	}
	if err := encoder.WriteBool(s.IsInterfaceRequest); err != nil {
		return err
	}
	if s.Identifier == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		if err := encoder.WriteString((*s.Identifier)); err != nil {
			return err
		}
	}
	if s.TypeKey == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		if err := encoder.WriteString((*s.TypeKey)); err != nil {
			return err
		}
	}

	if err := encoder.Finish(); err != nil {
		return err
	}
	return nil
}

var typeReference_Versions []bindings.DataHeader = []bindings.DataHeader{
	bindings.DataHeader{32, 0},
}

func (s *TypeReference) Decode(decoder *bindings.Decoder) error {
	header, err := decoder.StartStruct()
	if err != nil {
		return err
	}

	index := sort.Search(len(typeReference_Versions), func(i int) bool {
		return typeReference_Versions[i].ElementsOrVersion >= header.ElementsOrVersion
	})
	if index < len(typeReference_Versions) {
		if typeReference_Versions[index].ElementsOrVersion > header.ElementsOrVersion {
			index--
		}
		expectedSize := typeReference_Versions[index].Size
		if expectedSize != header.Size {
			return &bindings.ValidationError{bindings.UnexpectedStructHeader,
				fmt.Sprintf("invalid struct header size: should be %d, but was %d", expectedSize, header.Size),
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		value, err := decoder.ReadBool()
		if err != nil {
			return err
		}
		s.Nullable = value
	}
	if header.ElementsOrVersion >= 0 {
		value, err := decoder.ReadBool()
		if err != nil {
			return err
		}
		s.IsInterfaceRequest = value
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			s.Identifier = nil
		} else {
			value, err := decoder.ReadString()
			if err != nil {
				return err
			}
			s.Identifier = &value
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			s.TypeKey = nil
		} else {
			value, err := decoder.ReadString()
			if err != nil {
				return err
			}
			s.TypeKey = &value
		}
	}

	if err := decoder.Finish(); err != nil {
		return err
	}
	return nil
}

type StructField struct {
	DeclData     *DeclarationData
	Type         Type
	DefaultValue DefaultFieldValue
	Offset       uint32
	Bit          int8
	MinVersion   uint32
}

func (s *StructField) Encode(encoder *bindings.Encoder) error {
	encoder.StartStruct(56, 0)
	if s.DeclData == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		if err := s.DeclData.Encode(encoder); err != nil {
			return err
		}
	}
	if s.Type == nil {
		return &bindings.ValidationError{bindings.UnexpectedNullUnion, "unexpected null union"}
	} else {
		if err := s.Type.Encode(encoder); err != nil {
			return err
		}
	}
	if s.DefaultValue == nil {
		encoder.WriteNullUnion()
	} else {
		if err := s.DefaultValue.Encode(encoder); err != nil {
			return err
		}
	}
	if err := encoder.WriteUint32(s.Offset); err != nil {
		return err
	}
	if err := encoder.WriteInt8(s.Bit); err != nil {
		return err
	}
	if err := encoder.WriteUint32(s.MinVersion); err != nil {
		return err
	}

	if err := encoder.Finish(); err != nil {
		return err
	}
	return nil
}

var structField_Versions []bindings.DataHeader = []bindings.DataHeader{
	bindings.DataHeader{64, 0},
}

func (s *StructField) Decode(decoder *bindings.Decoder) error {
	header, err := decoder.StartStruct()
	if err != nil {
		return err
	}

	index := sort.Search(len(structField_Versions), func(i int) bool {
		return structField_Versions[i].ElementsOrVersion >= header.ElementsOrVersion
	})
	if index < len(structField_Versions) {
		if structField_Versions[index].ElementsOrVersion > header.ElementsOrVersion {
			index--
		}
		expectedSize := structField_Versions[index].Size
		if expectedSize != header.Size {
			return &bindings.ValidationError{bindings.UnexpectedStructHeader,
				fmt.Sprintf("invalid struct header size: should be %d, but was %d", expectedSize, header.Size),
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			s.DeclData = nil
		} else {
			s.DeclData = new(DeclarationData)
			if err := s.DeclData.Decode(decoder); err != nil {
				return err
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		var err error
		s.Type, err = DecodeType(decoder)
		if err != nil {
			return err
		}
		if s.Type == nil {
			return &bindings.ValidationError{bindings.UnexpectedNullUnion, "unexpected null union"}
		}
	}
	if header.ElementsOrVersion >= 0 {
		var err error
		s.DefaultValue, err = DecodeDefaultFieldValue(decoder)
		if err != nil {
			return err
		}
	}
	if header.ElementsOrVersion >= 0 {
		value, err := decoder.ReadUint32()
		if err != nil {
			return err
		}
		s.Offset = value
	}
	if header.ElementsOrVersion >= 0 {
		value, err := decoder.ReadInt8()
		if err != nil {
			return err
		}
		s.Bit = value
	}
	if header.ElementsOrVersion >= 0 {
		value, err := decoder.ReadUint32()
		if err != nil {
			return err
		}
		s.MinVersion = value
	}

	if err := decoder.Finish(); err != nil {
		return err
	}
	return nil
}

type DefaultKeyword struct {
}

func (s *DefaultKeyword) Encode(encoder *bindings.Encoder) error {
	encoder.StartStruct(0, 0)

	if err := encoder.Finish(); err != nil {
		return err
	}
	return nil
}

var defaultKeyword_Versions []bindings.DataHeader = []bindings.DataHeader{
	bindings.DataHeader{8, 0},
}

func (s *DefaultKeyword) Decode(decoder *bindings.Decoder) error {
	header, err := decoder.StartStruct()
	if err != nil {
		return err
	}

	index := sort.Search(len(defaultKeyword_Versions), func(i int) bool {
		return defaultKeyword_Versions[i].ElementsOrVersion >= header.ElementsOrVersion
	})
	if index < len(defaultKeyword_Versions) {
		if defaultKeyword_Versions[index].ElementsOrVersion > header.ElementsOrVersion {
			index--
		}
		expectedSize := defaultKeyword_Versions[index].Size
		if expectedSize != header.Size {
			return &bindings.ValidationError{bindings.UnexpectedStructHeader,
				fmt.Sprintf("invalid struct header size: should be %d, but was %d", expectedSize, header.Size),
			}
		}
	}

	if err := decoder.Finish(); err != nil {
		return err
	}
	return nil
}

type StructVersion struct {
	VersionNumber uint32
	NumFields     uint32
	NumBytes      uint32
}

func (s *StructVersion) Encode(encoder *bindings.Encoder) error {
	encoder.StartStruct(16, 0)
	if err := encoder.WriteUint32(s.VersionNumber); err != nil {
		return err
	}
	if err := encoder.WriteUint32(s.NumFields); err != nil {
		return err
	}
	if err := encoder.WriteUint32(s.NumBytes); err != nil {
		return err
	}

	if err := encoder.Finish(); err != nil {
		return err
	}
	return nil
}

var structVersion_Versions []bindings.DataHeader = []bindings.DataHeader{
	bindings.DataHeader{24, 0},
}

func (s *StructVersion) Decode(decoder *bindings.Decoder) error {
	header, err := decoder.StartStruct()
	if err != nil {
		return err
	}

	index := sort.Search(len(structVersion_Versions), func(i int) bool {
		return structVersion_Versions[i].ElementsOrVersion >= header.ElementsOrVersion
	})
	if index < len(structVersion_Versions) {
		if structVersion_Versions[index].ElementsOrVersion > header.ElementsOrVersion {
			index--
		}
		expectedSize := structVersion_Versions[index].Size
		if expectedSize != header.Size {
			return &bindings.ValidationError{bindings.UnexpectedStructHeader,
				fmt.Sprintf("invalid struct header size: should be %d, but was %d", expectedSize, header.Size),
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		value, err := decoder.ReadUint32()
		if err != nil {
			return err
		}
		s.VersionNumber = value
	}
	if header.ElementsOrVersion >= 0 {
		value, err := decoder.ReadUint32()
		if err != nil {
			return err
		}
		s.NumFields = value
	}
	if header.ElementsOrVersion >= 0 {
		value, err := decoder.ReadUint32()
		if err != nil {
			return err
		}
		s.NumBytes = value
	}

	if err := decoder.Finish(); err != nil {
		return err
	}
	return nil
}

type FidlStruct struct {
	DeclData    *DeclarationData
	Fields      []StructField
	VersionInfo *[]StructVersion
}

func (s *FidlStruct) Encode(encoder *bindings.Encoder) error {
	encoder.StartStruct(24, 0)
	if s.DeclData == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		if err := s.DeclData.Encode(encoder); err != nil {
			return err
		}
	}
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	encoder.StartArray(uint32(len(s.Fields)), 64)
	for _, elem0 := range s.Fields {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		if err := elem0.Encode(encoder); err != nil {
			return err
		}
	}
	if err := encoder.Finish(); err != nil {
		return err
	}
	if s.VersionInfo == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		encoder.StartArray(uint32(len((*s.VersionInfo))), 64)
		for _, elem0 := range *s.VersionInfo {
			if err := encoder.WritePointer(); err != nil {
				return err
			}
			if err := elem0.Encode(encoder); err != nil {
				return err
			}
		}
		if err := encoder.Finish(); err != nil {
			return err
		}
	}

	if err := encoder.Finish(); err != nil {
		return err
	}
	return nil
}

var fidlStruct_Versions []bindings.DataHeader = []bindings.DataHeader{
	bindings.DataHeader{32, 0},
}

func (s *FidlStruct) Decode(decoder *bindings.Decoder) error {
	header, err := decoder.StartStruct()
	if err != nil {
		return err
	}

	index := sort.Search(len(fidlStruct_Versions), func(i int) bool {
		return fidlStruct_Versions[i].ElementsOrVersion >= header.ElementsOrVersion
	})
	if index < len(fidlStruct_Versions) {
		if fidlStruct_Versions[index].ElementsOrVersion > header.ElementsOrVersion {
			index--
		}
		expectedSize := fidlStruct_Versions[index].Size
		if expectedSize != header.Size {
			return &bindings.ValidationError{bindings.UnexpectedStructHeader,
				fmt.Sprintf("invalid struct header size: should be %d, but was %d", expectedSize, header.Size),
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			s.DeclData = nil
		} else {
			s.DeclData = new(DeclarationData)
			if err := s.DeclData.Decode(decoder); err != nil {
				return err
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
		} else {

			len0, err := decoder.StartArray(64)
			if err != nil {
				return err
			}
			s.Fields = make([]StructField, len0)
			for i := uint32(0); i < len0; i++ {
				var elem0 StructField
				pointer, err := decoder.ReadPointer()
				if err != nil {
					return err
				}
				if pointer == 0 {
					return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
				} else {
					if err := elem0.Decode(decoder); err != nil {
						return err
					}
				}
				s.Fields[i] = elem0
			}
			if err := decoder.Finish(); err != nil {
				return nil
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			s.VersionInfo = nil
		} else {
			s.VersionInfo = new([]StructVersion)
			len0, err := decoder.StartArray(64)
			if err != nil {
				return err
			}
			(*s.VersionInfo) = make([]StructVersion, len0)
			for i := uint32(0); i < len0; i++ {
				var elem0 StructVersion
				pointer, err := decoder.ReadPointer()
				if err != nil {
					return err
				}
				if pointer == 0 {
					return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
				} else {
					if err := elem0.Decode(decoder); err != nil {
						return err
					}
				}
				(*s.VersionInfo)[i] = elem0
			}
			if err := decoder.Finish(); err != nil {
				return nil
			}
		}
	}

	if err := decoder.Finish(); err != nil {
		return err
	}
	return nil
}

type UnionField struct {
	DeclData *DeclarationData
	Type     Type
	Tag      uint32
}

func (s *UnionField) Encode(encoder *bindings.Encoder) error {
	encoder.StartStruct(32, 0)
	if s.DeclData == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		if err := s.DeclData.Encode(encoder); err != nil {
			return err
		}
	}
	if s.Type == nil {
		return &bindings.ValidationError{bindings.UnexpectedNullUnion, "unexpected null union"}
	} else {
		if err := s.Type.Encode(encoder); err != nil {
			return err
		}
	}
	if err := encoder.WriteUint32(s.Tag); err != nil {
		return err
	}

	if err := encoder.Finish(); err != nil {
		return err
	}
	return nil
}

var unionField_Versions []bindings.DataHeader = []bindings.DataHeader{
	bindings.DataHeader{40, 0},
}

func (s *UnionField) Decode(decoder *bindings.Decoder) error {
	header, err := decoder.StartStruct()
	if err != nil {
		return err
	}

	index := sort.Search(len(unionField_Versions), func(i int) bool {
		return unionField_Versions[i].ElementsOrVersion >= header.ElementsOrVersion
	})
	if index < len(unionField_Versions) {
		if unionField_Versions[index].ElementsOrVersion > header.ElementsOrVersion {
			index--
		}
		expectedSize := unionField_Versions[index].Size
		if expectedSize != header.Size {
			return &bindings.ValidationError{bindings.UnexpectedStructHeader,
				fmt.Sprintf("invalid struct header size: should be %d, but was %d", expectedSize, header.Size),
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			s.DeclData = nil
		} else {
			s.DeclData = new(DeclarationData)
			if err := s.DeclData.Decode(decoder); err != nil {
				return err
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		var err error
		s.Type, err = DecodeType(decoder)
		if err != nil {
			return err
		}
		if s.Type == nil {
			return &bindings.ValidationError{bindings.UnexpectedNullUnion, "unexpected null union"}
		}
	}
	if header.ElementsOrVersion >= 0 {
		value, err := decoder.ReadUint32()
		if err != nil {
			return err
		}
		s.Tag = value
	}

	if err := decoder.Finish(); err != nil {
		return err
	}
	return nil
}

type FidlUnion struct {
	DeclData *DeclarationData
	Fields   []UnionField
}

func (s *FidlUnion) Encode(encoder *bindings.Encoder) error {
	encoder.StartStruct(16, 0)
	if s.DeclData == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		if err := s.DeclData.Encode(encoder); err != nil {
			return err
		}
	}
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	encoder.StartArray(uint32(len(s.Fields)), 64)
	for _, elem0 := range s.Fields {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		if err := elem0.Encode(encoder); err != nil {
			return err
		}
	}
	if err := encoder.Finish(); err != nil {
		return err
	}

	if err := encoder.Finish(); err != nil {
		return err
	}
	return nil
}

var fidlUnion_Versions []bindings.DataHeader = []bindings.DataHeader{
	bindings.DataHeader{24, 0},
}

func (s *FidlUnion) Decode(decoder *bindings.Decoder) error {
	header, err := decoder.StartStruct()
	if err != nil {
		return err
	}

	index := sort.Search(len(fidlUnion_Versions), func(i int) bool {
		return fidlUnion_Versions[i].ElementsOrVersion >= header.ElementsOrVersion
	})
	if index < len(fidlUnion_Versions) {
		if fidlUnion_Versions[index].ElementsOrVersion > header.ElementsOrVersion {
			index--
		}
		expectedSize := fidlUnion_Versions[index].Size
		if expectedSize != header.Size {
			return &bindings.ValidationError{bindings.UnexpectedStructHeader,
				fmt.Sprintf("invalid struct header size: should be %d, but was %d", expectedSize, header.Size),
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			s.DeclData = nil
		} else {
			s.DeclData = new(DeclarationData)
			if err := s.DeclData.Decode(decoder); err != nil {
				return err
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
		} else {

			len0, err := decoder.StartArray(64)
			if err != nil {
				return err
			}
			s.Fields = make([]UnionField, len0)
			for i := uint32(0); i < len0; i++ {
				var elem0 UnionField
				pointer, err := decoder.ReadPointer()
				if err != nil {
					return err
				}
				if pointer == 0 {
					return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
				} else {
					if err := elem0.Decode(decoder); err != nil {
						return err
					}
				}
				s.Fields[i] = elem0
			}
			if err := decoder.Finish(); err != nil {
				return nil
			}
		}
	}

	if err := decoder.Finish(); err != nil {
		return err
	}
	return nil
}

type EnumValue struct {
	DeclData         *DeclarationData
	InitializerValue Value
	IntValue         int32
}

func (s *EnumValue) Encode(encoder *bindings.Encoder) error {
	encoder.StartStruct(32, 0)
	if s.DeclData == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		if err := s.DeclData.Encode(encoder); err != nil {
			return err
		}
	}
	if s.InitializerValue == nil {
		encoder.WriteNullUnion()
	} else {
		if err := s.InitializerValue.Encode(encoder); err != nil {
			return err
		}
	}
	if err := encoder.WriteInt32(s.IntValue); err != nil {
		return err
	}

	if err := encoder.Finish(); err != nil {
		return err
	}
	return nil
}

var enumValue_Versions []bindings.DataHeader = []bindings.DataHeader{
	bindings.DataHeader{40, 0},
}

func (s *EnumValue) Decode(decoder *bindings.Decoder) error {
	header, err := decoder.StartStruct()
	if err != nil {
		return err
	}

	index := sort.Search(len(enumValue_Versions), func(i int) bool {
		return enumValue_Versions[i].ElementsOrVersion >= header.ElementsOrVersion
	})
	if index < len(enumValue_Versions) {
		if enumValue_Versions[index].ElementsOrVersion > header.ElementsOrVersion {
			index--
		}
		expectedSize := enumValue_Versions[index].Size
		if expectedSize != header.Size {
			return &bindings.ValidationError{bindings.UnexpectedStructHeader,
				fmt.Sprintf("invalid struct header size: should be %d, but was %d", expectedSize, header.Size),
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			s.DeclData = nil
		} else {
			s.DeclData = new(DeclarationData)
			if err := s.DeclData.Decode(decoder); err != nil {
				return err
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		var err error
		s.InitializerValue, err = DecodeValue(decoder)
		if err != nil {
			return err
		}
	}
	if header.ElementsOrVersion >= 0 {
		value, err := decoder.ReadInt32()
		if err != nil {
			return err
		}
		s.IntValue = value
	}

	if err := decoder.Finish(); err != nil {
		return err
	}
	return nil
}

type FidlEnum struct {
	DeclData *DeclarationData
	Values   []EnumValue
}

func (s *FidlEnum) Encode(encoder *bindings.Encoder) error {
	encoder.StartStruct(16, 0)
	if s.DeclData == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		if err := s.DeclData.Encode(encoder); err != nil {
			return err
		}
	}
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	encoder.StartArray(uint32(len(s.Values)), 64)
	for _, elem0 := range s.Values {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		if err := elem0.Encode(encoder); err != nil {
			return err
		}
	}
	if err := encoder.Finish(); err != nil {
		return err
	}

	if err := encoder.Finish(); err != nil {
		return err
	}
	return nil
}

var fidlEnum_Versions []bindings.DataHeader = []bindings.DataHeader{
	bindings.DataHeader{24, 0},
}

func (s *FidlEnum) Decode(decoder *bindings.Decoder) error {
	header, err := decoder.StartStruct()
	if err != nil {
		return err
	}

	index := sort.Search(len(fidlEnum_Versions), func(i int) bool {
		return fidlEnum_Versions[i].ElementsOrVersion >= header.ElementsOrVersion
	})
	if index < len(fidlEnum_Versions) {
		if fidlEnum_Versions[index].ElementsOrVersion > header.ElementsOrVersion {
			index--
		}
		expectedSize := fidlEnum_Versions[index].Size
		if expectedSize != header.Size {
			return &bindings.ValidationError{bindings.UnexpectedStructHeader,
				fmt.Sprintf("invalid struct header size: should be %d, but was %d", expectedSize, header.Size),
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			s.DeclData = nil
		} else {
			s.DeclData = new(DeclarationData)
			if err := s.DeclData.Decode(decoder); err != nil {
				return err
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
		} else {

			len0, err := decoder.StartArray(64)
			if err != nil {
				return err
			}
			s.Values = make([]EnumValue, len0)
			for i := uint32(0); i < len0; i++ {
				var elem0 EnumValue
				pointer, err := decoder.ReadPointer()
				if err != nil {
					return err
				}
				if pointer == 0 {
					return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
				} else {
					if err := elem0.Decode(decoder); err != nil {
						return err
					}
				}
				s.Values[i] = elem0
			}
			if err := decoder.Finish(); err != nil {
				return nil
			}
		}
	}

	if err := decoder.Finish(); err != nil {
		return err
	}
	return nil
}

type FidlMethod struct {
	DeclData       *DeclarationData
	Parameters     FidlStruct
	ResponseParams *FidlStruct
	Ordinal        uint32
	MinVersion     uint32
}

func (s *FidlMethod) Encode(encoder *bindings.Encoder) error {
	encoder.StartStruct(32, 0)
	if s.DeclData == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		if err := s.DeclData.Encode(encoder); err != nil {
			return err
		}
	}
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	if err := s.Parameters.Encode(encoder); err != nil {
		return err
	}
	if s.ResponseParams == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		if err := s.ResponseParams.Encode(encoder); err != nil {
			return err
		}
	}
	if err := encoder.WriteUint32(s.Ordinal); err != nil {
		return err
	}
	if err := encoder.WriteUint32(s.MinVersion); err != nil {
		return err
	}

	if err := encoder.Finish(); err != nil {
		return err
	}
	return nil
}

var fidlMethod_Versions []bindings.DataHeader = []bindings.DataHeader{
	bindings.DataHeader{40, 0},
}

func (s *FidlMethod) Decode(decoder *bindings.Decoder) error {
	header, err := decoder.StartStruct()
	if err != nil {
		return err
	}

	index := sort.Search(len(fidlMethod_Versions), func(i int) bool {
		return fidlMethod_Versions[i].ElementsOrVersion >= header.ElementsOrVersion
	})
	if index < len(fidlMethod_Versions) {
		if fidlMethod_Versions[index].ElementsOrVersion > header.ElementsOrVersion {
			index--
		}
		expectedSize := fidlMethod_Versions[index].Size
		if expectedSize != header.Size {
			return &bindings.ValidationError{bindings.UnexpectedStructHeader,
				fmt.Sprintf("invalid struct header size: should be %d, but was %d", expectedSize, header.Size),
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			s.DeclData = nil
		} else {
			s.DeclData = new(DeclarationData)
			if err := s.DeclData.Decode(decoder); err != nil {
				return err
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
		} else {
			if err := s.Parameters.Decode(decoder); err != nil {
				return err
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			s.ResponseParams = nil
		} else {
			s.ResponseParams = new(FidlStruct)
			if err := s.ResponseParams.Decode(decoder); err != nil {
				return err
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		value, err := decoder.ReadUint32()
		if err != nil {
			return err
		}
		s.Ordinal = value
	}
	if header.ElementsOrVersion >= 0 {
		value, err := decoder.ReadUint32()
		if err != nil {
			return err
		}
		s.MinVersion = value
	}

	if err := decoder.Finish(); err != nil {
		return err
	}
	return nil
}

type FidlInterface struct {
	DeclData       *DeclarationData
	ServiceName    *string
	Methods        map[uint32]FidlMethod
	CurrentVersion uint32
}

func (s *FidlInterface) Encode(encoder *bindings.Encoder) error {
	encoder.StartStruct(32, 0)
	if s.DeclData == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		if err := s.DeclData.Encode(encoder); err != nil {
			return err
		}
	}
	if s.ServiceName == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		if err := encoder.WriteString((*s.ServiceName)); err != nil {
			return err
		}
	}
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	encoder.StartMap()
	{
		var keys0 []uint32
		var values0 []FidlMethod
		for elem0 := range s.Methods {
			keys0 = append(keys0, elem0)
		}
		if encoder.Deterministic() {
			bindings.SortMapKeys(&keys0)
		}
		for _, elem0 := range keys0 {
			values0 = append(values0, s.Methods[elem0])
		}
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		encoder.StartArray(uint32(len(keys0)), 32)
		for _, elem0 := range keys0 {
			if err := encoder.WriteUint32(elem0); err != nil {
				return err
			}
		}
		if err := encoder.Finish(); err != nil {
			return err
		}
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		encoder.StartArray(uint32(len(values0)), 64)
		for _, elem0 := range values0 {
			if err := encoder.WritePointer(); err != nil {
				return err
			}
			if err := elem0.Encode(encoder); err != nil {
				return err
			}
		}
		if err := encoder.Finish(); err != nil {
			return err
		}
	}
	if err := encoder.Finish(); err != nil {
		return err
	}
	if err := encoder.WriteUint32(s.CurrentVersion); err != nil {
		return err
	}

	if err := encoder.Finish(); err != nil {
		return err
	}
	return nil
}

var fidlInterface_Versions []bindings.DataHeader = []bindings.DataHeader{
	bindings.DataHeader{40, 0},
}

func (s *FidlInterface) Decode(decoder *bindings.Decoder) error {
	header, err := decoder.StartStruct()
	if err != nil {
		return err
	}

	index := sort.Search(len(fidlInterface_Versions), func(i int) bool {
		return fidlInterface_Versions[i].ElementsOrVersion >= header.ElementsOrVersion
	})
	if index < len(fidlInterface_Versions) {
		if fidlInterface_Versions[index].ElementsOrVersion > header.ElementsOrVersion {
			index--
		}
		expectedSize := fidlInterface_Versions[index].Size
		if expectedSize != header.Size {
			return &bindings.ValidationError{bindings.UnexpectedStructHeader,
				fmt.Sprintf("invalid struct header size: should be %d, but was %d", expectedSize, header.Size),
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			s.DeclData = nil
		} else {
			s.DeclData = new(DeclarationData)
			if err := s.DeclData.Decode(decoder); err != nil {
				return err
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			s.ServiceName = nil
		} else {
			value, err := decoder.ReadString()
			if err != nil {
				return err
			}
			s.ServiceName = &value
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
		} else {

			s.Methods = map[uint32]FidlMethod{}
			if err := decoder.StartMap(); err != nil {
				return err
			}
			var keys0 []uint32
			{
				pointer, err := decoder.ReadPointer()
				if err != nil {
					return err
				}
				if pointer == 0 {
					return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
				} else {

					len0, err := decoder.StartArray(32)
					if err != nil {
						return err
					}
					keys0 = make([]uint32, len0)
					for i := uint32(0); i < len0; i++ {
						var elem0 uint32
						value, err := decoder.ReadUint32()
						if err != nil {
							return err
						}
						elem0 = value
						keys0[i] = elem0
					}
					if err := decoder.Finish(); err != nil {
						return nil
					}
				}
			}
			var values0 []FidlMethod
			{
				pointer, err := decoder.ReadPointer()
				if err != nil {
					return err
				}
				if pointer == 0 {
					return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
				} else {

					len0, err := decoder.StartArray(64)
					if err != nil {
						return err
					}
					values0 = make([]FidlMethod, len0)
					for i := uint32(0); i < len0; i++ {
						var elem0 FidlMethod
						pointer, err := decoder.ReadPointer()
						if err != nil {
							return err
						}
						if pointer == 0 {
							return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
						} else {
							if err := elem0.Decode(decoder); err != nil {
								return err
							}
						}
						values0[i] = elem0
					}
					if err := decoder.Finish(); err != nil {
						return nil
					}
				}
			}
			if err := decoder.Finish(); err != nil {
				return nil
			}
			if len(keys0) != len(values0) {
				return &bindings.ValidationError{bindings.DifferentSizedArraysInMap,
					fmt.Sprintf("Number of keys %d is different from number of values %d",
						len(keys0), len(values0))}
			}
			for i := 0; i < len(keys0); i++ {
				s.Methods[keys0[i]] = values0[i]
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		value, err := decoder.ReadUint32()
		if err != nil {
			return err
		}
		s.CurrentVersion = value
	}

	if err := decoder.Finish(); err != nil {
		return err
	}
	return nil
}

type ConstantReference struct {
	Identifier  string
	ConstantKey string
}

func (s *ConstantReference) Encode(encoder *bindings.Encoder) error {
	encoder.StartStruct(16, 0)
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	if err := encoder.WriteString(s.Identifier); err != nil {
		return err
	}
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	if err := encoder.WriteString(s.ConstantKey); err != nil {
		return err
	}

	if err := encoder.Finish(); err != nil {
		return err
	}
	return nil
}

var constantReference_Versions []bindings.DataHeader = []bindings.DataHeader{
	bindings.DataHeader{24, 0},
}

func (s *ConstantReference) Decode(decoder *bindings.Decoder) error {
	header, err := decoder.StartStruct()
	if err != nil {
		return err
	}

	index := sort.Search(len(constantReference_Versions), func(i int) bool {
		return constantReference_Versions[i].ElementsOrVersion >= header.ElementsOrVersion
	})
	if index < len(constantReference_Versions) {
		if constantReference_Versions[index].ElementsOrVersion > header.ElementsOrVersion {
			index--
		}
		expectedSize := constantReference_Versions[index].Size
		if expectedSize != header.Size {
			return &bindings.ValidationError{bindings.UnexpectedStructHeader,
				fmt.Sprintf("invalid struct header size: should be %d, but was %d", expectedSize, header.Size),
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
		} else {
			value, err := decoder.ReadString()
			if err != nil {
				return err
			}
			s.Identifier = value
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
		} else {
			value, err := decoder.ReadString()
			if err != nil {
				return err
			}
			s.ConstantKey = value
		}
	}

	if err := decoder.Finish(); err != nil {
		return err
	}
	return nil
}

type EnumValueReference struct {
	Identifier     string
	EnumTypeKey    string
	EnumValueIndex uint32
}

func (s *EnumValueReference) Encode(encoder *bindings.Encoder) error {
	encoder.StartStruct(24, 0)
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	if err := encoder.WriteString(s.Identifier); err != nil {
		return err
	}
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	if err := encoder.WriteString(s.EnumTypeKey); err != nil {
		return err
	}
	if err := encoder.WriteUint32(s.EnumValueIndex); err != nil {
		return err
	}

	if err := encoder.Finish(); err != nil {
		return err
	}
	return nil
}

var enumValueReference_Versions []bindings.DataHeader = []bindings.DataHeader{
	bindings.DataHeader{32, 0},
}

func (s *EnumValueReference) Decode(decoder *bindings.Decoder) error {
	header, err := decoder.StartStruct()
	if err != nil {
		return err
	}

	index := sort.Search(len(enumValueReference_Versions), func(i int) bool {
		return enumValueReference_Versions[i].ElementsOrVersion >= header.ElementsOrVersion
	})
	if index < len(enumValueReference_Versions) {
		if enumValueReference_Versions[index].ElementsOrVersion > header.ElementsOrVersion {
			index--
		}
		expectedSize := enumValueReference_Versions[index].Size
		if expectedSize != header.Size {
			return &bindings.ValidationError{bindings.UnexpectedStructHeader,
				fmt.Sprintf("invalid struct header size: should be %d, but was %d", expectedSize, header.Size),
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
		} else {
			value, err := decoder.ReadString()
			if err != nil {
				return err
			}
			s.Identifier = value
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
		} else {
			value, err := decoder.ReadString()
			if err != nil {
				return err
			}
			s.EnumTypeKey = value
		}
	}
	if header.ElementsOrVersion >= 0 {
		value, err := decoder.ReadUint32()
		if err != nil {
			return err
		}
		s.EnumValueIndex = value
	}

	if err := decoder.Finish(); err != nil {
		return err
	}
	return nil
}

type DeclaredConstant struct {
	DeclData              DeclarationData
	Type                  Type
	Value                 Value
	ResolvedConcreteValue Value
}

func (s *DeclaredConstant) Encode(encoder *bindings.Encoder) error {
	encoder.StartStruct(56, 0)
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	if err := s.DeclData.Encode(encoder); err != nil {
		return err
	}
	if s.Type == nil {
		return &bindings.ValidationError{bindings.UnexpectedNullUnion, "unexpected null union"}
	} else {
		if err := s.Type.Encode(encoder); err != nil {
			return err
		}
	}
	if s.Value == nil {
		return &bindings.ValidationError{bindings.UnexpectedNullUnion, "unexpected null union"}
	} else {
		if err := s.Value.Encode(encoder); err != nil {
			return err
		}
	}
	if s.ResolvedConcreteValue == nil {
		encoder.WriteNullUnion()
	} else {
		if err := s.ResolvedConcreteValue.Encode(encoder); err != nil {
			return err
		}
	}

	if err := encoder.Finish(); err != nil {
		return err
	}
	return nil
}

var declaredConstant_Versions []bindings.DataHeader = []bindings.DataHeader{
	bindings.DataHeader{64, 0},
}

func (s *DeclaredConstant) Decode(decoder *bindings.Decoder) error {
	header, err := decoder.StartStruct()
	if err != nil {
		return err
	}

	index := sort.Search(len(declaredConstant_Versions), func(i int) bool {
		return declaredConstant_Versions[i].ElementsOrVersion >= header.ElementsOrVersion
	})
	if index < len(declaredConstant_Versions) {
		if declaredConstant_Versions[index].ElementsOrVersion > header.ElementsOrVersion {
			index--
		}
		expectedSize := declaredConstant_Versions[index].Size
		if expectedSize != header.Size {
			return &bindings.ValidationError{bindings.UnexpectedStructHeader,
				fmt.Sprintf("invalid struct header size: should be %d, but was %d", expectedSize, header.Size),
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
		} else {
			if err := s.DeclData.Decode(decoder); err != nil {
				return err
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		var err error
		s.Type, err = DecodeType(decoder)
		if err != nil {
			return err
		}
		if s.Type == nil {
			return &bindings.ValidationError{bindings.UnexpectedNullUnion, "unexpected null union"}
		}
	}
	if header.ElementsOrVersion >= 0 {
		var err error
		s.Value, err = DecodeValue(decoder)
		if err != nil {
			return err
		}
		if s.Value == nil {
			return &bindings.ValidationError{bindings.UnexpectedNullUnion, "unexpected null union"}
		}
	}
	if header.ElementsOrVersion >= 0 {
		var err error
		s.ResolvedConcreteValue, err = DecodeValue(decoder)
		if err != nil {
			return err
		}
	}

	if err := decoder.Finish(); err != nil {
		return err
	}
	return nil
}

type Attribute struct {
	Key   string
	Value LiteralValue
}

func (s *Attribute) Encode(encoder *bindings.Encoder) error {
	encoder.StartStruct(24, 0)
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	if err := encoder.WriteString(s.Key); err != nil {
		return err
	}
	if s.Value == nil {
		return &bindings.ValidationError{bindings.UnexpectedNullUnion, "unexpected null union"}
	} else {
		if err := s.Value.Encode(encoder); err != nil {
			return err
		}
	}

	if err := encoder.Finish(); err != nil {
		return err
	}
	return nil
}

var attribute_Versions []bindings.DataHeader = []bindings.DataHeader{
	bindings.DataHeader{32, 0},
}

func (s *Attribute) Decode(decoder *bindings.Decoder) error {
	header, err := decoder.StartStruct()
	if err != nil {
		return err
	}

	index := sort.Search(len(attribute_Versions), func(i int) bool {
		return attribute_Versions[i].ElementsOrVersion >= header.ElementsOrVersion
	})
	if index < len(attribute_Versions) {
		if attribute_Versions[index].ElementsOrVersion > header.ElementsOrVersion {
			index--
		}
		expectedSize := attribute_Versions[index].Size
		if expectedSize != header.Size {
			return &bindings.ValidationError{bindings.UnexpectedStructHeader,
				fmt.Sprintf("invalid struct header size: should be %d, but was %d", expectedSize, header.Size),
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
		} else {
			value, err := decoder.ReadString()
			if err != nil {
				return err
			}
			s.Key = value
		}
	}
	if header.ElementsOrVersion >= 0 {
		var err error
		s.Value, err = DecodeLiteralValue(decoder)
		if err != nil {
			return err
		}
		if s.Value == nil {
			return &bindings.ValidationError{bindings.UnexpectedNullUnion, "unexpected null union"}
		}
	}

	if err := decoder.Finish(); err != nil {
		return err
	}
	return nil
}

type DeclarationData struct {
	Attributes            *[]Attribute
	ShortName             *string
	FullIdentifier        *string
	DeclaredOrdinal       int32
	DeclarationOrder      int32
	SourceFileInfo        *SourceFileInfo
	ContainedDeclarations *ContainedDeclarations
	ContainerTypeKey      *string
	Comments              *Comments
}

func (s *DeclarationData) Encode(encoder *bindings.Encoder) error {
	encoder.StartStruct(64, 0)
	if s.Attributes == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		encoder.StartArray(uint32(len((*s.Attributes))), 64)
		for _, elem0 := range *s.Attributes {
			if err := encoder.WritePointer(); err != nil {
				return err
			}
			if err := elem0.Encode(encoder); err != nil {
				return err
			}
		}
		if err := encoder.Finish(); err != nil {
			return err
		}
	}
	if s.ShortName == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		if err := encoder.WriteString((*s.ShortName)); err != nil {
			return err
		}
	}
	if s.FullIdentifier == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		if err := encoder.WriteString((*s.FullIdentifier)); err != nil {
			return err
		}
	}
	if err := encoder.WriteInt32(s.DeclaredOrdinal); err != nil {
		return err
	}
	if err := encoder.WriteInt32(s.DeclarationOrder); err != nil {
		return err
	}
	if s.SourceFileInfo == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		if err := s.SourceFileInfo.Encode(encoder); err != nil {
			return err
		}
	}
	if s.ContainedDeclarations == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		if err := s.ContainedDeclarations.Encode(encoder); err != nil {
			return err
		}
	}
	if s.ContainerTypeKey == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		if err := encoder.WriteString((*s.ContainerTypeKey)); err != nil {
			return err
		}
	}
	if s.Comments == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		if err := s.Comments.Encode(encoder); err != nil {
			return err
		}
	}

	if err := encoder.Finish(); err != nil {
		return err
	}
	return nil
}

var declarationData_Versions []bindings.DataHeader = []bindings.DataHeader{
	bindings.DataHeader{72, 0},
}

func (s *DeclarationData) Decode(decoder *bindings.Decoder) error {
	header, err := decoder.StartStruct()
	if err != nil {
		return err
	}

	index := sort.Search(len(declarationData_Versions), func(i int) bool {
		return declarationData_Versions[i].ElementsOrVersion >= header.ElementsOrVersion
	})
	if index < len(declarationData_Versions) {
		if declarationData_Versions[index].ElementsOrVersion > header.ElementsOrVersion {
			index--
		}
		expectedSize := declarationData_Versions[index].Size
		if expectedSize != header.Size {
			return &bindings.ValidationError{bindings.UnexpectedStructHeader,
				fmt.Sprintf("invalid struct header size: should be %d, but was %d", expectedSize, header.Size),
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			s.Attributes = nil
		} else {
			s.Attributes = new([]Attribute)
			len0, err := decoder.StartArray(64)
			if err != nil {
				return err
			}
			(*s.Attributes) = make([]Attribute, len0)
			for i := uint32(0); i < len0; i++ {
				var elem0 Attribute
				pointer, err := decoder.ReadPointer()
				if err != nil {
					return err
				}
				if pointer == 0 {
					return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
				} else {
					if err := elem0.Decode(decoder); err != nil {
						return err
					}
				}
				(*s.Attributes)[i] = elem0
			}
			if err := decoder.Finish(); err != nil {
				return nil
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			s.ShortName = nil
		} else {
			value, err := decoder.ReadString()
			if err != nil {
				return err
			}
			s.ShortName = &value
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			s.FullIdentifier = nil
		} else {
			value, err := decoder.ReadString()
			if err != nil {
				return err
			}
			s.FullIdentifier = &value
		}
	}
	if header.ElementsOrVersion >= 0 {
		value, err := decoder.ReadInt32()
		if err != nil {
			return err
		}
		s.DeclaredOrdinal = value
	}
	if header.ElementsOrVersion >= 0 {
		value, err := decoder.ReadInt32()
		if err != nil {
			return err
		}
		s.DeclarationOrder = value
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			s.SourceFileInfo = nil
		} else {
			s.SourceFileInfo = new(SourceFileInfo)
			if err := s.SourceFileInfo.Decode(decoder); err != nil {
				return err
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			s.ContainedDeclarations = nil
		} else {
			s.ContainedDeclarations = new(ContainedDeclarations)
			if err := s.ContainedDeclarations.Decode(decoder); err != nil {
				return err
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			s.ContainerTypeKey = nil
		} else {
			value, err := decoder.ReadString()
			if err != nil {
				return err
			}
			s.ContainerTypeKey = &value
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			s.Comments = nil
		} else {
			s.Comments = new(Comments)
			if err := s.Comments.Decode(decoder); err != nil {
				return err
			}
		}
	}

	if err := decoder.Finish(); err != nil {
		return err
	}
	return nil
}

type Comments struct {
	ForAttributes *Comments
	Above         []string
	Left          []string
	Right         []string
	AtBottom      []string
}

func (s *Comments) Encode(encoder *bindings.Encoder) error {
	encoder.StartStruct(40, 0)
	if s.ForAttributes == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		if err := s.ForAttributes.Encode(encoder); err != nil {
			return err
		}
	}
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	encoder.StartArray(uint32(len(s.Above)), 64)
	for _, elem0 := range s.Above {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		if err := encoder.WriteString(elem0); err != nil {
			return err
		}
	}
	if err := encoder.Finish(); err != nil {
		return err
	}
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	encoder.StartArray(uint32(len(s.Left)), 64)
	for _, elem0 := range s.Left {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		if err := encoder.WriteString(elem0); err != nil {
			return err
		}
	}
	if err := encoder.Finish(); err != nil {
		return err
	}
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	encoder.StartArray(uint32(len(s.Right)), 64)
	for _, elem0 := range s.Right {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		if err := encoder.WriteString(elem0); err != nil {
			return err
		}
	}
	if err := encoder.Finish(); err != nil {
		return err
	}
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	encoder.StartArray(uint32(len(s.AtBottom)), 64)
	for _, elem0 := range s.AtBottom {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		if err := encoder.WriteString(elem0); err != nil {
			return err
		}
	}
	if err := encoder.Finish(); err != nil {
		return err
	}

	if err := encoder.Finish(); err != nil {
		return err
	}
	return nil
}

var comments_Versions []bindings.DataHeader = []bindings.DataHeader{
	bindings.DataHeader{48, 0},
}

func (s *Comments) Decode(decoder *bindings.Decoder) error {
	header, err := decoder.StartStruct()
	if err != nil {
		return err
	}

	index := sort.Search(len(comments_Versions), func(i int) bool {
		return comments_Versions[i].ElementsOrVersion >= header.ElementsOrVersion
	})
	if index < len(comments_Versions) {
		if comments_Versions[index].ElementsOrVersion > header.ElementsOrVersion {
			index--
		}
		expectedSize := comments_Versions[index].Size
		if expectedSize != header.Size {
			return &bindings.ValidationError{bindings.UnexpectedStructHeader,
				fmt.Sprintf("invalid struct header size: should be %d, but was %d", expectedSize, header.Size),
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			s.ForAttributes = nil
		} else {
			s.ForAttributes = new(Comments)
			if err := s.ForAttributes.Decode(decoder); err != nil {
				return err
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
		} else {

			len0, err := decoder.StartArray(64)
			if err != nil {
				return err
			}
			s.Above = make([]string, len0)
			for i := uint32(0); i < len0; i++ {
				var elem0 string
				pointer, err := decoder.ReadPointer()
				if err != nil {
					return err
				}
				if pointer == 0 {
					return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
				} else {
					value, err := decoder.ReadString()
					if err != nil {
						return err
					}
					elem0 = value
				}
				s.Above[i] = elem0
			}
			if err := decoder.Finish(); err != nil {
				return nil
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
		} else {

			len0, err := decoder.StartArray(64)
			if err != nil {
				return err
			}
			s.Left = make([]string, len0)
			for i := uint32(0); i < len0; i++ {
				var elem0 string
				pointer, err := decoder.ReadPointer()
				if err != nil {
					return err
				}
				if pointer == 0 {
					return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
				} else {
					value, err := decoder.ReadString()
					if err != nil {
						return err
					}
					elem0 = value
				}
				s.Left[i] = elem0
			}
			if err := decoder.Finish(); err != nil {
				return nil
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
		} else {

			len0, err := decoder.StartArray(64)
			if err != nil {
				return err
			}
			s.Right = make([]string, len0)
			for i := uint32(0); i < len0; i++ {
				var elem0 string
				pointer, err := decoder.ReadPointer()
				if err != nil {
					return err
				}
				if pointer == 0 {
					return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
				} else {
					value, err := decoder.ReadString()
					if err != nil {
						return err
					}
					elem0 = value
				}
				s.Right[i] = elem0
			}
			if err := decoder.Finish(); err != nil {
				return nil
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
		} else {

			len0, err := decoder.StartArray(64)
			if err != nil {
				return err
			}
			s.AtBottom = make([]string, len0)
			for i := uint32(0); i < len0; i++ {
				var elem0 string
				pointer, err := decoder.ReadPointer()
				if err != nil {
					return err
				}
				if pointer == 0 {
					return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
				} else {
					value, err := decoder.ReadString()
					if err != nil {
						return err
					}
					elem0 = value
				}
				s.AtBottom[i] = elem0
			}
			if err := decoder.Finish(); err != nil {
				return nil
			}
		}
	}

	if err := decoder.Finish(); err != nil {
		return err
	}
	return nil
}

type SourceFileInfo struct {
	FileName     string
	LineNumber   uint32
	ColumnNumber uint32
}

func (s *SourceFileInfo) Encode(encoder *bindings.Encoder) error {
	encoder.StartStruct(16, 0)
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	if err := encoder.WriteString(s.FileName); err != nil {
		return err
	}
	if err := encoder.WriteUint32(s.LineNumber); err != nil {
		return err
	}
	if err := encoder.WriteUint32(s.ColumnNumber); err != nil {
		return err
	}

	if err := encoder.Finish(); err != nil {
		return err
	}
	return nil
}

var sourceFileInfo_Versions []bindings.DataHeader = []bindings.DataHeader{
	bindings.DataHeader{24, 0},
}

func (s *SourceFileInfo) Decode(decoder *bindings.Decoder) error {
	header, err := decoder.StartStruct()
	if err != nil {
		return err
	}

	index := sort.Search(len(sourceFileInfo_Versions), func(i int) bool {
		return sourceFileInfo_Versions[i].ElementsOrVersion >= header.ElementsOrVersion
	})
	if index < len(sourceFileInfo_Versions) {
		if sourceFileInfo_Versions[index].ElementsOrVersion > header.ElementsOrVersion {
			index--
		}
		expectedSize := sourceFileInfo_Versions[index].Size
		if expectedSize != header.Size {
			return &bindings.ValidationError{bindings.UnexpectedStructHeader,
				fmt.Sprintf("invalid struct header size: should be %d, but was %d", expectedSize, header.Size),
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
		} else {
			value, err := decoder.ReadString()
			if err != nil {
				return err
			}
			s.FileName = value
		}
	}
	if header.ElementsOrVersion >= 0 {
		value, err := decoder.ReadUint32()
		if err != nil {
			return err
		}
		s.LineNumber = value
	}
	if header.ElementsOrVersion >= 0 {
		value, err := decoder.ReadUint32()
		if err != nil {
			return err
		}
		s.ColumnNumber = value
	}

	if err := decoder.Finish(); err != nil {
		return err
	}
	return nil
}

type ContainedDeclarations struct {
	Enums     *[]string
	Constants *[]string
}

func (s *ContainedDeclarations) Encode(encoder *bindings.Encoder) error {
	encoder.StartStruct(16, 0)
	if s.Enums == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		encoder.StartArray(uint32(len((*s.Enums))), 64)
		for _, elem0 := range *s.Enums {
			if err := encoder.WritePointer(); err != nil {
				return err
			}
			if err := encoder.WriteString(elem0); err != nil {
				return err
			}
		}
		if err := encoder.Finish(); err != nil {
			return err
		}
	}
	if s.Constants == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		encoder.StartArray(uint32(len((*s.Constants))), 64)
		for _, elem0 := range *s.Constants {
			if err := encoder.WritePointer(); err != nil {
				return err
			}
			if err := encoder.WriteString(elem0); err != nil {
				return err
			}
		}
		if err := encoder.Finish(); err != nil {
			return err
		}
	}

	if err := encoder.Finish(); err != nil {
		return err
	}
	return nil
}

var containedDeclarations_Versions []bindings.DataHeader = []bindings.DataHeader{
	bindings.DataHeader{24, 0},
}

func (s *ContainedDeclarations) Decode(decoder *bindings.Decoder) error {
	header, err := decoder.StartStruct()
	if err != nil {
		return err
	}

	index := sort.Search(len(containedDeclarations_Versions), func(i int) bool {
		return containedDeclarations_Versions[i].ElementsOrVersion >= header.ElementsOrVersion
	})
	if index < len(containedDeclarations_Versions) {
		if containedDeclarations_Versions[index].ElementsOrVersion > header.ElementsOrVersion {
			index--
		}
		expectedSize := containedDeclarations_Versions[index].Size
		if expectedSize != header.Size {
			return &bindings.ValidationError{bindings.UnexpectedStructHeader,
				fmt.Sprintf("invalid struct header size: should be %d, but was %d", expectedSize, header.Size),
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			s.Enums = nil
		} else {
			s.Enums = new([]string)
			len0, err := decoder.StartArray(64)
			if err != nil {
				return err
			}
			(*s.Enums) = make([]string, len0)
			for i := uint32(0); i < len0; i++ {
				var elem0 string
				pointer, err := decoder.ReadPointer()
				if err != nil {
					return err
				}
				if pointer == 0 {
					return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
				} else {
					value, err := decoder.ReadString()
					if err != nil {
						return err
					}
					elem0 = value
				}
				(*s.Enums)[i] = elem0
			}
			if err := decoder.Finish(); err != nil {
				return nil
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			s.Constants = nil
		} else {
			s.Constants = new([]string)
			len0, err := decoder.StartArray(64)
			if err != nil {
				return err
			}
			(*s.Constants) = make([]string, len0)
			for i := uint32(0); i < len0; i++ {
				var elem0 string
				pointer, err := decoder.ReadPointer()
				if err != nil {
					return err
				}
				if pointer == 0 {
					return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
				} else {
					value, err := decoder.ReadString()
					if err != nil {
						return err
					}
					elem0 = value
				}
				(*s.Constants)[i] = elem0
			}
			if err := decoder.Finish(); err != nil {
				return nil
			}
		}
	}

	if err := decoder.Finish(); err != nil {
		return err
	}
	return nil
}

type RuntimeTypeInfo struct {
	Services map[string]string
	TypeMap  map[string]UserDefinedType
}

func (s *RuntimeTypeInfo) Encode(encoder *bindings.Encoder) error {
	encoder.StartStruct(16, 0)
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	encoder.StartMap()
	{
		var keys0 []string
		var values0 []string
		for elem0 := range s.Services {
			keys0 = append(keys0, elem0)
		}
		if encoder.Deterministic() {
			bindings.SortMapKeys(&keys0)
		}
		for _, elem0 := range keys0 {
			values0 = append(values0, s.Services[elem0])
		}
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		encoder.StartArray(uint32(len(keys0)), 64)
		for _, elem0 := range keys0 {
			if err := encoder.WritePointer(); err != nil {
				return err
			}
			if err := encoder.WriteString(elem0); err != nil {
				return err
			}
		}
		if err := encoder.Finish(); err != nil {
			return err
		}
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		encoder.StartArray(uint32(len(values0)), 64)
		for _, elem0 := range values0 {
			if err := encoder.WritePointer(); err != nil {
				return err
			}
			if err := encoder.WriteString(elem0); err != nil {
				return err
			}
		}
		if err := encoder.Finish(); err != nil {
			return err
		}
	}
	if err := encoder.Finish(); err != nil {
		return err
	}
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	encoder.StartMap()
	{
		var keys0 []string
		var values0 []UserDefinedType
		for elem0 := range s.TypeMap {
			keys0 = append(keys0, elem0)
		}
		if encoder.Deterministic() {
			bindings.SortMapKeys(&keys0)
		}
		for _, elem0 := range keys0 {
			values0 = append(values0, s.TypeMap[elem0])
		}
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		encoder.StartArray(uint32(len(keys0)), 64)
		for _, elem0 := range keys0 {
			if err := encoder.WritePointer(); err != nil {
				return err
			}
			if err := encoder.WriteString(elem0); err != nil {
				return err
			}
		}
		if err := encoder.Finish(); err != nil {
			return err
		}
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		encoder.StartArray(uint32(len(values0)), 128)
		for _, elem0 := range values0 {
			if elem0 == nil {
				return &bindings.ValidationError{bindings.UnexpectedNullUnion, "unexpected null union"}
			} else {
				if err := elem0.Encode(encoder); err != nil {
					return err
				}
			}
		}
		if err := encoder.Finish(); err != nil {
			return err
		}
	}
	if err := encoder.Finish(); err != nil {
		return err
	}

	if err := encoder.Finish(); err != nil {
		return err
	}
	return nil
}

var runtimeTypeInfo_Versions []bindings.DataHeader = []bindings.DataHeader{
	bindings.DataHeader{24, 0},
}

func (s *RuntimeTypeInfo) Decode(decoder *bindings.Decoder) error {
	header, err := decoder.StartStruct()
	if err != nil {
		return err
	}

	index := sort.Search(len(runtimeTypeInfo_Versions), func(i int) bool {
		return runtimeTypeInfo_Versions[i].ElementsOrVersion >= header.ElementsOrVersion
	})
	if index < len(runtimeTypeInfo_Versions) {
		if runtimeTypeInfo_Versions[index].ElementsOrVersion > header.ElementsOrVersion {
			index--
		}
		expectedSize := runtimeTypeInfo_Versions[index].Size
		if expectedSize != header.Size {
			return &bindings.ValidationError{bindings.UnexpectedStructHeader,
				fmt.Sprintf("invalid struct header size: should be %d, but was %d", expectedSize, header.Size),
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
		} else {

			s.Services = map[string]string{}
			if err := decoder.StartMap(); err != nil {
				return err
			}
			var keys0 []string
			{
				pointer, err := decoder.ReadPointer()
				if err != nil {
					return err
				}
				if pointer == 0 {
					return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
				} else {

					len0, err := decoder.StartArray(64)
					if err != nil {
						return err
					}
					keys0 = make([]string, len0)
					for i := uint32(0); i < len0; i++ {
						var elem0 string
						pointer, err := decoder.ReadPointer()
						if err != nil {
							return err
						}
						if pointer == 0 {
							return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
						} else {
							value, err := decoder.ReadString()
							if err != nil {
								return err
							}
							elem0 = value
						}
						keys0[i] = elem0
					}
					if err := decoder.Finish(); err != nil {
						return nil
					}
				}
			}
			var values0 []string
			{
				pointer, err := decoder.ReadPointer()
				if err != nil {
					return err
				}
				if pointer == 0 {
					return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
				} else {

					len0, err := decoder.StartArray(64)
					if err != nil {
						return err
					}
					values0 = make([]string, len0)
					for i := uint32(0); i < len0; i++ {
						var elem0 string
						pointer, err := decoder.ReadPointer()
						if err != nil {
							return err
						}
						if pointer == 0 {
							return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
						} else {
							value, err := decoder.ReadString()
							if err != nil {
								return err
							}
							elem0 = value
						}
						values0[i] = elem0
					}
					if err := decoder.Finish(); err != nil {
						return nil
					}
				}
			}
			if err := decoder.Finish(); err != nil {
				return nil
			}
			if len(keys0) != len(values0) {
				return &bindings.ValidationError{bindings.DifferentSizedArraysInMap,
					fmt.Sprintf("Number of keys %d is different from number of values %d",
						len(keys0), len(values0))}
			}
			for i := 0; i < len(keys0); i++ {
				s.Services[keys0[i]] = values0[i]
			}
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
		} else {

			s.TypeMap = map[string]UserDefinedType{}
			if err := decoder.StartMap(); err != nil {
				return err
			}
			var keys0 []string
			{
				pointer, err := decoder.ReadPointer()
				if err != nil {
					return err
				}
				if pointer == 0 {
					return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
				} else {

					len0, err := decoder.StartArray(64)
					if err != nil {
						return err
					}
					keys0 = make([]string, len0)
					for i := uint32(0); i < len0; i++ {
						var elem0 string
						pointer, err := decoder.ReadPointer()
						if err != nil {
							return err
						}
						if pointer == 0 {
							return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
						} else {
							value, err := decoder.ReadString()
							if err != nil {
								return err
							}
							elem0 = value
						}
						keys0[i] = elem0
					}
					if err := decoder.Finish(); err != nil {
						return nil
					}
				}
			}
			var values0 []UserDefinedType
			{
				pointer, err := decoder.ReadPointer()
				if err != nil {
					return err
				}
				if pointer == 0 {
					return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
				} else {

					len0, err := decoder.StartArray(128)
					if err != nil {
						return err
					}
					values0 = make([]UserDefinedType, len0)
					for i := uint32(0); i < len0; i++ {
						var elem0 UserDefinedType
						var err error
						elem0, err = DecodeUserDefinedType(decoder)
						if err != nil {
							return err
						}
						if elem0 == nil {
							return &bindings.ValidationError{bindings.UnexpectedNullUnion, "unexpected null union"}
						}
						values0[i] = elem0
					}
					if err := decoder.Finish(); err != nil {
						return nil
					}
				}
			}
			if err := decoder.Finish(); err != nil {
				return nil
			}
			if len(keys0) != len(values0) {
				return &bindings.ValidationError{bindings.DifferentSizedArraysInMap,
					fmt.Sprintf("Number of keys %d is different from number of values %d",
						len(keys0), len(values0))}
			}
			for i := 0; i < len(keys0); i++ {
				s.TypeMap[keys0[i]] = values0[i]
			}
		}
	}

	if err := decoder.Finish(); err != nil {
		return err
	}
	return nil
}

type Type interface {
	Tag() uint32
	Interface() interface{}
	__Reflect(__TypeReflect)
	Encode(encoder *bindings.Encoder) error
}

type __TypeReflect struct {
	SimpleType    SimpleType
	StringType    StringType
	ArrayType     ArrayType
	MapType       MapType
	HandleType    HandleType
	TypeReference TypeReference
}

type TypeUnknown struct{ tag uint32 }

func (u *TypeUnknown) Tag() uint32             { return u.tag }
func (u *TypeUnknown) Interface() interface{}  { return nil }
func (u *TypeUnknown) __Reflect(__TypeReflect) {}
func (u *TypeUnknown) Encode(encoder *bindings.Encoder) error {
	return fmt.Errorf("Trying to serialize an unknown Type. There is no sane way to do that!")
}

type TypeSimpleType struct{ Value SimpleType }

func (u *TypeSimpleType) Tag() uint32             { return 0 }
func (u *TypeSimpleType) Interface() interface{}  { return u.Value }
func (u *TypeSimpleType) __Reflect(__TypeReflect) {}

func (u *TypeSimpleType) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	if err := encoder.WriteInt32(int32(u.Value)); err != nil {
		return err
	}

	encoder.FinishWritingUnionValue()
	return nil
}

func (u *TypeSimpleType) decodeInternal(decoder *bindings.Decoder) error {
	value, err := decoder.ReadInt32()
	if err != nil {
		return err
	}
	u.Value = SimpleType(value)

	return nil
}

type TypeStringType struct{ Value StringType }

func (u *TypeStringType) Tag() uint32             { return 1 }
func (u *TypeStringType) Interface() interface{}  { return u.Value }
func (u *TypeStringType) __Reflect(__TypeReflect) {}

func (u *TypeStringType) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	if err := u.Value.Encode(encoder); err != nil {
		return err
	}

	encoder.FinishWritingUnionValue()
	return nil
}

func (u *TypeStringType) decodeInternal(decoder *bindings.Decoder) error {
	pointer, err := decoder.ReadPointer()
	if err != nil {
		return err
	}
	if pointer == 0 {
		return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
	} else {
		if err := u.Value.Decode(decoder); err != nil {
			return err
		}
	}

	return nil
}

type TypeArrayType struct{ Value ArrayType }

func (u *TypeArrayType) Tag() uint32             { return 2 }
func (u *TypeArrayType) Interface() interface{}  { return u.Value }
func (u *TypeArrayType) __Reflect(__TypeReflect) {}

func (u *TypeArrayType) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	if err := u.Value.Encode(encoder); err != nil {
		return err
	}

	encoder.FinishWritingUnionValue()
	return nil
}

func (u *TypeArrayType) decodeInternal(decoder *bindings.Decoder) error {
	pointer, err := decoder.ReadPointer()
	if err != nil {
		return err
	}
	if pointer == 0 {
		return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
	} else {
		if err := u.Value.Decode(decoder); err != nil {
			return err
		}
	}

	return nil
}

type TypeMapType struct{ Value MapType }

func (u *TypeMapType) Tag() uint32             { return 3 }
func (u *TypeMapType) Interface() interface{}  { return u.Value }
func (u *TypeMapType) __Reflect(__TypeReflect) {}

func (u *TypeMapType) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	if err := u.Value.Encode(encoder); err != nil {
		return err
	}

	encoder.FinishWritingUnionValue()
	return nil
}

func (u *TypeMapType) decodeInternal(decoder *bindings.Decoder) error {
	pointer, err := decoder.ReadPointer()
	if err != nil {
		return err
	}
	if pointer == 0 {
		return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
	} else {
		if err := u.Value.Decode(decoder); err != nil {
			return err
		}
	}

	return nil
}

type TypeHandleType struct{ Value HandleType }

func (u *TypeHandleType) Tag() uint32             { return 4 }
func (u *TypeHandleType) Interface() interface{}  { return u.Value }
func (u *TypeHandleType) __Reflect(__TypeReflect) {}

func (u *TypeHandleType) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	if err := u.Value.Encode(encoder); err != nil {
		return err
	}

	encoder.FinishWritingUnionValue()
	return nil
}

func (u *TypeHandleType) decodeInternal(decoder *bindings.Decoder) error {
	pointer, err := decoder.ReadPointer()
	if err != nil {
		return err
	}
	if pointer == 0 {
		return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
	} else {
		if err := u.Value.Decode(decoder); err != nil {
			return err
		}
	}

	return nil
}

type TypeTypeReference struct{ Value TypeReference }

func (u *TypeTypeReference) Tag() uint32             { return 5 }
func (u *TypeTypeReference) Interface() interface{}  { return u.Value }
func (u *TypeTypeReference) __Reflect(__TypeReflect) {}

func (u *TypeTypeReference) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	if err := u.Value.Encode(encoder); err != nil {
		return err
	}

	encoder.FinishWritingUnionValue()
	return nil
}

func (u *TypeTypeReference) decodeInternal(decoder *bindings.Decoder) error {
	pointer, err := decoder.ReadPointer()
	if err != nil {
		return err
	}
	if pointer == 0 {
		return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
	} else {
		if err := u.Value.Decode(decoder); err != nil {
			return err
		}
	}

	return nil
}

func DecodeType(decoder *bindings.Decoder) (Type, error) {
	size, tag, err := decoder.ReadUnionHeader()
	if err != nil {
		return nil, err
	}

	if size == 0 {
		decoder.SkipUnionValue()
		return nil, nil
	}

	switch tag {
	case 0:
		var value TypeSimpleType
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	case 1:
		var value TypeStringType
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	case 2:
		var value TypeArrayType
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	case 3:
		var value TypeMapType
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	case 4:
		var value TypeHandleType
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	case 5:
		var value TypeTypeReference
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	}

	decoder.SkipUnionValue()
	return &TypeUnknown{tag: tag}, nil
}

type UserDefinedType interface {
	Tag() uint32
	Interface() interface{}
	__Reflect(__UserDefinedTypeReflect)
	Encode(encoder *bindings.Encoder) error
}

type __UserDefinedTypeReflect struct {
	EnumType      FidlEnum
	StructType    FidlStruct
	UnionType     FidlUnion
	InterfaceType FidlInterface
}

type UserDefinedTypeUnknown struct{ tag uint32 }

func (u *UserDefinedTypeUnknown) Tag() uint32                        { return u.tag }
func (u *UserDefinedTypeUnknown) Interface() interface{}             { return nil }
func (u *UserDefinedTypeUnknown) __Reflect(__UserDefinedTypeReflect) {}
func (u *UserDefinedTypeUnknown) Encode(encoder *bindings.Encoder) error {
	return fmt.Errorf("Trying to serialize an unknown UserDefinedType. There is no sane way to do that!")
}

type UserDefinedTypeEnumType struct{ Value FidlEnum }

func (u *UserDefinedTypeEnumType) Tag() uint32                        { return 0 }
func (u *UserDefinedTypeEnumType) Interface() interface{}             { return u.Value }
func (u *UserDefinedTypeEnumType) __Reflect(__UserDefinedTypeReflect) {}

func (u *UserDefinedTypeEnumType) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	if err := u.Value.Encode(encoder); err != nil {
		return err
	}

	encoder.FinishWritingUnionValue()
	return nil
}

func (u *UserDefinedTypeEnumType) decodeInternal(decoder *bindings.Decoder) error {
	pointer, err := decoder.ReadPointer()
	if err != nil {
		return err
	}
	if pointer == 0 {
		return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
	} else {
		if err := u.Value.Decode(decoder); err != nil {
			return err
		}
	}

	return nil
}

type UserDefinedTypeStructType struct{ Value FidlStruct }

func (u *UserDefinedTypeStructType) Tag() uint32                        { return 1 }
func (u *UserDefinedTypeStructType) Interface() interface{}             { return u.Value }
func (u *UserDefinedTypeStructType) __Reflect(__UserDefinedTypeReflect) {}

func (u *UserDefinedTypeStructType) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	if err := u.Value.Encode(encoder); err != nil {
		return err
	}

	encoder.FinishWritingUnionValue()
	return nil
}

func (u *UserDefinedTypeStructType) decodeInternal(decoder *bindings.Decoder) error {
	pointer, err := decoder.ReadPointer()
	if err != nil {
		return err
	}
	if pointer == 0 {
		return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
	} else {
		if err := u.Value.Decode(decoder); err != nil {
			return err
		}
	}

	return nil
}

type UserDefinedTypeUnionType struct{ Value FidlUnion }

func (u *UserDefinedTypeUnionType) Tag() uint32                        { return 2 }
func (u *UserDefinedTypeUnionType) Interface() interface{}             { return u.Value }
func (u *UserDefinedTypeUnionType) __Reflect(__UserDefinedTypeReflect) {}

func (u *UserDefinedTypeUnionType) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	if err := u.Value.Encode(encoder); err != nil {
		return err
	}

	encoder.FinishWritingUnionValue()
	return nil
}

func (u *UserDefinedTypeUnionType) decodeInternal(decoder *bindings.Decoder) error {
	pointer, err := decoder.ReadPointer()
	if err != nil {
		return err
	}
	if pointer == 0 {
		return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
	} else {
		if err := u.Value.Decode(decoder); err != nil {
			return err
		}
	}

	return nil
}

type UserDefinedTypeInterfaceType struct{ Value FidlInterface }

func (u *UserDefinedTypeInterfaceType) Tag() uint32                        { return 3 }
func (u *UserDefinedTypeInterfaceType) Interface() interface{}             { return u.Value }
func (u *UserDefinedTypeInterfaceType) __Reflect(__UserDefinedTypeReflect) {}

func (u *UserDefinedTypeInterfaceType) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	if err := u.Value.Encode(encoder); err != nil {
		return err
	}

	encoder.FinishWritingUnionValue()
	return nil
}

func (u *UserDefinedTypeInterfaceType) decodeInternal(decoder *bindings.Decoder) error {
	pointer, err := decoder.ReadPointer()
	if err != nil {
		return err
	}
	if pointer == 0 {
		return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
	} else {
		if err := u.Value.Decode(decoder); err != nil {
			return err
		}
	}

	return nil
}

func DecodeUserDefinedType(decoder *bindings.Decoder) (UserDefinedType, error) {
	size, tag, err := decoder.ReadUnionHeader()
	if err != nil {
		return nil, err
	}

	if size == 0 {
		decoder.SkipUnionValue()
		return nil, nil
	}

	switch tag {
	case 0:
		var value UserDefinedTypeEnumType
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	case 1:
		var value UserDefinedTypeStructType
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	case 2:
		var value UserDefinedTypeUnionType
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	case 3:
		var value UserDefinedTypeInterfaceType
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	}

	decoder.SkipUnionValue()
	return &UserDefinedTypeUnknown{tag: tag}, nil
}

type DefaultFieldValue interface {
	Tag() uint32
	Interface() interface{}
	__Reflect(__DefaultFieldValueReflect)
	Encode(encoder *bindings.Encoder) error
}

type __DefaultFieldValueReflect struct {
	Value          Value
	DefaultKeyword DefaultKeyword
}

type DefaultFieldValueUnknown struct{ tag uint32 }

func (u *DefaultFieldValueUnknown) Tag() uint32                          { return u.tag }
func (u *DefaultFieldValueUnknown) Interface() interface{}               { return nil }
func (u *DefaultFieldValueUnknown) __Reflect(__DefaultFieldValueReflect) {}
func (u *DefaultFieldValueUnknown) Encode(encoder *bindings.Encoder) error {
	return fmt.Errorf("Trying to serialize an unknown DefaultFieldValue. There is no sane way to do that!")
}

type DefaultFieldValueValue struct{ Value Value }

func (u *DefaultFieldValueValue) Tag() uint32                          { return 0 }
func (u *DefaultFieldValueValue) Interface() interface{}               { return u.Value }
func (u *DefaultFieldValueValue) __Reflect(__DefaultFieldValueReflect) {}

func (u *DefaultFieldValueValue) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	encoder.StartNestedUnion()
	if u.Value == nil {
		return &bindings.ValidationError{bindings.UnexpectedNullUnion, "unexpected null union"}
	} else {
		if err := u.Value.Encode(encoder); err != nil {
			return err
		}
	}
	encoder.Finish()

	encoder.FinishWritingUnionValue()
	return nil
}

func (u *DefaultFieldValueValue) decodeInternal(decoder *bindings.Decoder) error {
	pointer, err := decoder.ReadPointer()
	if err != nil {
		return err
	}
	if pointer == 0 {
		return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null union pointer"}
	} else {
		if err := decoder.StartNestedUnion(); err != nil {
			return err
		}
		var err error
		u.Value, err = DecodeValue(decoder)
		if err != nil {
			return err
		}
		if u.Value == nil {
			return &bindings.ValidationError{bindings.UnexpectedNullUnion, "unexpected null union"}
		}
		decoder.Finish()
	}

	return nil
}

type DefaultFieldValueDefaultKeyword struct{ Value DefaultKeyword }

func (u *DefaultFieldValueDefaultKeyword) Tag() uint32                          { return 1 }
func (u *DefaultFieldValueDefaultKeyword) Interface() interface{}               { return u.Value }
func (u *DefaultFieldValueDefaultKeyword) __Reflect(__DefaultFieldValueReflect) {}

func (u *DefaultFieldValueDefaultKeyword) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	if err := u.Value.Encode(encoder); err != nil {
		return err
	}

	encoder.FinishWritingUnionValue()
	return nil
}

func (u *DefaultFieldValueDefaultKeyword) decodeInternal(decoder *bindings.Decoder) error {
	pointer, err := decoder.ReadPointer()
	if err != nil {
		return err
	}
	if pointer == 0 {
		return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
	} else {
		if err := u.Value.Decode(decoder); err != nil {
			return err
		}
	}

	return nil
}

func DecodeDefaultFieldValue(decoder *bindings.Decoder) (DefaultFieldValue, error) {
	size, tag, err := decoder.ReadUnionHeader()
	if err != nil {
		return nil, err
	}

	if size == 0 {
		decoder.SkipUnionValue()
		return nil, nil
	}

	switch tag {
	case 0:
		var value DefaultFieldValueValue
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	case 1:
		var value DefaultFieldValueDefaultKeyword
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	}

	decoder.SkipUnionValue()
	return &DefaultFieldValueUnknown{tag: tag}, nil
}

type Value interface {
	Tag() uint32
	Interface() interface{}
	__Reflect(__ValueReflect)
	Encode(encoder *bindings.Encoder) error
}

type __ValueReflect struct {
	LiteralValue       LiteralValue
	ConstantReference  ConstantReference
	EnumValueReference EnumValueReference
	BuiltinValue       BuiltinConstantValue
}

type ValueUnknown struct{ tag uint32 }

func (u *ValueUnknown) Tag() uint32              { return u.tag }
func (u *ValueUnknown) Interface() interface{}   { return nil }
func (u *ValueUnknown) __Reflect(__ValueReflect) {}
func (u *ValueUnknown) Encode(encoder *bindings.Encoder) error {
	return fmt.Errorf("Trying to serialize an unknown Value. There is no sane way to do that!")
}

type ValueLiteralValue struct{ Value LiteralValue }

func (u *ValueLiteralValue) Tag() uint32              { return 0 }
func (u *ValueLiteralValue) Interface() interface{}   { return u.Value }
func (u *ValueLiteralValue) __Reflect(__ValueReflect) {}

func (u *ValueLiteralValue) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	encoder.StartNestedUnion()
	if u.Value == nil {
		return &bindings.ValidationError{bindings.UnexpectedNullUnion, "unexpected null union"}
	} else {
		if err := u.Value.Encode(encoder); err != nil {
			return err
		}
	}
	encoder.Finish()

	encoder.FinishWritingUnionValue()
	return nil
}

func (u *ValueLiteralValue) decodeInternal(decoder *bindings.Decoder) error {
	pointer, err := decoder.ReadPointer()
	if err != nil {
		return err
	}
	if pointer == 0 {
		return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null union pointer"}
	} else {
		if err := decoder.StartNestedUnion(); err != nil {
			return err
		}
		var err error
		u.Value, err = DecodeLiteralValue(decoder)
		if err != nil {
			return err
		}
		if u.Value == nil {
			return &bindings.ValidationError{bindings.UnexpectedNullUnion, "unexpected null union"}
		}
		decoder.Finish()
	}

	return nil
}

type ValueConstantReference struct{ Value ConstantReference }

func (u *ValueConstantReference) Tag() uint32              { return 1 }
func (u *ValueConstantReference) Interface() interface{}   { return u.Value }
func (u *ValueConstantReference) __Reflect(__ValueReflect) {}

func (u *ValueConstantReference) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	if err := u.Value.Encode(encoder); err != nil {
		return err
	}

	encoder.FinishWritingUnionValue()
	return nil
}

func (u *ValueConstantReference) decodeInternal(decoder *bindings.Decoder) error {
	pointer, err := decoder.ReadPointer()
	if err != nil {
		return err
	}
	if pointer == 0 {
		return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
	} else {
		if err := u.Value.Decode(decoder); err != nil {
			return err
		}
	}

	return nil
}

type ValueEnumValueReference struct{ Value EnumValueReference }

func (u *ValueEnumValueReference) Tag() uint32              { return 2 }
func (u *ValueEnumValueReference) Interface() interface{}   { return u.Value }
func (u *ValueEnumValueReference) __Reflect(__ValueReflect) {}

func (u *ValueEnumValueReference) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	if err := u.Value.Encode(encoder); err != nil {
		return err
	}

	encoder.FinishWritingUnionValue()
	return nil
}

func (u *ValueEnumValueReference) decodeInternal(decoder *bindings.Decoder) error {
	pointer, err := decoder.ReadPointer()
	if err != nil {
		return err
	}
	if pointer == 0 {
		return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
	} else {
		if err := u.Value.Decode(decoder); err != nil {
			return err
		}
	}

	return nil
}

type ValueBuiltinValue struct{ Value BuiltinConstantValue }

func (u *ValueBuiltinValue) Tag() uint32              { return 3 }
func (u *ValueBuiltinValue) Interface() interface{}   { return u.Value }
func (u *ValueBuiltinValue) __Reflect(__ValueReflect) {}

func (u *ValueBuiltinValue) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	if err := encoder.WriteInt32(int32(u.Value)); err != nil {
		return err
	}

	encoder.FinishWritingUnionValue()
	return nil
}

func (u *ValueBuiltinValue) decodeInternal(decoder *bindings.Decoder) error {
	value, err := decoder.ReadInt32()
	if err != nil {
		return err
	}
	u.Value = BuiltinConstantValue(value)

	return nil
}

func DecodeValue(decoder *bindings.Decoder) (Value, error) {
	size, tag, err := decoder.ReadUnionHeader()
	if err != nil {
		return nil, err
	}

	if size == 0 {
		decoder.SkipUnionValue()
		return nil, nil
	}

	switch tag {
	case 0:
		var value ValueLiteralValue
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	case 1:
		var value ValueConstantReference
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	case 2:
		var value ValueEnumValueReference
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	case 3:
		var value ValueBuiltinValue
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	}

	decoder.SkipUnionValue()
	return &ValueUnknown{tag: tag}, nil
}

type LiteralValue interface {
	Tag() uint32
	Interface() interface{}
	__Reflect(__LiteralValueReflect)
	Encode(encoder *bindings.Encoder) error
}

type __LiteralValueReflect struct {
	BoolValue   bool
	DoubleValue float64
	FloatValue  float32
	Int8Value   int8
	Int16Value  int16
	Int32Value  int32
	Int64Value  int64
	StringValue string
	Uint8Value  uint8
	Uint16Value uint16
	Uint32Value uint32
	Uint64Value uint64
}

type LiteralValueUnknown struct{ tag uint32 }

func (u *LiteralValueUnknown) Tag() uint32                     { return u.tag }
func (u *LiteralValueUnknown) Interface() interface{}          { return nil }
func (u *LiteralValueUnknown) __Reflect(__LiteralValueReflect) {}
func (u *LiteralValueUnknown) Encode(encoder *bindings.Encoder) error {
	return fmt.Errorf("Trying to serialize an unknown LiteralValue. There is no sane way to do that!")
}

type LiteralValueBoolValue struct{ Value bool }

func (u *LiteralValueBoolValue) Tag() uint32                     { return 0 }
func (u *LiteralValueBoolValue) Interface() interface{}          { return u.Value }
func (u *LiteralValueBoolValue) __Reflect(__LiteralValueReflect) {}

func (u *LiteralValueBoolValue) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	if err := encoder.WriteBool(u.Value); err != nil {
		return err
	}

	encoder.FinishWritingUnionValue()
	return nil
}

func (u *LiteralValueBoolValue) decodeInternal(decoder *bindings.Decoder) error {
	value, err := decoder.ReadBool()
	if err != nil {
		return err
	}
	u.Value = value

	return nil
}

type LiteralValueDoubleValue struct{ Value float64 }

func (u *LiteralValueDoubleValue) Tag() uint32                     { return 1 }
func (u *LiteralValueDoubleValue) Interface() interface{}          { return u.Value }
func (u *LiteralValueDoubleValue) __Reflect(__LiteralValueReflect) {}

func (u *LiteralValueDoubleValue) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	if err := encoder.WriteFloat64(u.Value); err != nil {
		return err
	}

	encoder.FinishWritingUnionValue()
	return nil
}

func (u *LiteralValueDoubleValue) decodeInternal(decoder *bindings.Decoder) error {
	value, err := decoder.ReadFloat64()
	if err != nil {
		return err
	}
	u.Value = value

	return nil
}

type LiteralValueFloatValue struct{ Value float32 }

func (u *LiteralValueFloatValue) Tag() uint32                     { return 2 }
func (u *LiteralValueFloatValue) Interface() interface{}          { return u.Value }
func (u *LiteralValueFloatValue) __Reflect(__LiteralValueReflect) {}

func (u *LiteralValueFloatValue) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	if err := encoder.WriteFloat32(u.Value); err != nil {
		return err
	}

	encoder.FinishWritingUnionValue()
	return nil
}

func (u *LiteralValueFloatValue) decodeInternal(decoder *bindings.Decoder) error {
	value, err := decoder.ReadFloat32()
	if err != nil {
		return err
	}
	u.Value = value

	return nil
}

type LiteralValueInt8Value struct{ Value int8 }

func (u *LiteralValueInt8Value) Tag() uint32                     { return 3 }
func (u *LiteralValueInt8Value) Interface() interface{}          { return u.Value }
func (u *LiteralValueInt8Value) __Reflect(__LiteralValueReflect) {}

func (u *LiteralValueInt8Value) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	if err := encoder.WriteInt8(u.Value); err != nil {
		return err
	}

	encoder.FinishWritingUnionValue()
	return nil
}

func (u *LiteralValueInt8Value) decodeInternal(decoder *bindings.Decoder) error {
	value, err := decoder.ReadInt8()
	if err != nil {
		return err
	}
	u.Value = value

	return nil
}

type LiteralValueInt16Value struct{ Value int16 }

func (u *LiteralValueInt16Value) Tag() uint32                     { return 4 }
func (u *LiteralValueInt16Value) Interface() interface{}          { return u.Value }
func (u *LiteralValueInt16Value) __Reflect(__LiteralValueReflect) {}

func (u *LiteralValueInt16Value) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	if err := encoder.WriteInt16(u.Value); err != nil {
		return err
	}

	encoder.FinishWritingUnionValue()
	return nil
}

func (u *LiteralValueInt16Value) decodeInternal(decoder *bindings.Decoder) error {
	value, err := decoder.ReadInt16()
	if err != nil {
		return err
	}
	u.Value = value

	return nil
}

type LiteralValueInt32Value struct{ Value int32 }

func (u *LiteralValueInt32Value) Tag() uint32                     { return 5 }
func (u *LiteralValueInt32Value) Interface() interface{}          { return u.Value }
func (u *LiteralValueInt32Value) __Reflect(__LiteralValueReflect) {}

func (u *LiteralValueInt32Value) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	if err := encoder.WriteInt32(u.Value); err != nil {
		return err
	}

	encoder.FinishWritingUnionValue()
	return nil
}

func (u *LiteralValueInt32Value) decodeInternal(decoder *bindings.Decoder) error {
	value, err := decoder.ReadInt32()
	if err != nil {
		return err
	}
	u.Value = value

	return nil
}

type LiteralValueInt64Value struct{ Value int64 }

func (u *LiteralValueInt64Value) Tag() uint32                     { return 6 }
func (u *LiteralValueInt64Value) Interface() interface{}          { return u.Value }
func (u *LiteralValueInt64Value) __Reflect(__LiteralValueReflect) {}

func (u *LiteralValueInt64Value) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	if err := encoder.WriteInt64(u.Value); err != nil {
		return err
	}

	encoder.FinishWritingUnionValue()
	return nil
}

func (u *LiteralValueInt64Value) decodeInternal(decoder *bindings.Decoder) error {
	value, err := decoder.ReadInt64()
	if err != nil {
		return err
	}
	u.Value = value

	return nil
}

type LiteralValueStringValue struct{ Value string }

func (u *LiteralValueStringValue) Tag() uint32                     { return 7 }
func (u *LiteralValueStringValue) Interface() interface{}          { return u.Value }
func (u *LiteralValueStringValue) __Reflect(__LiteralValueReflect) {}

func (u *LiteralValueStringValue) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	if err := encoder.WriteString(u.Value); err != nil {
		return err
	}

	encoder.FinishWritingUnionValue()
	return nil
}

func (u *LiteralValueStringValue) decodeInternal(decoder *bindings.Decoder) error {
	pointer, err := decoder.ReadPointer()
	if err != nil {
		return err
	}
	if pointer == 0 {
		return &bindings.ValidationError{bindings.UnexpectedNullPointer, "unexpected null pointer"}
	} else {
		value, err := decoder.ReadString()
		if err != nil {
			return err
		}
		u.Value = value
	}

	return nil
}

type LiteralValueUint8Value struct{ Value uint8 }

func (u *LiteralValueUint8Value) Tag() uint32                     { return 8 }
func (u *LiteralValueUint8Value) Interface() interface{}          { return u.Value }
func (u *LiteralValueUint8Value) __Reflect(__LiteralValueReflect) {}

func (u *LiteralValueUint8Value) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	if err := encoder.WriteUint8(u.Value); err != nil {
		return err
	}

	encoder.FinishWritingUnionValue()
	return nil
}

func (u *LiteralValueUint8Value) decodeInternal(decoder *bindings.Decoder) error {
	value, err := decoder.ReadUint8()
	if err != nil {
		return err
	}
	u.Value = value

	return nil
}

type LiteralValueUint16Value struct{ Value uint16 }

func (u *LiteralValueUint16Value) Tag() uint32                     { return 9 }
func (u *LiteralValueUint16Value) Interface() interface{}          { return u.Value }
func (u *LiteralValueUint16Value) __Reflect(__LiteralValueReflect) {}

func (u *LiteralValueUint16Value) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	if err := encoder.WriteUint16(u.Value); err != nil {
		return err
	}

	encoder.FinishWritingUnionValue()
	return nil
}

func (u *LiteralValueUint16Value) decodeInternal(decoder *bindings.Decoder) error {
	value, err := decoder.ReadUint16()
	if err != nil {
		return err
	}
	u.Value = value

	return nil
}

type LiteralValueUint32Value struct{ Value uint32 }

func (u *LiteralValueUint32Value) Tag() uint32                     { return 10 }
func (u *LiteralValueUint32Value) Interface() interface{}          { return u.Value }
func (u *LiteralValueUint32Value) __Reflect(__LiteralValueReflect) {}

func (u *LiteralValueUint32Value) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	if err := encoder.WriteUint32(u.Value); err != nil {
		return err
	}

	encoder.FinishWritingUnionValue()
	return nil
}

func (u *LiteralValueUint32Value) decodeInternal(decoder *bindings.Decoder) error {
	value, err := decoder.ReadUint32()
	if err != nil {
		return err
	}
	u.Value = value

	return nil
}

type LiteralValueUint64Value struct{ Value uint64 }

func (u *LiteralValueUint64Value) Tag() uint32                     { return 11 }
func (u *LiteralValueUint64Value) Interface() interface{}          { return u.Value }
func (u *LiteralValueUint64Value) __Reflect(__LiteralValueReflect) {}

func (u *LiteralValueUint64Value) Encode(encoder *bindings.Encoder) error {
	encoder.WriteUnionHeader(u.Tag())
	if err := encoder.WriteUint64(u.Value); err != nil {
		return err
	}

	encoder.FinishWritingUnionValue()
	return nil
}

func (u *LiteralValueUint64Value) decodeInternal(decoder *bindings.Decoder) error {
	value, err := decoder.ReadUint64()
	if err != nil {
		return err
	}
	u.Value = value

	return nil
}

func DecodeLiteralValue(decoder *bindings.Decoder) (LiteralValue, error) {
	size, tag, err := decoder.ReadUnionHeader()
	if err != nil {
		return nil, err
	}

	if size == 0 {
		decoder.SkipUnionValue()
		return nil, nil
	}

	switch tag {
	case 0:
		var value LiteralValueBoolValue
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	case 1:
		var value LiteralValueDoubleValue
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	case 2:
		var value LiteralValueFloatValue
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	case 3:
		var value LiteralValueInt8Value
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	case 4:
		var value LiteralValueInt16Value
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	case 5:
		var value LiteralValueInt32Value
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	case 6:
		var value LiteralValueInt64Value
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	case 7:
		var value LiteralValueStringValue
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	case 8:
		var value LiteralValueUint8Value
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	case 9:
		var value LiteralValueUint16Value
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	case 10:
		var value LiteralValueUint32Value
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	case 11:
		var value LiteralValueUint64Value
		if err := value.decodeInternal(decoder); err != nil {
			return nil, err
		}
		decoder.FinishReadingUnionValue()
		return &value, nil
	}

	decoder.SkipUnionValue()
	return &LiteralValueUnknown{tag: tag}, nil
}

type SimpleType int32

const (
	SimpleType_Bool   SimpleType = 0
	SimpleType_Double SimpleType = 1
	SimpleType_Float  SimpleType = 2
	SimpleType_Int8   SimpleType = 3
	SimpleType_Int16  SimpleType = 4
	SimpleType_Int32  SimpleType = 5
	SimpleType_Int64  SimpleType = 6
	SimpleType_Uint8  SimpleType = 7
	SimpleType_Uint16 SimpleType = 8
	SimpleType_Uint32 SimpleType = 9
	SimpleType_Uint64 SimpleType = 10
)

type BuiltinConstantValue int32

const (
	BuiltinConstantValue_DoubleInfinity         BuiltinConstantValue = 0
	BuiltinConstantValue_DoubleNegativeInfinity BuiltinConstantValue = 1
	BuiltinConstantValue_DoubleNan              BuiltinConstantValue = 2
	BuiltinConstantValue_FloatInfinity          BuiltinConstantValue = 3
	BuiltinConstantValue_FloatNegativeInfinity  BuiltinConstantValue = 4
	BuiltinConstantValue_FloatNan               BuiltinConstantValue = 5
)
