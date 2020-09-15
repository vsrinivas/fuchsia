// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"io"
	"os"
	"path/filepath"
	"text/template"

	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/cpp"
)

type Generator struct {
	tmpls *template.Template
}

type TypedArgument struct {
	ArgumentName  string
	ArgumentValue string
	ArgumentType  cpp.Type
	Pointer       bool
	Nullable      bool
	Access        bool
	MutableAccess bool
}

func NewGenerator() *Generator {
	tmpls := template.New("LLCPPTemplates").Funcs(template.FuncMap{
		"Kinds": func() interface{} { return cpp.Kinds },
		"Eq":    func(a interface{}, b interface{}) bool { return a == b },
		"MethodsHaveReqOrResp": func(ms []cpp.Method) string {
			for _, m := range ms {
				if (m.HasRequest && len(m.Request) != 0) || (m.HasResponse && len(m.Response) != 0) {
					return "\n"
				}
			}
			return ""
		},
		"FilterMethodsWithReqs": func(ms []cpp.Method) []cpp.Method {
			var out []cpp.Method
			for _, m := range ms {
				if !m.HasRequest {
					out = append(out, m)
				}
			}
			return out
		},
		"HasMethodWithReqs": func(ms []cpp.Method) bool {
			for _, m := range ms {
				if m.HasRequest {
					return true
				}
			}
			return false
		},
		"FilterMethodsWithoutReqs": func(ms []cpp.Method) []cpp.Method {
			var out []cpp.Method
			for _, m := range ms {
				if m.HasRequest {
					out = append(out, m)
				}
			}
			return out
		},
		"FilterMethodsWithoutResps": func(ms []cpp.Method) []cpp.Method {
			var out []cpp.Method
			for _, m := range ms {
				if m.HasResponse {
					out = append(out, m)
				}
			}
			return out
		},
		"StackUse": func(props cpp.LLContextProps) int {
			return props.StackUseRequest + props.StackUseResponse
		},
		"TrivialCopy": func() cpp.FamilyKind {
			return cpp.TrivialCopy
		},
		"Reference": func() cpp.FamilyKind {
			return cpp.Reference
		},
		"String": func() cpp.FamilyKind {
			return cpp.String
		},
		"Vector": func() cpp.FamilyKind {
			return cpp.Vector
		},
		"ArrayKind": func() cpp.TypeKind {
			return cpp.ArrayKind
		},
		"VectorKind": func() cpp.TypeKind {
			return cpp.VectorKind
		},
		"StringKind": func() cpp.TypeKind {
			return cpp.StringKind
		},
		"HandleKind": func() cpp.TypeKind {
			return cpp.HandleKind
		},
		"RequestKind": func() cpp.TypeKind {
			return cpp.RequestKind
		},
		"PrimitiveKind": func() cpp.TypeKind {
			return cpp.PrimitiveKind
		},
		"BitsKind": func() cpp.TypeKind {
			return cpp.BitsKind
		},
		"EnumKind": func() cpp.TypeKind {
			return cpp.EnumKind
		},
		"ConstKind": func() cpp.TypeKind {
			return cpp.ConstKind
		},
		"StructKind": func() cpp.TypeKind {
			return cpp.StructKind
		},
		"TableKind": func() cpp.TypeKind {
			return cpp.TableKind
		},
		"UnionKind": func() cpp.TypeKind {
			return cpp.UnionKind
		},
		"ProtocolKind": func() cpp.TypeKind {
			return cpp.ProtocolKind
		},
		"NewTypedArgument": func(argument_name string,
			argument_type cpp.Type,
			pointer bool,
			access bool,
			mutable_access bool) TypedArgument {
			return TypedArgument{
				ArgumentName:  argument_name,
				ArgumentValue: argument_name,
				ArgumentType:  argument_type,
				Pointer:       pointer,
				Nullable:      pointer,
				Access:        access,
				MutableAccess: mutable_access}
		},
		"NewTypedArgumentElement": func(argument_name string, argument_type cpp.Type) TypedArgument {
			return TypedArgument{
				ArgumentName:  argument_name + "_element",
				ArgumentValue: "(*" + argument_name + "_element)",
				ArgumentType:  argument_type,
				Pointer:       true,
				Nullable:      false,
				Access:        false,
				MutableAccess: false}
		},
	})
	templates := []string{
		fragmentBitsTmpl,
		fragmentClientTmpl,
		fragmentClientAsyncMethodsTmpl,
		fragmentClientSyncMethodsTmpl,
		fragmentConstTmpl,
		fragmentEnumTmpl,
		fragmentEventSenderTmpl,
		fragmentProtocolTmpl,
		fragmentReplyCFlavorTmpl,
		fragmentReplyCallerAllocateTmpl,
		fragmentSendEventCFlavorTmpl,
		fragmentSendEventCallerAllocateTmpl,
		fragmentServiceTmpl,
		fragmentStructTmpl,
		fragmentSyncEventHandlerTmpl,
		fragmentSyncRequestManagedTmpl,
		fragmentSyncRequestCallerAllocateTmpl,
		fragmentSyncServerTmpl,
		fragmentTableTmpl,
		fragmentTypeTmpl,
		fragmentUnionTmpl,
		fileHeaderTmpl,
		fileSourceTmpl,
	}
	for _, t := range templates {
		template.Must(tmpls.Parse(t))
	}
	return &Generator{
		tmpls: tmpls,
	}
}

func generateFile(filename, clangFormatPath string, contentGenerator func(wr io.Writer) error) error {
	if err := os.MkdirAll(filepath.Dir(filename), os.ModePerm); err != nil {
		return err
	}

	file, err := os.Create(filename)
	if err != nil {
		return err
	}

	generatedPipe, err := cpp.NewClangFormatter(clangFormatPath).FormatPipe(file)
	if err != nil {
		return err
	}

	if err := contentGenerator(generatedPipe); err != nil {
		return err
	}

	return generatedPipe.Close()
}

func (gen *Generator) generateHeader(wr io.Writer, tree cpp.Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "Header", tree)
}

func (gen *Generator) generateSource(wr io.Writer, tree cpp.Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "Source", tree)
}

// GenerateHeader generates the LLCPP bindings header, and writes it into
// the target filename.
func (gen *Generator) GenerateHeader(tree cpp.Root, filename, clangFormatPath string) error {
	return generateFile(filename, clangFormatPath, func(wr io.Writer) error {
		return gen.generateHeader(wr, tree)
	})
}

// GenerateSource generates the LLCPP bindings header, and writes it into
// the target filename.
func (gen *Generator) GenerateSource(tree cpp.Root, filename, clangFormatPath string) error {
	return generateFile(filename, clangFormatPath, func(wr io.Writer) error {
		return gen.generateSource(wr, tree)
	})
}
