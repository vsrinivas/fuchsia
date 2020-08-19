// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"io"
	"os"
	"path/filepath"
	"text/template"

	"fidl/compiler/backend/cpp"

	"go.fuchsia.dev/fuchsia/tools/fidl/fidlgen_llcpp/templates/files"
	"go.fuchsia.dev/fuchsia/tools/fidl/fidlgen_llcpp/templates/fragments"
)

type Generator struct {
	tmpls *template.Template
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
	})
	templates := []string{
		fragments.Bits,
		fragments.Client,
		fragments.ClientAsyncMethods,
		fragments.ClientSyncMethods,
		fragments.Const,
		fragments.Enum,
		fragments.EventSender,
		fragments.Protocol,
		fragments.ReplyCFlavor,
		fragments.ReplyCallerAllocate,
		fragments.ReplyInPlace,
		fragments.SendEventCFlavor,
		fragments.SendEventCallerAllocate,
		fragments.SendEventInPlace,
		fragments.Service,
		fragments.Struct,
		fragments.SyncEventHandler,
		fragments.SyncRequestManaged,
		fragments.SyncRequestCallerAllocate,
		fragments.SyncServer,
		fragments.Table,
		fragments.Union,
		files.Header,
		files.Source,
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
