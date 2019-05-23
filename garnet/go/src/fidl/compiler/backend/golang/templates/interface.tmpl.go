// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Interface = `
{{- define "InterfaceDefinition" -}}

const (
{{- range .Methods }}
	{{ .OrdinalName }} uint32 = {{ .Ordinal }}
	{{ .GenOrdinalName }} uint32 = {{ .GenOrdinal }}
	{{- end }}
)

{{- range .Methods }}
{{- if .Request }}
{{- if len .Request.Members }}
{{ template "StructDefinition" .Request }}
{{- end }}
{{- end }}
{{- if .Response }}
{{- if len .Response.Members }}
{{ template "StructDefinition" .Response }}
{{- end }}
{{- end }}
{{- end }}

type {{ .ProxyName }} _bindings.{{ .ProxyType }}
{{ range .Methods }}
{{range .DocComments}}
//{{ . }}
{{- end}}
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
	{{- if len .Request.Members }}
	req_ := &{{ .Request.Name }}{
		{{- range .Request.Members }}
		{{ .Name }}: {{ .PrivateName }},
		{{- end }}
	}
	{{- else }}
	var req_ _bindings.Message
	{{- end }}
	{{- end }}
	{{- if .Response }}
	{{- if len .Response.Members }}
	resp_ := &{{ .Response.Name }}{}
	{{- else }}
	var resp_ _bindings.Message
	{{- end }}
	{{- end }}
	{{- if .Request }}
		{{- if .Response }}
	err := ((*_bindings.{{ $.ProxyType }})(p)).CallNew({{ .OrdinalName }}, req_, resp_)
		{{- else }}
	err := ((*_bindings.{{ $.ProxyType }})(p)).SendNew({{ .OrdinalName }}, req_)
		{{- end }}
	{{- else }}
		{{- if .Response }}
	err := ((*_bindings.{{ $.ProxyType }})(p)).RecvNew({{ .OrdinalName }}, resp_{{ if ne .Ordinal .GenOrdinal }}, {{ .GenOrdinalName }}{{ end }})
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

{{range .DocComments}}
//{{ . }}
{{- end}}
type {{ .Name }} interface {
{{- range .Methods }}
	{{- range .DocComments}}
	//{{ . }}
	{{- end}}
	{{- if .Request }}
	{{ .Name }}(
	{{- range $index, $m := .Request.Members -}}
		{{- if $index -}}, {{- end -}}
		{{ $m.PrivateName }} {{ $m.Type }}
	{{- end -}}
	)
	{{- if .Response -}}
	{{- if len .Response.Members }} (
	{{- range .Response.Members }}{{ .Type }}, {{ end -}}
		error)
	{{- else }} error{{ end -}}
	{{- else }} error{{ end }}
	{{- end }}
{{- end }}
}

type {{.TransitionalBaseName}} struct {}

{{ $transitionalBaseName := .TransitionalBaseName }}

{{- range  $method := .Methods }}
	{{- if $method.IsTransitional }}
		{{- if $method.Request }}
			func (_ *{{$transitionalBaseName}}) {{ $method.Name }} (
			{{- range $index, $m := $method.Request.Members -}}
				{{- if $index -}}, {{- end -}}
				{{ $m.PrivateName }} {{ $m.Type }}
			{{- end -}}
			)
			{{- if $method.Response -}}
				{{- if len $method.Response.Members }} (
					{{- range $method.Response.Members }}{{ .Type }}, {{ end -}}
						error)
				{{- else -}}
					error
				{{ end -}}
			{{- else -}}
				error
			{{- end -}}
			{
				panic("Not Implemented")
			}
		{{- end -}}
	{{- end}}
{{- end }}

{{- if eq .ProxyType "ChannelProxy" }}
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
{{- end }}

type {{ .StubName }} struct {
	Impl {{ .Name }}
}

func (s *{{ .StubName }}) DispatchNew(ord uint32, b_ []byte, h_ []_zx.Handle) (_bindings.Message, error) {
	switch ord {
	{{- range .Methods }}
	{{- if not .IsEvent }}
	{{- if ne .Ordinal .GenOrdinal }}
	case {{ .GenOrdinalName }}:
		fallthrough
	{{ end }}
	case {{ .OrdinalName }}:
		{{- if .Request }}
		{{- if len .Request.Members }}
		in_ := {{ .Request.Name }}{}
		if _, _, err_ := _bindings.UnmarshalNew(b_, h_, &in_); err_ != nil {
			return nil, err_
		}
		{{- end }}
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
		{{- if len .Response.Members }}
		out_ := {{ .Response.Name }}{}
		{{- range .Response.Members }}
		out_.{{ .Name }} = {{ .PrivateName }}
		{{- end }}
		return &out_, err_
		{{- else }}
		return nil, err_
		{{- end }}
		{{- else }}
		return nil, err_
		{{- end }}
	{{- end }}
	{{- end }}
	}
	return nil, _bindings.ErrUnknownOrdinal
}

{{- if eq .ProxyType "ChannelProxy" }}
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
{{- end }}

type {{ .EventProxyName }} _bindings.{{ .ProxyType }}
{{ range .Methods }}
{{- if .IsEvent }}
func (p *{{ $.EventProxyName }}) {{ .Name }}(
	{{- range $index, $m := .Response.Members -}}
		{{- if $index -}}, {{- end -}}
		{{ $m.PrivateName }} {{ $m.Type }}
	{{- end -}}
	) error {

	{{- if .Response }}
	{{- if len .Response.Members }}
	event_ := &{{ .Response.Name }}{
		{{- range .Response.Members }}
		{{ .Name }}: {{ .PrivateName }},
		{{- end }}
	}
	{{- else }}
	var event_ _bindings.Message
	{{- end }}
	{{- end }}
	return ((*_bindings.{{ $.ProxyType }})(p)).SendNew({{ .OrdinalName }}, event_)
}
{{- end }}
{{- end }}

{{ end -}}
`
