// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Interface = `
{{- define "InterfaceDefinition" -}}

{{- range .Methods }}
// Request for {{ .Name }}.
{{- if .Request }}
{{ template "StructDefinition" .Request }}
{{- else }}
// {{ .Name }} has no request.
{{- end }}
{{- if .Response }}
// Response for {{ .Name }}.
{{ template "StructDefinition" .Response }}
{{- else }}
// {{ .Name }} has no response.
{{- end }}
{{- end }}

type {{ .ProxyName }} _bindings.Proxy
{{ range .Methods }}
func (p *{{ $.ProxyName }}) {{ .Name }}(
	{{- if .Request -}}
	{{- range $index, $m := .Request.Members -}}
		{{- if $index -}}, {{- end -}}
		{{ $m.Name | privatize }} {{ $m.Type }}
	{{- end -}}
	{{- end -}}
	)
	{{- if .Response -}}
	{{- if len .Response.Members }} (
	{{- range .Response.Members }}{{ .Type }}, {{ end -}}
		error)
	{{- else }} error{{ end -}}
	{{- else }} error{{ end }} {

	{{- if .Request }}
	req_ := {{ .Request.Name }}{
		{{- range .Request.Members }}
		{{ .Name }}: {{ .Name | privatize }},
		{{- end }}
	}
	{{- end }}
	{{- if .Response }}
	resp_ := {{ .Response.Name }}{}
	{{- end }}
	{{- if .Request }}
		{{- if .Response }}
	err := ((*Proxy)(p)).Call({{ .Ordinal }}, &req_, &resp_)
		{{- else }}
	err := p.Send({{ .Ordinal }}, &req_)
		{{- end }}
	{{- else }}
		{{- if .Response }}
	err := p.Recv({{ .Ordinal }}, &resp_)
		{{- else }}
	err := nil
		{{- end }}
	{{- end }}
	{{- if .Response }}
	return {{ range .Response.Members }}resp_.{{ .Name }}, {{ end }}err
	{{- else }}
	return err
	{{- end }}
}
{{- end }}

type {{ .Name }} interface {
{{- range .Methods }}
	{{ .Name }}(
	{{- if .Request -}}
	{{- range $index, $m := .Request.Members -}}
		{{- if $index -}}, {{- end -}}
		{{ $m.Name | privatize }} {{ $m.Type }}
	{{- end -}}
	{{- end -}}
	)
	{{- if .Response -}}
	{{- if len .Response.Members }} (
	{{- range .Response.Members }}{{ .Name | privatize }} {{ .Type }}, {{ end -}}
		err error)
	{{- else }} error{{ end -}}
	{{- else }} error{{ end }}
{{- end }}
}

type {{ .RequestName }} _bindings.InterfaceRequest

type {{ .StubName }} struct {
	Impl {{ .Name }}
}

func (s *{{ .StubName }}) Dispatch(ord uint32, b_ []byte, h_ []_zx.Handle) (_bindings.Payload, error) {
	switch ord {
	{{- range .Methods }}
	case {{ .Ordinal }}:
		{{- if .Request }}
		in_ := {{ .Request.Name }}{}
		if err_ := _bindings.Unmarshal(b_, h_, &in_); err_ != nil {
			return nil, err_
		}
		{{- end }}
		{{- if .Response }}
		out_ := {{ .Response.Name }}{}
		{{- end }}
		{{ if .Response }}
		{{- range .Response.Members }}{{ .Name | privatize }}, {{ end -}}
		{{- end -}}
		err_ := s.Impl.{{ .Name }}(
		{{- if .Request -}}
		{{- range $index, $m := .Request.Members -}}
		{{- if $index -}}, {{- end -}}
		in_.{{ $m.Name }}
		{{- end -}}
		{{- end -}}
		)
		{{- if .Response }}
		{{- range .Response.Members }}
		out_.{{ .Name }} = {{ .Name | privatize }}
		{{- end }}
		return &out_, err_
		{{- else }}
		return nil, err_
		{{- end }}
	{{- end }}
	}
	// TODO(mknyszek): Use a well-defined error here.
	return nil, fmt.Errorf("Unknown ordinal %d", ord)
}
{{ end -}}
`
