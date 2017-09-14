// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

import (
	"text/template"
)

const interfaceTmplText = `
{{- define "Interface" -}}
{{$interface := . -}}
{{ template "InterfaceDecl" $interface }}

{{ template "RuntimeTypeAccessors" $interface }}

{{- range $method := $interface.Methods}}
{{ template "Method" $method }}
{{- end}}

{{- if $interface.ServiceName}}
{{ template "ServiceDecl" $interface }}
{{- end}}

{{- end -}}
`

const interfaceDeclTmplText = `
{{- define "InterfaceDecl" -}}
{{- $interface := . -}}
{{ template "InterfaceConstantsDecl" $interface }}

{{- range $enum := $interface.NestedEnums }}
{{ template "EnumDecl" $enum }}
{{- end}}

{{ template "InterfaceInterfaceDecl" $interface }}

{{ template "InterfaceOtherDecl" $interface }}

{{ template "MethodOrdinals" $interface }}
{{- end -}}
`
const interfaceConstantsDeclTmplText = `
{{- define "InterfaceConstantsDecl" -}}
{{- $interface := . -}}
{{- range $constant := $interface.NestedConstants }}
const {{$constant.Name}} {{$constant.Type}} = {{$constant.Value}}
{{- end -}}
{{- end -}}
`

const interfaceInterfaceDeclTmplText = `
{{- define "InterfaceInterfaceDecl" -}}
{{- $interface := . -}}
type {{$interface.Name}} interface {
{{- range $method := $interface.Methods -}}
{{ template "MethodSignature" $method }}
{{end -}}
}
{{- end -}}
`

const interfaceOtherDeclTmplText = `
{{- define "InterfaceOtherDecl" -}}
{{- $interface := . -}}
type {{$interface.Name}}_Request bindings.InterfaceRequest

func (r {{$interface.Name}}_Request) PassChannel() zx.Handle {
	i := bindings.InterfaceRequest(r)
	return i.PassChannel()
}

func (r {{$interface.Name}}_Request) NewStub(impl {{$interface.Name}}, waiter bindings.AsyncWaiter) *bindings.Stub {
	return NewStubFor{{$interface.Name}}(r, impl, waiter)
}

type {{$interface.Name}}_Pointer bindings.InterfacePointer

type {{$interface.Name}}_ServiceBinder struct{
	Delegate {{$interface.Name}}_Binder
}

type {{$interface.Name}}_Binder interface {
	Bind(request {{$interface.Name}}_Request)
}

func (f *{{$interface.Name}}_ServiceBinder) Bind(handle zx.Handle) {
	request := {{$interface.Name}}_Request{bindings.NewChannelHandleOwner(handle)}
	f.Delegate.Bind(request)
}

// NewChannelFor{{$interface.Name}} creates a channel for use with the {{$interface.Name}} interface with
// a {{$interface.Name}}_Request on one end and a {{$interface.Name}}_Pointer on the other.
func NewChannelFor{{$interface.Name}}() ({{$interface.Name}}_Request, {{$interface.Name}}_Pointer) {
        r, p := bindings.CreateChannelForFidlInterface()
        return {{$interface.Name}}_Request(r), {{$interface.Name}}_Pointer(p)
}

type {{$interface.Name}}_Proxy struct {
	router *bindings.Router
	ids bindings.Counter
}

func NewProxyFor{{$interface.Name}}(p {{$interface.Name}}_Pointer, waiter bindings.AsyncWaiter) *{{$interface.Name}}_Proxy {
	return &{{$interface.Name}}_Proxy{
		bindings.NewRouter(p.PassChannel(), waiter),
		bindings.NewCounter(),
	}
}

func (p *{{$interface.Name}}_Proxy) Close() {
	p.router.Close()
}

func (p *{{$interface.Name}}_Proxy) NewRequest(waiter bindings.AsyncWaiter) ({{$interface.Name}}_Request, *{{$interface.Name}}_Proxy) {
	r, ptr := NewChannelFor{{$interface.Name}}()
	if p != nil {
		p.Close()
	}
	p = NewProxyFor{{$interface.Name}}(ptr, waiter)
	return r, p
}

type {{$interface.PrivateName}}_Stub struct {
	connector *bindings.Connector
	impl {{$interface.Name}}
}

func NewStubFor{{$interface.Name}}(r {{$interface.Name}}_Request, impl {{$interface.Name}}, waiter bindings.AsyncWaiter) *bindings.Stub {
	connector := bindings.NewConnector(r.PassChannel(), waiter)
	return bindings.NewStub(connector, &{{$interface.PrivateName}}_Stub{connector, impl})
}

func (s *{{$interface.PrivateName}}_Stub) Accept(message *bindings.Message) (err error) {
	switch message.Header.Type {
{{- range $method := $interface.Methods}}
{{ template "AcceptMethod" $method }}
{{- end}}
	default:
		return &bindings.ValidationError{
			bindings.MessageHeaderUnknownMethod,
			fmt.Sprintf("unknown method %v", message.Header.Type),
		}
  }
  return
}
{{- end -}}
`

const acceptMethodTmplText = `
{{- define "AcceptMethod" -}}
{{- $method := . -}}
case {{$method.FullName}}_Ordinal:
{{ if $method.ResponseParams -}}
  if message.Header.Flags != bindings.MessageExpectsResponseFlag {
{{ else }}
  if message.Header.Flags != bindings.MessageNoFlag {
{{ end }}
    return &bindings.ValidationError{bindings.MessageHeaderInvalidFlags,
      fmt.Sprintf("invalid message header flag: %v", message.Header.Flags),
    }
  }
  var request {{$method.FullName}}_Params
  if err := message.DecodePayload(&request); err != nil {
    return err
  }
{{ if $method.ResponseParams -}}
var response {{$method.FullName}}_ResponseParams
{{range $field := $method.ResponseParams.Fields -}}
response.{{$field.Name}},
{{- end -}}
{{- end -}}
  err = s.impl.{{$method.MethodName}}(
{{- range $field := $method.Params.Fields -}}
request.{{$field.Name}},
{{- end -}}
  )
  if err != nil {
    return
  }
{{ if $method.ResponseParams }}
  header := bindings.MessageHeader{
    Type: {{$method.FullName}}_Ordinal,
    Flags: bindings.MessageIsResponseFlag,
    RequestId: message.Header.RequestId,
  }
  message, err = bindings.EncodeMessage(header, &response)
  if err != nil {
    return err
  }
  return s.connector.WriteMessage(message)
{{ end }}
{{- end -}}
`

const methodTmplText = `
{{- define "Method" -}}
{{- $method := . -}}

{{ template "MethodParams" $method }}

{{ template "MethodFunction" $method }}
{{- end -}}
`

const methodOrdinalsTmplText = `
{{- define "MethodOrdinals" -}}
{{- $interface := . -}}
{{- range $method := $interface.Methods -}}
const {{$method.FullName}}_Ordinal uint32 = {{$method.Ordinal}}
{{end -}}
{{- end -}}
`

const methodParamsTmplText = `
{{- define "MethodParams" -}}
{{- $interface := . -}}
{{ template "Struct" $interface.Params }}

{{- if $interface.ResponseParams}}
{{ template "Struct" $interface.ResponseParams }}
{{- end -}}
{{- end -}}
`

const methodSignatureTmplText = `
{{- define "MethodSignature" -}}
{{- $method := . -}}
{{$method.MethodName}}(
{{- range $field := $method.Params.Fields -}}
in{{$field.Name}} {{$field.Type}}, 
{{- end -}}
) (
{{- if $method.ResponseParams -}}
{{- range $field := $method.ResponseParams.Fields -}}
out{{$field.Name}} {{$field.Type}},
{{- end -}}
{{- end -}}
err error)
{{- end -}}
`

const methodFuncTmplText = `
{{- define "MethodFunction" -}}
{{- $method := . -}}
func (p *{{$method.Interface.Name}}_Proxy) {{ template "MethodSignature" $method }} {
	payload := &{{$method.Params.Name}}{
{{range $param := $method.Params.Fields -}}
		in{{$param.Name}},
{{end}}
	}

	header := bindings.MessageHeader{
		Type: {{$method.FullName}}_Ordinal,
{{- if $method.ResponseParams}}
		Flags: bindings.MessageExpectsResponseFlag,
		RequestId: p.ids.Count(),
{{- else}}
		Flags: bindings.MessageNoFlag,
{{- end}}
	}
	var message *bindings.Message
	if message, err = bindings.EncodeMessage(header, payload); err != nil {
		err = fmt.Errorf("can't encode request: %v", err.Error())
		p.Close()
		return
	}
{{if $method.ResponseParams}}
	readResult := <-p.router.AcceptWithResponse(message)
	if err = readResult.Error; err != nil {
		p.Close()
		return
	}
	if readResult.Message.Header.Flags != bindings.MessageIsResponseFlag {
		err = &bindings.ValidationError{bindings.MessageHeaderInvalidFlags,
			fmt.Sprintf("invalid message header flag: %v", readResult.Message.Header.Flags),
		}
		return
	}
	if got, want := readResult.Message.Header.Type, {{$method.FullName}}_Ordinal; got != want {
		err = &bindings.ValidationError{bindings.MessageHeaderUnknownMethod,
			fmt.Sprintf("invalid method in response: expected %v, got %v", want, got),
		}
		return
	}
	var response {{$method.ResponseParams.Name}}
	if err = readResult.Message.DecodePayload(&response); err != nil {
		p.Close()
		return
	}
{{- range $param := $method.ResponseParams.Fields}}
	out{{$param.Name}} = response.{{$param.Name}}
{{- end -}}
{{- else -}}
	if err = p.router.Accept(message); err != nil {
		p.Close()
		return
	}
{{end}}
	return
}
{{- end -}}
`

const serviceDeclTmplText = `
{{- define "ServiceDecl" -}}
{{- $interface := . -}}
const {{$interface.PrivateName}}_Name string = "{{$interface.ServiceName}}"

func (r {{$interface.Name}}_Request) Name() string {
	return {{$interface.PrivateName}}_Name
}

func (p {{$interface.Name}}_Pointer) Name() string {
	return {{$interface.PrivateName}}_Name
}

func (f *{{$interface.Name}}_ServiceBinder) Name() string {
	return {{$interface.PrivateName}}_Name
}
{{- end -}}
`

func initInterfaceTemplates() {
	template.Must(goFileTmpl.Parse(interfaceTmplText))
	template.Must(goFileTmpl.Parse(interfaceDeclTmplText))
	template.Must(goFileTmpl.Parse(interfaceConstantsDeclTmplText))
	template.Must(goFileTmpl.Parse(interfaceInterfaceDeclTmplText))
	template.Must(goFileTmpl.Parse(interfaceOtherDeclTmplText))
	template.Must(goFileTmpl.Parse(serviceDeclTmplText))
	template.Must(goFileTmpl.Parse(methodOrdinalsTmplText))
	template.Must(goFileTmpl.Parse(methodParamsTmplText))
	template.Must(goFileTmpl.Parse(methodSignatureTmplText))
	template.Must(goFileTmpl.Parse(methodFuncTmplText))
	template.Must(goFileTmpl.Parse(methodTmplText))
	template.Must(goFileTmpl.Parse(acceptMethodTmplText))
}
