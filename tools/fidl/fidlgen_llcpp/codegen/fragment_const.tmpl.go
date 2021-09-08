// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentConstTmpl = `
{{- define "Const:Header" }}
{{ EnsureNamespace . }}
{{ .Docs }}
{{- if .Extern }}
extern {{ .Decorator }} {{ .Type }} {{ .Name }};
{{- else }}
{{ .Decorator }} {{ .Type }} {{ .Name }} = {{ .Value }};
{{- end }}
{{- end }}

{{- define "Const:Source" }}
{{- if .Extern }}
{{ EnsureNamespace "" }}
{{ .Decorator }} {{ .Type }} {{ . }} = {{ .Value }};
{{- end }}
{{- end }}
`
