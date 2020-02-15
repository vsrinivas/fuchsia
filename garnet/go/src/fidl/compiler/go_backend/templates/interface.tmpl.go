// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Interface = `
{{- define "InterfaceDefinition" -}}

const (
{{- range .Methods }}
	{{- range .Ordinals.Reads }}
	{{ .Name }} uint64 = {{ .Ordinal }}
	{{- end }}
{{- end }}
)

type {{ .ProxyWithCtxName }} _bindings.{{ .ProxyType }}
{{ range .Methods }}
{{range .DocComments}}
//{{ . }}
{{- end}}
func (p *{{ $.ProxyWithCtxName }}) {{ if .IsEvent -}}
		{{ .EventExpectName }}
	{{- else -}}
		{{ .Name }}
	{{- end -}}
	(ctx_ _bindings.Context
	{{- if .RequestWithCtx -}}
	{{- range .RequestWithCtx.Members -}}
		, {{ .PrivateName }} {{ .Type }}
	{{- end -}}
	{{- end -}}
	)
	{{- if .HasResponse -}}
	{{- if .ResponseWithCtx }} (
	{{- range .ResponseWithCtx.Members }}{{ .Type }}, {{ end -}}
		error)
	{{- else }} error{{ end -}}
	{{- else }} error{{ end }} {

	{{- if .HasRequest }}
	{{- if .RequestWithCtx }}
	req_ := &{{ .RequestWithCtx.Name }}{
		{{- range .RequestWithCtx.Members }}
		{{ .Name }}: {{ .PrivateName }},
		{{- end }}
	}
	{{- else }}
	var req_ _bindings.Message
	{{- end }}
	{{- end }}
	{{- if .HasResponse }}
	{{- if .ResponseWithCtx }}
	resp_ := &{{ .ResponseWithCtx.Name }}{}
	{{- else }}
	var resp_ _bindings.Message
	{{- end }}
	{{- end }}
	{{- if .HasRequest }}
		{{- if .HasResponse }}
	err_ := ((*_bindings.{{ $.ProxyType }})(p)).Call({{ .Ordinals.Write.Name }}, req_, resp_
		{{- range $index, $ordinal := .Ordinals.Reads -}}, {{ $ordinal.Name }}{{- end -}})
		{{- else }}
	err_ := ((*_bindings.{{ $.ProxyType }})(p)).Send({{ .Ordinals.Write.Name }}, req_)
		{{- end }}
	{{- else }}
		{{- if .HasResponse }}
	err_ := ((*_bindings.{{ $.ProxyType }})(p)).Recv(
		{{- with $first_ordinal := index .Ordinals.Reads 0 -}}
			{{- $first_ordinal.Name -}}
		{{- end -}}
		, resp_
		{{- range $index, $ordinal := .Ordinals.Reads -}}
			{{- if $index -}}, {{ $ordinal.Name }}{{- end -}}
		{{- end -}}
		)
		{{- else }}
	err_ := nil
		{{- end }}
	{{- end }}
	{{- if .HasResponse }}
	{{- if .ResponseWithCtx }}
	return {{ range .ResponseWithCtx.Members }}resp_.{{ .Name }}, {{ end }}err_
	{{- else }}
	return err_
	{{- end }}
	{{- else }}
	return err_
	{{- end }}
}
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
	{{- if .HasResponse -}}
	{{- if .Response }} (
	{{- range .Response.Members }}{{ .Type }}, {{ end -}}
		error)
	{{- else }} error{{ end -}}
	{{- else }} error{{ end }} {

	{{- if .HasRequest }}
	{{- if .Request }}
	req_ := &{{ .Request.Name }}{
		{{- range .Request.Members }}
		{{ .Name }}: {{ .PrivateName }},
		{{- end }}
	}
	{{- else }}
	var req_ _bindings.Message
	{{- end }}
	{{- end }}
	{{- if .HasResponse }}
	{{- if .Response }}
	resp_ := &{{ .Response.Name }}{}
	{{- else }}
	var resp_ _bindings.Message
	{{- end }}
	{{- end }}
	{{- if .HasRequest }}
		{{- if .HasResponse }}
	err_ := ((*_bindings.{{ $.ProxyType }})(p)).Call({{ .Ordinals.Write.Name }}, req_, resp_
		{{- range $index, $ordinal := .Ordinals.Reads -}}, {{ $ordinal.Name }}{{- end -}})
		{{- else }}
	err_ := ((*_bindings.{{ $.ProxyType }})(p)).Send({{ .Ordinals.Write.Name }}, req_)
		{{- end }}
	{{- else }}
		{{- if .HasResponse }}
	err_ := ((*_bindings.{{ $.ProxyType }})(p)).Recv(
		{{- with $first_ordinal := index .Ordinals.Reads 0 -}}
			{{- $first_ordinal.Name -}}
		{{- end -}}
		, resp_
		{{- range $index, $ordinal := .Ordinals.Reads -}}
			{{- if $index -}}, {{ $ordinal.Name }}{{- end -}}
		{{- end -}}
		)
		{{- else }}
	err_ := nil
		{{- end }}
	{{- end }}
	{{- if .HasResponse }}
	{{- if .Response }}
	return {{ range .Response.Members }}resp_.{{ .Name }}, {{ end }}err_
	{{- else }}
	return err_
	{{- end }}
	{{- else }}
	return err_
	{{- end }}
}
{{- end }}

{{range .DocComments}}
//{{ . }}
{{- end}}
type {{ .WithCtxName }} interface {
{{- range .Methods }}
	{{- range .DocComments}}
	//{{ . }}
	{{- end}}
	{{- if .HasRequest }}
	{{- if .RequestWithCtx }}
	{{ .Name }}(ctx_ _bindings.Context
	{{- range .RequestWithCtx.Members -}}
		, {{ .PrivateName }} {{ .Type }}
	{{- end -}}
	)
	{{- else }}
	{{ .Name }}(ctx_ _bindings.Context)
	{{- end }}
	{{- if .HasResponse -}}
	{{- if .ResponseWithCtx }} (
	{{- range .ResponseWithCtx.Members }}{{ .Type }}, {{ end -}}
		error)
	{{- else }} error{{ end -}}
	{{- else }} error{{ end }}
	{{- end }}
{{- end }}
}

{{ $transitionalBaseWithCtxName := .TransitionalBaseWithCtxName }}

type {{.TransitionalBaseWithCtxName}} struct {}

{{ range .Methods }}
{{- if and .IsTransitional .HasRequest }}
{{- if .RequestWithCtx }}
func (_ *{{$transitionalBaseWithCtxName}}) {{ .Name }} (ctx_ _bindings.Context
{{- range .RequestWithCtx.Members -}}
	, {{ .PrivateName }} {{ .Type }}
{{- end -}}
)
{{- else }}
func (_ *{{$transitionalBaseWithCtxName}}) {{ .Name }} (ctx_ _bindings.Context)
{{- end }}
{{- if .HasResponse -}}
	{{- if .ResponseWithCtx }} (
		{{- range .ResponseWithCtx.Members }}{{ .Type }}, {{ end -}}
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
{{- end }}
{{- end }}

{{range .DocComments}}
//{{ . }}
{{- end}}
type {{ .Name }} interface {
{{- range .Methods }}
	{{- range .DocComments}}
	//{{ . }}
	{{- end}}
	{{- if .HasRequest }}
	{{- if .Request }}
	{{ .Name }}(
	{{- range $index, $m := .Request.Members -}}
		{{- if $index -}}, {{- end -}}
		{{ $m.PrivateName }} {{ $m.Type }}
	{{- end -}}
	)
	{{- else }}
	{{ .Name }}()
	{{- end }}
	{{- if .HasResponse -}}
	{{- if .Response }} (
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
		{{- if $method.HasRequest }}
			{{- if $method.Request }}
			func (_ *{{$transitionalBaseName}}) {{ $method.Name }} (
			{{- range $index, $m := $method.Request.Members -}}
				{{- if $index -}}, {{- end -}}
				{{ $m.PrivateName }} {{ $m.Type }}
			{{- end -}}
			)
			{{- else }}
			func (_ *{{$transitionalBaseName}}) {{ $method.Name }} ()
			{{- end }}
			{{- if $method.HasResponse -}}
				{{- if $method.Response }} (
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
type {{ .RequestWithCtxName }} _bindings.InterfaceRequest

func New{{ .RequestWithCtxName }}() ({{ .RequestWithCtxName }}, *{{ .ProxyWithCtxName }}, error) {
	req, cli, err := _bindings.NewInterfaceRequest()
	return {{ .RequestWithCtxName }}(req), (*{{ .ProxyWithCtxName }})(cli), err
}

type {{ .RequestName }} _bindings.InterfaceRequest

func New{{ .RequestName }}() ({{ .RequestName }}, *{{ .ProxyName }}, error) {
	req, cli, err := _bindings.NewInterfaceRequest()
	return {{ .RequestName }}(req), (*{{ .ProxyName }})(cli), err
}

{{- if .ServiceNameString }}
// Implements ServiceRequest.
func (_ {{ .RequestWithCtxName }}) Name() string {
	return {{ .ServiceNameString }}
}
func (c {{ .RequestWithCtxName }}) ToChannel() _zx.Channel {
	return c.Channel
}

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

func (s_ *{{ .StubName }}) Dispatch(args_ _bindings.DispatchArgs) (_bindings.Message, bool, error) {
	ctx, ok := _bindings.GetMarshalerContext(args_.Ctx)
	if !ok {
		return nil, false, _bindings.ErrMissingMarshalerContext
	}
	return s_.DispatchImplWithCtx2(args_.Ordinal, ctx, args_.Bytes, args_.HandleInfos)
}

func (s_ *{{ .StubName }}) DispatchImplWithCtx2(ordinal_ uint64, ctx_ _bindings.MarshalerContext, data_ []byte, handleInfos_ []_zx.HandleInfo) (_bindings.Message, bool, error) {
	switch ordinal_ {
	{{- range .Methods }}
	{{- if not .IsEvent }}
	{{- range $index, $ordinal := .Ordinals.Reads }}
		{{- if $index }}
		fallthrough
		{{- end }}
	case {{ $ordinal.Name }}:
	{{- end }}
		{{- if .HasRequest }}
		{{- if .Request }}
		in_ := {{ .Request.Name }}{}
		if _, _, err_ := _bindings.UnmarshalWithContext2(ctx_, data_, handleInfos_, &in_); err_ != nil {
			return nil, false, err_
		}
		{{- end }}
		{{- end }}
		{{ if .Response }}
		{{- range .Response.Members }}{{ .PrivateName }}, {{ end -}}
		{{- end -}}
		err_ := s_.Impl.{{ .Name }}(
		{{- if .HasRequest }}
		{{- if .Request -}}
		{{- range $index, $m := .Request.Members -}}
		{{- if $index -}}, {{- end -}}
		in_.{{ $m.Name }}
		{{- end -}}
		{{- end -}}
		{{- end -}}
		)
		{{- if .HasResponse }}
		{{- if .Response }}
		out_ := {{ .Response.Name }}{}
		{{- range .Response.Members }}
		out_.{{ .Name }} = {{ .PrivateName }}
		{{- end }}
		return &out_, true, err_
		{{- else }}
		return nil, true, err_
		{{- end }}
		{{- else }}
		return nil, false, err_
		{{- end }}
	{{- end }}
	{{- end }}
	}
	return nil, false, _bindings.ErrUnknownOrdinal
}

type {{ .StubWithCtxName }} struct {
	Impl {{ .WithCtxName }}
}

func (s_ *{{ .StubWithCtxName }}) Dispatch(args_ _bindings.DispatchArgs) (_bindings.Message, bool, error) {
	switch args_.Ordinal {
	{{- range .Methods }}
	{{- if not .IsEvent }}
	{{- range $index, $ordinal := .Ordinals.Reads }}
		{{- if $index }}
		fallthrough
		{{- end }}
	case {{ $ordinal.Name }}:
	{{- end }}
		{{- if .HasRequest }}
		{{- if .RequestWithCtx }}
		in_ := {{ .RequestWithCtx.Name }}{}
		marshalerCtx, ok := _bindings.GetMarshalerContext(args_.Ctx)
		if !ok {
			return nil, false, _bindings.ErrMissingMarshalerContext
		}
		if _, _, err_ := _bindings.UnmarshalWithContext2(marshalerCtx, args_.Bytes, args_.HandleInfos, &in_); err_ != nil {
			return nil, false, err_
		}
		{{- end }}
		{{- end }}
		{{ if .ResponseWithCtx }}
		{{- range .ResponseWithCtx.Members }}{{ .PrivateName }}, {{ end -}}
		{{- end -}}
		err_ := s_.Impl.{{ .Name }}(args_.Ctx
		{{- if .HasRequest -}}
		{{- if .RequestWithCtx -}}
		{{- range $index, $m := .RequestWithCtx.Members -}}
		, in_.{{ $m.Name }}
		{{- end -}}
		{{- end -}}
		{{- end -}}
		)
		{{- if .HasResponse }}
		{{- if .ResponseWithCtx }}
		out_ := {{ .ResponseWithCtx.Name }}{}
		{{- range .ResponseWithCtx.Members }}
		out_.{{ .Name }} = {{ .PrivateName }}
		{{- end }}
		return &out_, true, err_
		{{- else }}
		return nil, true, err_
		{{- end }}
		{{- else }}
		return nil, false, err_
		{{- end }}
	{{- end }}
	{{- end }}
	}
	return nil, false, _bindings.ErrUnknownOrdinal
}

{{- if eq .ProxyType "ChannelProxy" }}
type {{ .ServerName }} struct {
	_bindings.BindingSet
}

func (s *{{ .ServerName }}) Add(impl {{ .Name }}, c _zx.Channel, onError func(error)) (_bindings.BindingKey, error) {
	return s.BindingSet.Add(&{{ .StubName }}{Impl: impl}, c, onError)
}

func (s *{{ .ServerName }}) AddWithCtx(impl {{ .WithCtxName }}, c _zx.Channel, onError func(error)) (_bindings.BindingKey, error) {
	return s.BindingSet.Add(&{{ .StubWithCtxName }}{Impl: impl}, c, onError)
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
	{{- if .Response -}}
	{{- range $index, $m := .Response.Members -}}
		{{- if $index -}}, {{- end -}}
		{{ $m.PrivateName }} {{ $m.Type }}
	{{- end -}}
	{{- end -}}
	) error {

	{{- if .HasResponse }}
	{{- if .Response }}
	event_ := &{{ .Response.Name }}{
		{{- range .Response.Members }}
		{{ .Name }}: {{ .PrivateName }},
		{{- end }}
	}
	{{- else }}
	var event_ _bindings.Message
	{{- end }}
	{{- end }}
	return ((*_bindings.{{ $.ProxyType }})(p)).Send({{ .Ordinals.Write.Name }}, event_)
}
{{- end }}
{{- end }}

{{ end -}}
`
