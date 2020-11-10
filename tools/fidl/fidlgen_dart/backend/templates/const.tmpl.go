// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

// Const is the template for const declarations.
const Const = `
{{- define "ConstDeclaration" -}}
{{- range .Doc }}
///{{ . -}}
{{- end }}
const {{ .Type.Decl }} {{ .Name }} = {{ .Value }};
{{ end }}
`
