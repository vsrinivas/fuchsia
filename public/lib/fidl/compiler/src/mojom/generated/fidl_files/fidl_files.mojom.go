package fidl_files

import (
	fmt "fmt"
	bindings "mojo/public/go/bindings"
	fidl_types "mojom/generated/fidl_types"
	sort "sort"
)

type FidlFile struct {
	FileName                  string
	SpecifiedFileName         *string
	ModuleNamespace           *string
	Attributes                *[]fidl_types.Attribute
	Imports                   *[]string
	DeclaredFidlObjects       KeysByType
	SerializedRuntimeTypeInfo *string
	Comments                  *fidl_types.Comments
}

func (s *FidlFile) Encode(encoder *bindings.Encoder) error {
	encoder.StartStruct(64, 0)
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	if err := encoder.WriteString(s.FileName); err != nil {
		return err
	}
	if s.SpecifiedFileName == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		if err := encoder.WriteString((*s.SpecifiedFileName)); err != nil {
			return err
		}
	}
	if s.ModuleNamespace == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		if err := encoder.WriteString((*s.ModuleNamespace)); err != nil {
			return err
		}
	}
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
	if s.Imports == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		encoder.StartArray(uint32(len((*s.Imports))), 64)
		for _, elem0 := range *s.Imports {
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
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	if err := s.DeclaredFidlObjects.Encode(encoder); err != nil {
		return err
	}
	if s.SerializedRuntimeTypeInfo == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		if err := encoder.WriteString((*s.SerializedRuntimeTypeInfo)); err != nil {
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

var fidlFile_Versions []bindings.DataHeader = []bindings.DataHeader{
	bindings.DataHeader{72, 0},
}

func (s *FidlFile) Decode(decoder *bindings.Decoder) error {
	header, err := decoder.StartStruct()
	if err != nil {
		return err
	}

	index := sort.Search(len(fidlFile_Versions), func(i int) bool {
		return fidlFile_Versions[i].ElementsOrVersion >= header.ElementsOrVersion
	})
	if index < len(fidlFile_Versions) {
		if fidlFile_Versions[index].ElementsOrVersion > header.ElementsOrVersion {
			index--
		}
		expectedSize := fidlFile_Versions[index].Size
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
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			s.SpecifiedFileName = nil
		} else {
			value, err := decoder.ReadString()
			if err != nil {
				return err
			}
			s.SpecifiedFileName = &value
		}
	}
	if header.ElementsOrVersion >= 0 {
		pointer, err := decoder.ReadPointer()
		if err != nil {
			return err
		}
		if pointer == 0 {
			s.ModuleNamespace = nil
		} else {
			value, err := decoder.ReadString()
			if err != nil {
				return err
			}
			s.ModuleNamespace = &value
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
			s.Attributes = new([]fidl_types.Attribute)
			len0, err := decoder.StartArray(64)
			if err != nil {
				return err
			}
			(*s.Attributes) = make([]fidl_types.Attribute, len0)
			for i := uint32(0); i < len0; i++ {
				var elem0 fidl_types.Attribute
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
			s.Imports = nil
		} else {
			s.Imports = new([]string)
			len0, err := decoder.StartArray(64)
			if err != nil {
				return err
			}
			(*s.Imports) = make([]string, len0)
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
				(*s.Imports)[i] = elem0
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
			if err := s.DeclaredFidlObjects.Decode(decoder); err != nil {
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
			s.SerializedRuntimeTypeInfo = nil
		} else {
			value, err := decoder.ReadString()
			if err != nil {
				return err
			}
			s.SerializedRuntimeTypeInfo = &value
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
			s.Comments = new(fidl_types.Comments)
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

type FidlFileGraph struct {
	Files             map[string]FidlFile
	ResolvedTypes     map[string]fidl_types.UserDefinedType
	ResolvedConstants map[string]fidl_types.DeclaredConstant
}

func (s *FidlFileGraph) Encode(encoder *bindings.Encoder) error {
	encoder.StartStruct(24, 0)
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	encoder.StartMap()
	{
		var keys0 []string
		var values0 []FidlFile
		for elem0 := range s.Files {
			keys0 = append(keys0, elem0)
		}
		if encoder.Deterministic() {
			bindings.SortMapKeys(&keys0)
		}
		for _, elem0 := range keys0 {
			values0 = append(values0, s.Files[elem0])
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
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	encoder.StartMap()
	{
		var keys0 []string
		var values0 []fidl_types.UserDefinedType
		for elem0 := range s.ResolvedTypes {
			keys0 = append(keys0, elem0)
		}
		if encoder.Deterministic() {
			bindings.SortMapKeys(&keys0)
		}
		for _, elem0 := range keys0 {
			values0 = append(values0, s.ResolvedTypes[elem0])
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
	if err := encoder.WritePointer(); err != nil {
		return err
	}
	encoder.StartMap()
	{
		var keys0 []string
		var values0 []fidl_types.DeclaredConstant
		for elem0 := range s.ResolvedConstants {
			keys0 = append(keys0, elem0)
		}
		if encoder.Deterministic() {
			bindings.SortMapKeys(&keys0)
		}
		for _, elem0 := range keys0 {
			values0 = append(values0, s.ResolvedConstants[elem0])
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

	if err := encoder.Finish(); err != nil {
		return err
	}
	return nil
}

var fidlFileGraph_Versions []bindings.DataHeader = []bindings.DataHeader{
	bindings.DataHeader{32, 0},
}

func (s *FidlFileGraph) Decode(decoder *bindings.Decoder) error {
	header, err := decoder.StartStruct()
	if err != nil {
		return err
	}

	index := sort.Search(len(fidlFileGraph_Versions), func(i int) bool {
		return fidlFileGraph_Versions[i].ElementsOrVersion >= header.ElementsOrVersion
	})
	if index < len(fidlFileGraph_Versions) {
		if fidlFileGraph_Versions[index].ElementsOrVersion > header.ElementsOrVersion {
			index--
		}
		expectedSize := fidlFileGraph_Versions[index].Size
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

			s.Files = map[string]FidlFile{}
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
			var values0 []FidlFile
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
					values0 = make([]FidlFile, len0)
					for i := uint32(0); i < len0; i++ {
						var elem0 FidlFile
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
				s.Files[keys0[i]] = values0[i]
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

			s.ResolvedTypes = map[string]fidl_types.UserDefinedType{}
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
			var values0 []fidl_types.UserDefinedType
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
					values0 = make([]fidl_types.UserDefinedType, len0)
					for i := uint32(0); i < len0; i++ {
						var elem0 fidl_types.UserDefinedType
						var err error
						elem0, err = fidl_types.DecodeUserDefinedType(decoder)
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
				s.ResolvedTypes[keys0[i]] = values0[i]
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

			s.ResolvedConstants = map[string]fidl_types.DeclaredConstant{}
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
			var values0 []fidl_types.DeclaredConstant
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
					values0 = make([]fidl_types.DeclaredConstant, len0)
					for i := uint32(0); i < len0; i++ {
						var elem0 fidl_types.DeclaredConstant
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
				s.ResolvedConstants[keys0[i]] = values0[i]
			}
		}
	}

	if err := decoder.Finish(); err != nil {
		return err
	}
	return nil
}

type KeysByType struct {
	Interfaces        *[]string
	Structs           *[]string
	Unions            *[]string
	TopLevelEnums     *[]string
	EmbeddedEnums     *[]string
	TopLevelConstants *[]string
	EmbeddedConstants *[]string
}

func (s *KeysByType) Encode(encoder *bindings.Encoder) error {
	encoder.StartStruct(56, 0)
	if s.Interfaces == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		encoder.StartArray(uint32(len((*s.Interfaces))), 64)
		for _, elem0 := range *s.Interfaces {
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
	if s.Structs == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		encoder.StartArray(uint32(len((*s.Structs))), 64)
		for _, elem0 := range *s.Structs {
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
	if s.Unions == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		encoder.StartArray(uint32(len((*s.Unions))), 64)
		for _, elem0 := range *s.Unions {
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
	if s.TopLevelEnums == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		encoder.StartArray(uint32(len((*s.TopLevelEnums))), 64)
		for _, elem0 := range *s.TopLevelEnums {
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
	if s.EmbeddedEnums == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		encoder.StartArray(uint32(len((*s.EmbeddedEnums))), 64)
		for _, elem0 := range *s.EmbeddedEnums {
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
	if s.TopLevelConstants == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		encoder.StartArray(uint32(len((*s.TopLevelConstants))), 64)
		for _, elem0 := range *s.TopLevelConstants {
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
	if s.EmbeddedConstants == nil {
		encoder.WriteNullPointer()
	} else {
		if err := encoder.WritePointer(); err != nil {
			return err
		}
		encoder.StartArray(uint32(len((*s.EmbeddedConstants))), 64)
		for _, elem0 := range *s.EmbeddedConstants {
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

var keysByType_Versions []bindings.DataHeader = []bindings.DataHeader{
	bindings.DataHeader{64, 0},
}

func (s *KeysByType) Decode(decoder *bindings.Decoder) error {
	header, err := decoder.StartStruct()
	if err != nil {
		return err
	}

	index := sort.Search(len(keysByType_Versions), func(i int) bool {
		return keysByType_Versions[i].ElementsOrVersion >= header.ElementsOrVersion
	})
	if index < len(keysByType_Versions) {
		if keysByType_Versions[index].ElementsOrVersion > header.ElementsOrVersion {
			index--
		}
		expectedSize := keysByType_Versions[index].Size
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
			s.Interfaces = nil
		} else {
			s.Interfaces = new([]string)
			len0, err := decoder.StartArray(64)
			if err != nil {
				return err
			}
			(*s.Interfaces) = make([]string, len0)
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
				(*s.Interfaces)[i] = elem0
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
			s.Structs = nil
		} else {
			s.Structs = new([]string)
			len0, err := decoder.StartArray(64)
			if err != nil {
				return err
			}
			(*s.Structs) = make([]string, len0)
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
				(*s.Structs)[i] = elem0
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
			s.Unions = nil
		} else {
			s.Unions = new([]string)
			len0, err := decoder.StartArray(64)
			if err != nil {
				return err
			}
			(*s.Unions) = make([]string, len0)
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
				(*s.Unions)[i] = elem0
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
			s.TopLevelEnums = nil
		} else {
			s.TopLevelEnums = new([]string)
			len0, err := decoder.StartArray(64)
			if err != nil {
				return err
			}
			(*s.TopLevelEnums) = make([]string, len0)
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
				(*s.TopLevelEnums)[i] = elem0
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
			s.EmbeddedEnums = nil
		} else {
			s.EmbeddedEnums = new([]string)
			len0, err := decoder.StartArray(64)
			if err != nil {
				return err
			}
			(*s.EmbeddedEnums) = make([]string, len0)
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
				(*s.EmbeddedEnums)[i] = elem0
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
			s.TopLevelConstants = nil
		} else {
			s.TopLevelConstants = new([]string)
			len0, err := decoder.StartArray(64)
			if err != nil {
				return err
			}
			(*s.TopLevelConstants) = make([]string, len0)
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
				(*s.TopLevelConstants)[i] = elem0
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
			s.EmbeddedConstants = nil
		} else {
			s.EmbeddedConstants = new([]string)
			len0, err := decoder.StartArray(64)
			if err != nil {
				return err
			}
			(*s.EmbeddedConstants) = make([]string, len0)
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
				(*s.EmbeddedConstants)[i] = elem0
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
