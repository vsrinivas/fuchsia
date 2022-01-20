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
		"Protocols":            protocols,
		"CountDecoderEncoders": countDecoderEncoders,
	})
}

func protocols(decls []cpp.Kinded) []*cpp.Protocol {
	protocols := make([]*cpp.Protocol, 0, len(decls))
	for _, decl := range decls {
		if decl.Kind() == cpp.Kinds.Protocol {
			protocols = append(protocols, decl.(*cpp.Protocol))
		}
	}
	return protocols
}

// countDecoderEncoders duplicates template logic that inlines protocol, struct, and table
// decode/encode callbacks to get a count of total callbacks.
func countDecoderEncoders(decls []cpp.Kinded) int {
	count := 0
	for _, decl := range decls {
		if p, ok := decl.(*cpp.Protocol); ok {
			for _, method := range p.Methods {
				if method.HasRequest {
					count++
				}
				if method.HasResponse {
					count++
				}
			}
		} else if s, ok := decl.(*cpp.Struct); ok {
			if !s.IsAnonymousRequestOrResponse() {
				count++
			}
		} else if _, ok := decl.(*cpp.Table); ok {
			count++
		}
	}
	return count
}
