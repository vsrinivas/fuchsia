// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

import (
	"embed"
	"strings"
	"text/template"

	cpp "go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen_cpp"
)

//go:embed *.tmpl
var templates embed.FS

// A fuzzableProtocol is a protocol with at least one fuzzable method (method
// that has at least one request argument). Identifying fuzzable protocols up
// front makes it easier to avoid dividing by zero in the generated code.
type fuzzableProtocol struct {
	*cpp.Protocol
	FuzzableMethods []*cpp.Method
}

func NewGenerator(flags *cpp.CmdlineFlags) *cpp.Generator {
	return cpp.NewGenerator(flags, templates, template.FuncMap{
		"DoubleColonToUnderscore": func(s string) string {
			s2 := strings.ReplaceAll(s, "::", "_")
			// Drop any leading "::" => "_".
			if len(s2) > 0 && s2[0] == '_' {
				return s2[1:]
			}
			return s2
		},
		"FuzzableProtocols":    fuzzableProtocols,
		"CountDecoderEncoders": countDecoderEncoders,
	})
}

func fuzzableProtocols(decls []cpp.Kinded) []fuzzableProtocol {
	var result []fuzzableProtocol
	for _, decl := range decls {
		if decl.Kind() != cpp.Kinds.Protocol {
			continue
		}
		protocol := decl.(*cpp.Protocol)
		var fuzzableMethods []*cpp.Method
		for i := range protocol.Methods {
			if len(protocol.Methods[i].RequestArgs) != 0 {
				fuzzableMethods = append(fuzzableMethods, &protocol.Methods[i])
			}
		}
		if len(fuzzableMethods) != 0 {
			result = append(result, fuzzableProtocol{
				Protocol:        protocol,
				FuzzableMethods: fuzzableMethods,
			})
		}
	}
	return result
}

// countDecoderEncoders duplicates template logic that inlines struct and table
// decode/encode callbacks to get a count of total callbacks.
func countDecoderEncoders(decls []cpp.Kinded) int {
	count := 0
	for _, decl := range decls {
		if _, ok := decl.(*cpp.Struct); ok {
			count++
		} else if _, ok := decl.(*cpp.Table); ok {
			count++
		}
	}
	return count
}
