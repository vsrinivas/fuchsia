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
	"reflect"
	"runtime"
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

// formatParam funcs are helpers that transform a type and name into a string
// for rendering in a template.
type formatParam func(string, cpp.Type) string

// visitSliceMembers visits each member of nested slices passed in and calls
// |fn| on each of them in depth first order.
func visitSliceMembers(val reflect.Value, fn func(interface{})) {
	switch val.Type().Kind() {
	case reflect.Slice:
		for j := 0; j < val.Len(); j++ {
			visitSliceMembers(val.Index(j), fn)
		}
	case reflect.Interface:
		visitSliceMembers(val.Elem(), fn)
	default:
		fn(val.Interface())
	}
}

// renderParams renders a nested list of parameter definitions.
// The parameter definitions are either strings or cpp.Parameters.
// Parameter structs are rendered with the supplied format func.
// The strings and formatted Parameters are joined with commas and returned.
func renderParams(format formatParam, list interface{}) string {
	var (
		buf   bytes.Buffer
		first = true
	)
	visitSliceMembers(reflect.ValueOf(list), func(val interface{}) {
		if val == nil {
			panic(fmt.Sprintf("Unexpected nil in %#v", list))
		}
		if first {
			first = false
		} else {
			buf.WriteString(", ")
		}
		switch val := val.(type) {
		case string:
			buf.WriteString(val)
		case cpp.Parameter:
			n, t := val.NameAndType()
			buf.WriteString(format(n, t))
		default:
			panic(fmt.Sprintf("Invalid RenderParams arg %#v", val))
		}
	})

	return buf.String()
}

func param(n string, t cpp.Type) string {
	if t.Kind == cpp.TypeKinds.Array || t.Kind == cpp.TypeKinds.Struct {
		if !t.Nullable {
			if t.IsResource {
				return fmt.Sprintf("%s&& %s", t.String(), n)
			}
			return fmt.Sprintf("const %s& %s", t.String(), n)
		}
	}
	if t.Kind == cpp.TypeKinds.Handle || t.Kind == cpp.TypeKinds.Request || t.Kind == cpp.TypeKinds.Protocol {
		return fmt.Sprintf("%s&& %s", t.String(), n)
	}
	return fmt.Sprintf("%s %s", t.String(), n)
}

func forwardParam(n string, t cpp.Type) string {
	if t.Kind == cpp.TypeKinds.Union && t.IsResource {
		return fmt.Sprintf("std::move(%s)", n)
	} else if t.Kind == cpp.TypeKinds.Array || t.Kind == cpp.TypeKinds.Struct {
		if t.IsResource && !t.Nullable {
			return fmt.Sprintf("std::move(%s)", n)
		}
	} else if t.Kind == cpp.TypeKinds.Handle || t.Kind == cpp.TypeKinds.Request || t.Kind == cpp.TypeKinds.Protocol {
		return fmt.Sprintf("std::move(%s)", n)
	}
	return n
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
	"SyncCallTotalStackSizeV1": func(m cpp.Method) int {
		totalSize := 0
		if m.Request.ClientAllocationV1.IsStack {
			totalSize += m.Request.ClientAllocationV1.Size
		}
		if m.Response.ClientAllocationV1.IsStack {
			totalSize += m.Response.ClientAllocationV1.Size
		}
		return totalSize
	},
	"SyncCallTotalStackSizeV2": func(m cpp.Method) int {
		totalSize := 0
		if m.Request.ClientAllocationV2.IsStack {
			totalSize += m.Request.ClientAllocationV2.Size
		}
		if m.Response.ClientAllocationV2.IsStack {
			totalSize += m.Response.ClientAllocationV2.Size
		}
		return totalSize
	},
	"CloseHandles": func(member cpp.Member,
		access bool,
		mutableAccess bool) string {
		n, t := member.NameAndType()
		return closeHandles(n, n, t, t.WirePointer, t.WirePointer, access, mutableAccess)
	},
	"RenderParams": func(params ...interface{}) string {
		return renderParams(param, params)
	},
	"RenderForwardParams": func(params ...interface{}) string {
		return renderParams(forwardParam, params)
	},
	"RenderInitMessage": func(params ...interface{}) string {
		s := renderParams(func(n string, t cpp.Type) string {
			return n + "(" + forwardParam(n, t) + ")"
		}, params)
		if len(s) == 0 {
			return ""
		}
		return ": " + s
	},
	// List is a helper to return a list of its arguments.
	"List": func(items ...interface{}) []interface{} {
		return items
	},
}

func NewGenerator() *Generator {
	tmpls := template.New("LLCPPTemplates").
		Funcs(cpp.MergeFuncMaps(cpp.CommonTemplateFuncs, utilityFuncs))
	templates := []string{
		fileHeaderTmpl,
		fileSourceTmpl,
		fileTestBaseTmpl,
		fragmentBitsTmpl,
		fragmentConstTmpl,
		fragmentEnumTmpl,
		fragmentMethodClientImplAsyncTmpl,
		fragmentMethodClientImplOnewayTmpl,
		fragmentMethodClientImplSyncTmpl,
		fragmentMethodClientImplTmpl,
		fragmentMethodCompleterBaseTmpl,
		fragmentMethodRequestTmpl,
		fragmentMethodResponseContextTmpl,
		fragmentMethodResponseTmpl,
		fragmentMethodResultTmpl,
		fragmentMethodUnownedResultTmpl,
		fragmentProtocolCallerTmpl,
		fragmentProtocolClientImplTmpl,
		fragmentProtocolDetailsTmpl,
		fragmentProtocolDispatcherTmpl,
		fragmentProtocolEventHandlerTmpl,
		fragmentProtocolEventSenderTmpl,
		fragmentProtocolInterfaceTmpl,
		fragmentProtocolSyncClientTmpl,
		fragmentProtocolTmpl,
		fragmentServiceTmpl,
		fragmentStructTmpl,
		fragmentTableTmpl,
		fragmentUnionTmpl,
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
		return fmt.Errorf("Error creating LazyWriter: %w", err)
	}

	bufferedContent := new(bytes.Buffer)
	if err := contentGenerator(bufferedContent); err != nil {
		return fmt.Errorf("Error generating content: %w", err)
	}
	// TODO(fxbug.dev/78303): Investigate clang-format memory usage on large files.
	maybeFormatter := clangFormatPath
	if bufferedContent.Len() > 1024*1024 && runtime.GOOS == "darwin" {
		maybeFormatter = ""
	}
	generatedPipe, err := cpp.NewClangFormatter(maybeFormatter).FormatPipe(file)
	if err != nil {
		return fmt.Errorf("Error in FormatPipe: %w", err)
	}
	_, err = bufferedContent.WriteTo(generatedPipe)
	if err != nil {
		return fmt.Errorf("Error writing to formatter: %w", err)
	}

	if err := generatedPipe.Close(); err != nil {
		return fmt.Errorf("Error closing generatedPipe: %w", err)
	}

	return nil
}

func (gen *Generator) generateHeader(wr io.Writer, tree cpp.Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "File:Header", tree)
}

func (gen *Generator) generateSource(wr io.Writer, tree cpp.Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "File:Source", tree)
}

func (gen *Generator) generateTestBase(wr io.Writer, tree cpp.Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "File:TestBase", tree)
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
