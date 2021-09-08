// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentMethodClientImplTmpl = `
{{- define "Method:ClientImpl:Header" }}
  {{- if .HasResponse }}
    {{- /* two-way method */}}

    {{- template "Method:ClientImplAsync:Header" . }}
    {{- template "Method:ClientImplSync:Header" . }}

  {{- else }}
    {{- /* one-way method */}}

    {{- /* There is no distinction between sync vs async for one-way methods . */}}
    {{- template "Method:ClientImplOneway:Header" . }}

  {{- end }}
{{- end }}


{{- define "Method:ClientImpl:Source" }}
  {{- if .HasResponse }}
    {{- /* two-way method */}}

    {{- template "Method:ClientImplAsync:Source" . }}
    {{- template "Method:ClientImplSync:Source" . }}

  {{- else }}
    {{- /* one-way method */}}

    {{- /* There is no distinction between sync vs async for one-way methods . */}}
    {{- template "Method:ClientImplOneway:Source" . }}
  {{- end }}
{{- end }}
`
