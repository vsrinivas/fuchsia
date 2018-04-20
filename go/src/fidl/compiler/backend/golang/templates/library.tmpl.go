// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Library = `
{{- define "GenerateLibraryFile" -}}
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package {{ .Name }}

import (
{{- if .NeedsBindings }}
	_bindings "fidl/bindings"
{{- end }}
{{- if .NeedsSyscallZx }}
	_zx "syscall/zx"
{{- end }}

{{- range $lib := .Libraries }}
	{{ $lib }} "fuchsia/go/{{ $lib }}"
{{- end }}
)

const (
{{- range $const := .Consts }}
	{{ .Name }} {{ .Type }} = {{ .Value }}
{{- end }}
)

{{ range $enum := .Enums -}}
{{ template "EnumDefinition" $enum }}
{{ end -}}
{{ range $struct := .Structs -}}
{{ template "StructDefinition" $struct }}
{{ end -}}
{{ range $union := .Unions -}}
{{ template "UnionDefinition" $union }}
{{ end -}}
{{ range $interface := .Interfaces -}}
{{ template "InterfaceDefinition" $interface }}
{{ end -}}

{{- end -}}
`
