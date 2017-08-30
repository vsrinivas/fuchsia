// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const GenerateEnum = `
{{/* . (dot) refers to a the Go type |cgen.EnumTemplate|  */}}
{{define "GenerateEnum"}}
{{- $enum := . -}}
typedef uint32_t {{$enum.Name}};
enum {{$enum.Name}}_Enum {
  {{range $enum_val := $enum.Values -}}
  {{$enum.Name}}_{{$enum_val.Name}} = {{$enum_val.Value}},
  {{end}}
  {{$enum.Name}}__UNKNOWN__ = 0xFFFFFFFF,
};
{{end}}
`
