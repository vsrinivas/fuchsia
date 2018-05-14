// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Interface = `
{{- define "InterfaceDefinition" -}}

const (
{{- range .Methods }}
	{{ .OrdinalName }} uint32 = {{ .Ordinal }}
{{- end }}
)

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
func (p *{{ $.ProxyName }}) {{ if .IsEvent -}}
		{{ .EventExpectName }}
	{{- else -}}
		{{ .Name }}
	{{- end -}}
	(
	{{- if .Request -}}
	{{- range $index, $m := .Request.Members -}}
		{{- if $index -}}, {{- end -}}
		{{ $m.PrivateName }} {{ $m.Type }}
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
		{{ .Name }}: {{ .PrivateName }},
		{{- end }}
	}
	{{- end }}
	{{- if .Response }}
	resp_ := {{ .Response.Name }}{}
	{{- end }}
	{{- if .Request }}
		{{- if .Response }}
	err := ((*_bindings.Proxy)(p)).Call({{ .OrdinalName }}, &req_, &resp_)
		{{- else }}
	err := ((*_bindings.Proxy)(p)).Send({{ .OrdinalName }}, &req_)
		{{- end }}
	{{- else }}
		{{- if .Response }}
	err := ((*_bindings.Proxy)(p)).Recv({{ .OrdinalName }}, &resp_)
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

// {{ .Name }} server interface.
type {{ .Name }} interface {
{{- range .Methods }}
	{{- if .Request }}
	{{ .Name }}(
	{{- range $index, $m := .Request.Members -}}
		{{- if $index -}}, {{- end -}}
		{{ $m.PrivateName }} {{ $m.Type }}
	{{- end -}}
	)
	{{- if .Response -}}
	{{- if len .Response.Members }} (
	{{- range .Response.Members }}{{ .PrivateName }} {{ .Type }}, {{ end -}}
		err_ error)
	{{- else }} error{{ end -}}
	{{- else }} error{{ end }}
	{{- end }}
{{- end }}
}

type {{ .RequestName }} _bindings.InterfaceRequest

func New{{ .RequestName }}() ({{ .RequestName }}, *{{ .ProxyName }}, error) {
	req, cli, err := _bindings.NewInterfaceRequest()
	return {{ .RequestName }}(req), (*{{ .ProxyName }})(cli), err
}

{{- if .ServiceNameString }}
// Implements ServiceRequest.
func (_ {{ .RequestName }}) Name() string {
	return {{ .ServiceNameString }}
}
func (c {{ .RequestName }}) ToChannel() _zx.Channel {
	return c.Channel
}

const {{ .ServiceNameConstant }} = {{ .ServiceNameString }}
{{- end }}

type {{ .StubName }} struct {
	Impl {{ .Name }}
}

func (s *{{ .StubName }}) Dispatch(ord uint32, b_ []byte, h_ []_zx.Handle) (_bindings.Payload, error) {
	switch ord {
	{{- range .Methods }}
	{{- if not .IsEvent }}
	case {{ .OrdinalName }}:
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
		{{- range .Response.Members }}{{ .PrivateName }}, {{ end -}}
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
		out_.{{ .Name }} = {{ .PrivateName }}
		{{- end }}
		return &out_, err_
		{{- else }}
		return nil, err_
		{{- end }}
	{{- end }}
	{{- end }}
	}
	return nil, _bindings.ErrUnknownOrdinal
}

type {{ .ServerName }} struct {
	_bindings.BindingSet
}

func (s *{{ .ServerName }}) Add(impl {{ .Name }}, c _zx.Channel, onError func(error)) (_bindings.BindingKey, error) {
	return s.BindingSet.Add(&{{ .StubName }}{Impl: impl}, c, onError)
}

func (s *{{ .ServerName }}) EventProxyFor(key _bindings.BindingKey) (*{{ .EventProxyName }}, bool) {
	pxy, err := s.BindingSet.ProxyFor(key)
	return (*{{ .EventProxyName }})(pxy), err
}

type {{ .EventProxyName }} _bindings.Proxy
{{ range .Methods }}
{{- if .IsEvent }}
func (p *{{ $.EventProxyName }}) {{ .Name }}(
	{{- range $index, $m := .Response.Members -}}
		{{- if $index -}}, {{- end -}}
		{{ $m.PrivateName }} {{ $m.Type }}
	{{- end -}}
	) error {

	{{- if .Response }}
	event_ := {{ .Response.Name }}{
		{{- range .Response.Members }}
		{{ .Name }}: {{ .PrivateName }},
		{{- end }}
	}
	{{- end }}
	return ((*_bindings.Proxy)(p)).Send({{ .OrdinalName }}, &event_)
}
{{- end }}
{{- end }}

{{ end -}}
`
