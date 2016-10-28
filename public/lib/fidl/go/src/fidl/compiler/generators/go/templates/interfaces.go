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

{{ template "ServiceDescription" $interface }}

{{- range $enum := $interface.NestedEnums }}
{{ template "EnumDecl" $enum }}
{{- end}}
{{- end -}}
`

const interfaceDeclTmplText = `
{{- define "InterfaceDecl" -}}
{{- $interface := . -}}
{{ template "InterfaceInterfaceDecl" $interface }}

{{ template "InterfaceOtherDecl" $interface }}

{{ template "MethodOrdinals" $interface }}
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

type {{$interface.Name}}_Pointer bindings.InterfacePointer

type {{$interface.Name}}_ServiceFactory struct{
	Delegate {{$interface.Name}}_Factory
}

type {{$interface.Name}}_Factory interface {
	Create(request {{$interface.Name}}_Request)
}

{{/* TODO(azani) This should only be defined for interfaces that have a ServiceName. */}}
func (f *{{$interface.Name}}_ServiceFactory) ServiceDescription() {{DescPkg}}ServiceDescription {
	return &{{$interface.Name}}_ServiceDescription{}
}

func (f *{{$interface.Name}}_ServiceFactory) Create(messagePipe system.MessagePipeHandle) {
	request := {{$interface.Name}}_Request{bindings.NewMessagePipeHandleOwner(messagePipe)}
	f.Delegate.Create(request)
}

// CreateMessagePipeFor{{$interface.Name}} creates a message pipe for use with the
// {{$interface.Name}} interface with a {{$interface.Name}}_Request on one end and a {{$interface.Name}}_Pointer on the other.
func CreateMessagePipeFor{{$interface.Name}}() ({{$interface.Name}}_Request, {{$interface.Name}}_Pointer) {
        r, p := bindings.CreateMessagePipeForMojoInterface()
        return {{$interface.Name}}_Request(r), {{$interface.Name}}_Pointer(p)
}

type {{$interface.Name}}_Proxy struct {
	router *bindings.Router
	ids bindings.Counter
}

func New{{$interface.Name}}Proxy(p {{$interface.Name}}_Pointer, waiter bindings.AsyncWaiter) *{{$interface.Name}}_Proxy {
	return &{{$interface.Name}}_Proxy{
		bindings.NewRouter(p.PassMessagePipe(), waiter),
		bindings.NewCounter(),
	}
}

func (p *{{$interface.Name}}_Proxy) Close_Proxy() {
	p.router.Close()
}

type {{$interface.PrivateName}}_Stub struct {
	connector *bindings.Connector
	impl {{$interface.Name}}
}

func New{{$interface.Name}}Stub(r {{$interface.Name}}_Request, impl {{$interface.Name}}, waiter bindings.AsyncWaiter) *bindings.Stub {
	connector := bindings.NewConnector(r.PassMessagePipe(), waiter)
	return bindings.NewStub(connector, &{{$interface.PrivateName}}_Stub{connector, impl})
}

func (f *{{$interface.Name}}_Request) ServiceDescription() {{DescPkg}}ServiceDescription {
	return &{{$interface.Name}}_ServiceDescription{}
}
{{- end -}}
`

const serviceDescriptionTmplText = `
{{- define "ServiceDescription" -}}
{{- $interface := . -}}
type {{$interface.Name}}_ServiceDescription struct{}

{{if not GenTypeInfo}}
func (sd *{{$interface.Name}}_ServiceDescription) GetTopLevelInterface() (outMojomInterface {{TypesPkg}}MojomInterface, err error) {
	err = fmt.Errorf("GetTopLevelInterface not implemented")
	return
}

func (sd *{{$interface.Name}}_ServiceDescription) GetTypeDefinition(inTypeKey string) (outType {{TypesPkg}}UserDefinedType, err error) {
	err = fmt.Errorf("GetTypeDefinition not implemented")
	return
}

func (sd *{{$interface.Name}}_ServiceDescription) GetAllTypeDefinitions() (outDefinitions *map[string]{{TypesPkg}}UserDefinedType, err error) {
	err = fmt.Errorf("GetAllTypeDefinitions not implemented")
	return
}
{{else}}
func (sd *{{$interface.Name}}_ServiceDescription) GetTopLevelInterface() (outMojomInterface {{TypesPkg}}MojomInterface, err error) {
	interfaceTypeKey := getRuntimeTypeInfo().Services["{{$interface.ServiceName}}"]
	userDefinedType := getRuntimeTypeInfo().TypeMap[interfaceTypeKey].(*{{TypesPkg}}UserDefinedTypeInterfaceType)
	return userDefinedType.Value, nil
}

func (sd *{{$interface.Name}}_ServiceDescription) GetTypeDefinition(inTypeKey string) (outType {{TypesPkg}}UserDefinedType, err error) {
	if udt, ok := GetAllMojomTypeDefinitions()[inTypeKey]; ok {
		return udt, nil
	}
	return nil, fmt.Errorf("%s_ServiceDescription does not recognize %s", "{{$interface.Name}}", inTypeKey)
}

func (sd *{{$interface.Name}}_ServiceDescription) GetAllTypeDefinitions() (outDefinitions *map[string]{{TypesPkg}}UserDefinedType, err error) {
	o := GetAllMojomTypeDefinitions()
	return &o, nil
}
{{end}}

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
		p.Close_Proxy()
		return
	}
{{if $method.ResponseParams}}
	readResult := <-p.router.AcceptWithResponse(message)
	if err = readResult.Error; err != nil {
		p.Close_Proxy()
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
		p.Close_Proxy()
		return
	}
{{- range $param := $method.ResponseParams.Fields}}
	out{{$param.Name}} = response.{{$param.Name}}
{{- end -}}
{{- else -}}
	if err = p.router.Accept(message); err != nil {
		p.Close_Proxy()
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

func (r *{{$interface.Name}}_Request) Name() string {
	return {{$interface.PrivateName}}_Name
}

func (p *{{$interface.Name}}_Pointer) Name() string {
	return {{$interface.PrivateName}}_Name
}

func (f *{{$interface.Name}}_ServiceFactory) Name() string {
	return {{$interface.PrivateName}}_Name
}
{{- end -}}
`

func initInterfaceTemplates() {
	template.Must(goFileTmpl.Parse(interfaceTmplText))
	template.Must(goFileTmpl.Parse(interfaceDeclTmplText))
	template.Must(goFileTmpl.Parse(interfaceInterfaceDeclTmplText))
	template.Must(goFileTmpl.Parse(interfaceOtherDeclTmplText))
	template.Must(goFileTmpl.Parse(serviceDescriptionTmplText))
	template.Must(goFileTmpl.Parse(serviceDeclTmplText))
	template.Must(goFileTmpl.Parse(methodOrdinalsTmplText))
	template.Must(goFileTmpl.Parse(methodParamsTmplText))
	template.Must(goFileTmpl.Parse(methodSignatureTmplText))
	template.Must(goFileTmpl.Parse(methodFuncTmplText))
	template.Must(goFileTmpl.Parse(methodTmplText))
	template.Must(goFileTmpl.Parse(acceptMethodTmplText))
}
