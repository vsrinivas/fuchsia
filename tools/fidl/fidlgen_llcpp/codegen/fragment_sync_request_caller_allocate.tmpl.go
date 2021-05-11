// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentSyncRequestCallerAllocateTmpl = `
{{- define "SyncRequestCallerAllocateMethodArguments" -}}
{{- $args := (List) }}
{{- if .RequestArgs }}
  {{- $args = (List $args "::fidl::BufferSpan _request_buffer" .RequestArgs) }}
{{- end }}
{{- if .HasResponse }}
  {{- $args = (List $args "::fidl::BufferSpan _response_buffer") }}
{{- end }}
{{- RenderParams $args }}
{{- end }}

{{- define "StaticCallSyncRequestCallerAllocateMethodArguments" -}}
{{- $args := (printf "::fidl::UnownedClientEnd<%s> _client_end" .Protocol) }}
{{- if .RequestArgs }}
  {{- $args = (List $args "::fidl::BufferSpan _request_buffer" .RequestArgs) }}
{{- end }}
{{- if .HasResponse }}
  {{- $args = (List $args "::fidl::BufferSpan _response_buffer") }}
{{- end }}
{{- RenderParams $args }}
{{- end }}

`
