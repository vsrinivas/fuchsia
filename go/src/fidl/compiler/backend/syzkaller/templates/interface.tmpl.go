// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Interface = `
{{- define "InterfaceDefinition" -}}

{{- range .Methods }}
{{- if .Request }}
{{ template "StructDefinition" .Request }}
{{- end }}
{{- if .Response }}
{{ template "StructDefinition" .Response }}
{{- end }}
{{- range .Structs }}
{{ template "StructDefinition" . }}
{{- end }}
{{- end }}

resource zx_chan_{{ .Name }}_client[zx_chan]
resource zx_chan_{{ .Name }}_server[zx_chan]

zx_channel_create${{ .Name }}(options const[0], out0 ptr[out, zx_chan_{{ .Name }}_client], out1 ptr[out, zx_chan_{{ .Name }}_server])
fdio_service_connect${{ .Name }}(path ptr[in, string["/svc/{{ .ServiceNameString }}"]], handle zx_chan_{{ .Name }}_server)

{{- $if := .Name }}
{{- range .Methods }}
zx_channel_call${{ .Name }}(handle zx_chan_{{ $if }}_client, options const[0], deadline zx_time, args ptr[in, fidl_call_args[{{ .Name }}Request, array[zx_handle], array[zx_handle]]], actual_bytes ptr[out, int32], actual_handles ptr[out, int32])
{{- end }}

{{ end -}}
`
