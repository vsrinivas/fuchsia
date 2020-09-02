// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentEnumTmpl = `
{{- define "EnumForwardDeclaration" }}
enum class {{ .Name }} : {{ .Type }} {
  {{- range .Members }}
  {{ .Name }} = {{ .Value }},
  {{- end }}
};

{{ end }}

{{- define "EnumTraits" }}

template <>
struct IsFidlType<{{ .Namespace }}::{{ .Name }}> : public std::true_type {};

{{- end }}
`
