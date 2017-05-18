// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const GenerateEnum = `
{{/* . (dot) refers to a the Go type |rustgen.EnumTemplate|  */}}
{{- define "GenerateEnum" -}}
{{- $enum := . -}}
pub type {{$enum.Name}} =
{{- if eq $enum.Signed true}} i32
{{- else}} u32
{{- end}};
{{range $enum_val := $enum.Values}}pub const {{$enum.Name}}_{{$enum_val.Name}}: {{$enum.Name}} = {{$enum_val.Value}};
{{end}}
{{if eq $enum.Signed true -}}
pub const {{$enum.Name}}__UNKNOWN: {{$enum.Name}} = 0x7FFFFFFF;
{{else}}
pub const {{$enum.Name}}__UNKNOWN: {{$enum.Name}} = 0xFFFFFFFF;
{{end}}
{{end}}
`
