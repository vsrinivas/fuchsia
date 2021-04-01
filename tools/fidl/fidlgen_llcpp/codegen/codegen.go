// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"bytes"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"text/template"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
	cpp "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen_cpp"
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

type formatParam func(cpp.Type, string) string

func formatParams(params []cpp.Parameter, prefixIfNonempty string, format formatParam) string {
	if len(params) == 0 {
		return ""
	}
	args := []string{}

	for _, p := range params {
		args = append(args, format(p.Type, p.Name))
	}
	if len(args) > 0 {
		return prefixIfNonempty + strings.Join(args, ", ")
	}
	return ""
}

func closeHandles(argumentName string, argumentValue string, argumentType cpp.Type, pointer bool, nullable bool, access bool, mutableAccess bool) string {
	if !argumentType.IsResource {
		return ""
	}
	name := argumentName
	value := argumentValue
	if access {
		name = fmt.Sprintf("%s()", name)
		value = name
	} else if mutableAccess {
		name = fmt.Sprintf("mutable_%s()", name)
		value = name
	}

	switch argumentType.Kind {
	case cpp.TypeKinds.Handle, cpp.TypeKinds.Request, cpp.TypeKinds.Protocol:
		if pointer {
			if nullable {
				return fmt.Sprintf("if (%s != nullptr) { %s->reset(); }", name, name)
			}
			return fmt.Sprintf("%s->reset();", name)
		} else {
			return fmt.Sprintf("%s.reset();", name)
		}
	case cpp.TypeKinds.Array:
		element_name := argumentName + "_element"
		element_type := argumentType.ElementType
		var buf bytes.Buffer
		buf.WriteString("{\n")
		buf.WriteString(fmt.Sprintf("%s* %s = %s.data();\n", element_type, element_name, value))
		buf.WriteString(fmt.Sprintf("for (size_t i = 0; i < %s.size(); ++i, ++%s) {\n", value, element_name))
		buf.WriteString(closeHandles(element_name, fmt.Sprintf("(*%s)", element_name), *element_type, true, false, false, false))
		buf.WriteString("\n}\n}\n")
		return buf.String()
	case cpp.TypeKinds.Vector:
		element_name := argumentName + "_element"
		element_type := argumentType.ElementType
		var buf bytes.Buffer
		buf.WriteString("{\n")
		buf.WriteString(fmt.Sprintf("%s* %s = %s.mutable_data();\n", element_type, element_name, value))
		buf.WriteString(fmt.Sprintf("for (uint64_t i = 0; i < %s.count(); ++i, ++%s) {\n", value, element_name))
		buf.WriteString(closeHandles(element_name, fmt.Sprintf("(*%s)", element_name), *element_type, true, false, false, false))
		buf.WriteString("\n}\n}\n")
		return buf.String()
	default:
		if pointer {
			if nullable {
				return fmt.Sprintf("if (%s != nullptr) { %s->_CloseHandles(); }", name, name)
			}
			return fmt.Sprintf("%s->_CloseHandles();", name)
		} else {
			return fmt.Sprintf("%s._CloseHandles();", name)
		}
	}
}

// These are the helper functions we inject for use by the templates.
var utilityFuncs = template.FuncMap{
	"SyncCallTotalStackSize": func(m cpp.Method) int {
		totalSize := 0
		if m.Request.ClientAllocation.IsStack {
			totalSize += m.Request.ClientAllocation.Size
		}
		if m.Response.ClientAllocation.IsStack {
			totalSize += m.Response.ClientAllocation.Size
		}
		return totalSize
	},
	"CloseHandles": func(member cpp.Member,
		access bool,
		mutableAccess bool) string {
		n, t := member.NameAndType()
		return closeHandles(n, n, t, t.WirePointer, t.WirePointer, access, mutableAccess)
	},
	"Params": func(params []cpp.Parameter) string {
		return formatParams(params, "", func(t cpp.Type, n string) string {
			return fmt.Sprintf("%s %s", t.String(), n)
		})
	},
	"CommaParams": func(params []cpp.Parameter) string {
		return formatParams(params, ", ", func(t cpp.Type, n string) string {
			return fmt.Sprintf("%s %s", t.String(), n)
		})
	},
	"ParamNames": func(params []cpp.Parameter) string {
		return formatParams(params, "", func(t cpp.Type, n string) string {
			return n
		})
	},
	"CommaParamNames": func(params []cpp.Parameter) string {
		return formatParams(params, ", ", func(t cpp.Type, n string) string {
			return n
		})
	},
	"ParamsNoTypedChannels": func(params []cpp.Parameter) string {
		return formatParams(params, "", func(t cpp.Type, n string) string {
			return fmt.Sprintf("%s %s", t.WireNoTypedChannels(), n)
		})
	},
	"ParamMoveNames": func(params []cpp.Parameter) string {
		return formatParams(params, "", func(t cpp.Type, n string) string {
			return fmt.Sprintf("std::move(%s)", n)
		})
	},
	"MessagePrototype": func(params []cpp.Parameter) string {
		return formatParams(params, "", func(t cpp.Type, n string) string {
			return t.WireArgumentDeclaration(n)
		})
	},
	"CommaMessagePrototype": func(params []cpp.Parameter) string {
		return formatParams(params, ", ", func(t cpp.Type, n string) string {
			return t.WireArgumentDeclaration(n)
		})
	},
	"InitMessage": func(params []cpp.Parameter) string {
		return formatParams(params, ": ", func(t cpp.Type, n string) string {
			return t.WireInitMessage(n)
		})
	},
}

func NewGenerator() *Generator {
	tmpls := template.New("LLCPPTemplates").
		Funcs(cpp.MergeFuncMaps(cpp.CommonTemplateFuncs, utilityFuncs))
	templates := []string{
		fragmentBitsTmpl,
		fragmentClientTmpl,
		fragmentClientAsyncMethodsTmpl,
		fragmentClientSyncMethodsTmpl,
		fragmentConstTmpl,
		fragmentEnumTmpl,
		fragmentEventSenderTmpl,
		fragmentMethodRequestTmpl,
		fragmentMethodResponseTmpl,
		fragmentMethodResultTmpl,
		fragmentMethodUnownedResultTmpl,
		fragmentProtocolTmpl,
		fragmentProtocolInterfaceTmpl,
		fragmentReplyManagedTmpl,
		fragmentReplyCallerAllocateTmpl,
		fragmentServiceTmpl,
		fragmentStructTmpl,
		fragmentSyncEventHandlerTmpl,
		fragmentSyncRequestCallerAllocateTmpl,
		fragmentSyncServerTmpl,
		fragmentTableTmpl,
		fragmentUnionTmpl,
		fileHeaderTmpl,
		fileSourceTmpl,
		testBaseTmpl,
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

	file, err := fidlgen.NewLazyWriter(filename)
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

func (gen *Generator) generateTestBase(wr io.Writer, tree cpp.Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "TestBase", tree)
}

// GenerateHeader generates the LLCPP bindings header, and writes it into
// the target filename.
func (gen *Generator) GenerateHeader(tree cpp.Root, filename, clangFormatPath string) error {
	return generateFile(filename, clangFormatPath, func(wr io.Writer) error {
		return gen.generateHeader(wr, tree)
	})
}

// GenerateSource generates the LLCPP bindings source, and writes it into
// the target filename.
func (gen *Generator) GenerateSource(tree cpp.Root, filename, clangFormatPath string) error {
	return generateFile(filename, clangFormatPath, func(wr io.Writer) error {
		return gen.generateSource(wr, tree)
	})
}

// GenerateTestBase generates the LLCPP bindings test base header, and
// writes it into the target filename.
func (gen *Generator) GenerateTestBase(tree cpp.Root, filename, clangFormatPath string) error {
	return generateFile(filename, clangFormatPath, func(wr io.Writer) error {
		return gen.generateTestBase(wr, tree)
	})
}
