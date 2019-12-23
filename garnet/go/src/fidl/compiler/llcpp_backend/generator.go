// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"io"
	"os"
	"path/filepath"
	"text/template"

	"fidl/compiler/backend/cpp/ir"
	"fidl/compiler/llcpp_backend/templates/files"
	"fidl/compiler/llcpp_backend/templates/fragments"
)

type generator struct {
	tmpls *template.Template
}

func newGenerator() *generator {
	tmpls := template.New("LLCPPTemplates").Funcs(template.FuncMap{
		"Kinds": func() interface{} { return ir.Kinds },
		"Eq":    func(a interface{}, b interface{}) bool { return a == b },
		"MethodsHaveReqOrResp": func(ms []ir.Method) string {
			for _, m := range ms {
				if (m.HasRequest && len(m.Request) != 0) || (m.HasResponse && len(m.Response) != 0) {
					return "\n"
				}
			}
			return ""
		},
		"FilterMethodsWithReqs": func(ms []ir.Method) []ir.Method {
			var out []ir.Method
			for _, m := range ms {
				if !m.HasRequest {
					out = append(out, m)
				}
			}
			return out
		},
		"FilterMethodsWithoutReqs": func(ms []ir.Method) []ir.Method {
			var out []ir.Method
			for _, m := range ms {
				if m.HasRequest {
					out = append(out, m)
				}
			}
			return out
		},
		"FilterMethodsWithoutResps": func(ms []ir.Method) []ir.Method {
			var out []ir.Method
			for _, m := range ms {
				if m.HasResponse {
					out = append(out, m)
				}
			}
			return out
		},
	})
	templates := []string{
		fragments.Bits,
		fragments.Const,
		fragments.Enum,
		fragments.Helpers,
		fragments.Interface,
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
		fragments.SyncRequestInPlace,
		fragments.SyncServer,
		fragments.Table,
		fragments.XUnion,
		files.Header,
		files.Source,
	}
	for _, t := range templates {
		template.Must(tmpls.Parse(t))
	}
	return &generator{
		tmpls: tmpls,
	}
}

// generateHeader generates the C++ bindings header.
func (gen *generator) generateHeader(wr io.Writer, tree ir.Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "Header", tree)
}

// generateSource generates the C++ bindings source, i.e. implementation.
func (gen *generator) generateSource(wr io.Writer, tree ir.Root) error {
	return gen.tmpls.ExecuteTemplate(wr, "Source", tree)
}

// generateFidl generates all files required for the C++ bindings.
func (gen generator) generateFidl(config config) error {
	fidl := config.fidl
	tree := ir.CompileLL(fidl)
	tree.PrimaryHeader = config.primaryHeaderPath

	if err := os.MkdirAll(filepath.Dir(config.headerPath), os.ModePerm); err != nil {
		return err
	}

	if err := os.MkdirAll(filepath.Dir(config.sourcePath), os.ModePerm); err != nil {
		return err
	}

	headerFile, err := os.Create(config.headerPath)
	if err != nil {
		return err
	}
	defer headerFile.Close()

	if err := gen.generateHeader(headerFile, tree); err != nil {
		return err
	}

	sourceFile, err := os.Create(config.sourcePath)
	if err != nil {
		return err
	}
	defer sourceFile.Close()

	if err := gen.generateSource(sourceFile, tree); err != nil {
		return err
	}

	return nil
}
