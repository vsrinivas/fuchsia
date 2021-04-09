// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentSyncRequestCallerAllocateTmpl = `
{{- define "CallerBufferParams" -}}
{{- if . -}}
::fidl::BufferSpan _request_buffer {{ . | CalleeCommaParams }}
{{- end -}}
{{- end }}

{{- define "SyncRequestCallerAllocateMethodArguments" -}}
{{ template "CallerBufferParams" .RequestArgs }}{{ if .HasResponse }}{{ if .RequestArgs }}, {{ end }}
::fidl::BufferSpan _response_buffer{{ end }}
{{- end }}

{{- define "StaticCallSyncRequestCallerAllocateMethodArguments" -}}
::fidl::UnownedClientEnd<{{ .Protocol }}> _client_end
{{- if .RequestArgs }}, {{ end }}
{{- template "CallerBufferParams" .RequestArgs }}
{{- if .HasResponse }}, ::fidl::BufferSpan _response_buffer{{ end }}
{{- end }}

`
