// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentMethodClientImplTmpl = `
{{- define "Method:ClientImpl:MessagingHeader" }}
  {{- if .HasResponse }}
    {{- /* two-way method */}}

    {{- template "Method:ClientImplAsync:MessagingHeader" . }}
    {{- template "Method:ClientImplSync:MessagingHeader" . }}

  {{- else }}
    {{- /* one-way method */}}

    {{- /* There is no distinction between sync vs async for one-way methods . */}}
    {{- template "Method:ClientImplOneway:MessagingHeader" . }}

  {{- end }}
{{- end }}


{{- define "Method:ClientImpl:MessagingSource" }}
  {{- if .HasResponse }}
    {{- /* two-way method */}}

    {{- template "Method:ClientImplAsync:MessagingSource" . }}
    {{- template "Method:ClientImplSync:MessagingSource" . }}

  {{- else }}
    {{- /* one-way method */}}

    {{- /* There is no distinction between sync vs async for one-way methods . */}}
    {{- template "Method:ClientImplOneway:MessagingSource" . }}
  {{- end }}
{{- end }}
`
