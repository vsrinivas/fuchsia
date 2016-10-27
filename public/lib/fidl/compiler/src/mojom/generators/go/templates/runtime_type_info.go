// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

import (
	"text/template"
)

const getRuntimeTypeInfoTmplText = `
{{- define "GetRuntimeTypeInfo" -}}
{{- if GenTypeInfo -}}
{{- $fileTmpl := . -}}
// This global variable contains a mojom_types.RuntimeTypeInfo struct
// describing the types defined in this file and all of its imports as
// well as the top-level interfaces defined in this file.
var runtimeTypeInfo__ = {{TypesPkg}}RuntimeTypeInfo{}

func getRuntimeTypeInfo() {{TypesPkg}}RuntimeTypeInfo {
  if runtimeTypeInfo__.TypeMap == nil {
    initRuntimeTypeInfo()
  }
  return runtimeTypeInfo__
}

func initRuntimeTypeInfo() {
  // serializedRuntimeTypeInfo contains the bytes of the Mojo serialization of
  // a mojom_types.RuntimeTypeInfo struct describing the Mojom types in this file.
  // The string contains the base64 encoding of the gzip-compressed bytes.
  serializedRuntimeTypeInfo := "{{$fileTmpl.SerializedRuntimeTypeInfo}}"

  // Deserialize RuntimeTypeInfo
  compressedBytes, err := base64.StdEncoding.DecodeString(serializedRuntimeTypeInfo)
  if err != nil {
    panic(fmt.Sprintf("Error while base64Decoding runtimeTypeInfo: %s", err.Error()))
  }
  reader, err := gzip.NewReader(bytes.NewBuffer(compressedBytes))
  if err != nil {
    panic(fmt.Sprintf("Error while decompressing runtimeTypeInfo: %s", err.Error()))
  }
  uncompressedBytes, err := ioutil.ReadAll(reader)
  if err != nil {
     panic(fmt.Sprintf("Error while decompressing runtimeTypeInfo: %s", err.Error()))
  }
  if err = reader.Close(); err != nil {
    panic(fmt.Sprintf("Error while decompressing runtimeTypeInfo: %s", err.Error()))
  }
  decoder := bindings.NewDecoder(uncompressedBytes, nil)
  runtimeTypeInfo__.Decode(decoder)

  {{ range $pkg := $fileTmpl.MojomImports }}
  for s, udt := range {{$pkg}}.GetAllMojomTypeDefinitions() {
    runtimeTypeInfo__.TypeMap[s] = udt
  }
  {{end}}
}
func GetAllMojomTypeDefinitions() map[string]{{TypesPkg}}UserDefinedType {
  return getRuntimeTypeInfo().TypeMap
}
{{- end -}}
{{- end -}}
`

const staticMojomTypeAccessorTmplText = `
{{- define "StaticRuntimeTypeAccessor" -}}
{{- $type := . -}}
{{- if and GenTypeInfo $type.TypeKey -}}
// {{$type.Name}}MojomType returns the UserDefinedType that describes the Mojom
// type of {{$type.Name}}. To obtain the UserDefinedType for Mojom types
// recursively contained in the returned UserDefinedType, look in the map
// returned by the function GetAllMojomTypeDefinitions().
func {{$type.Name}}MojomType() {{TypesPkg}}UserDefinedType {
	return GetAllMojomTypeDefinitions()["{{$type.TypeKey}}"]
}
{{- end -}}
{{- end -}}
`

const mojomTypeAccessorTmplText = `
{{- define "RuntimeTypeAccessor" -}}
{{- if GenTypeInfo -}}
{{- $type := . -}}
// MojomType returns the UserDefinedType that describes the Mojom
// type of this object. To obtain the UserDefinedType for Mojom types recursively
// contained in the returned UserDefinedType, look in the map returned
// by the function AllMojomTypes().
func (*{{$type.ConcreteName}}) MojomType() {{TypesPkg}}UserDefinedType {
	return {{$type.InterfaceName}}MojomType()
}

// AllMojomTypes returns a map that contains the UserDefinedType for
// all Mojom types in the complete type graph of the Mojom type of this object.
func (*{{$type.ConcreteName}}) AllMojomTypes() map[string]{{TypesPkg}}UserDefinedType {
	return GetAllMojomTypeDefinitions()
}
{{- end -}}
{{- end -}}
`

const mojomTypeAccessorsTmplText = `
{{- define "RuntimeTypeAccessors" -}}
{{- $type := . -}}
{{ template "StaticRuntimeTypeAccessor" $type }}
{{- if and GenTypeInfo $type.TypeKey -}}
{{ template "RuntimeTypeAccessor" $type }}
{{- end -}}
{{- end -}}
`

func initRuntimeTypeInfoTemplates() {
	template.Must(goFileTmpl.Parse(getRuntimeTypeInfoTmplText))
	template.Must(goFileTmpl.Parse(staticMojomTypeAccessorTmplText))
	template.Must(goFileTmpl.Parse(mojomTypeAccessorTmplText))
	template.Must(goFileTmpl.Parse(mojomTypeAccessorsTmplText))
}
