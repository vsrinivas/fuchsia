// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentEnumTmpl = `
{{- define "EnumForwardDeclaration" }}
{{ EnsureNamespace .Decl.Wire }}
{{ if .IsStrict }}
{{range .DocComments}}
//{{ . }}
{{- end}}
enum class {{ .Decl.Wire.Name }} : {{ .Type.Wire }} {
  {{- range .Members }}
  {{range .DocComments}}
  //{{ . }}
  {{- end}}
  {{ .Name }} = {{ .Value.Wire }},
  {{- end }}
};
{{ else }}
{{range .DocComments}}
//{{ . }}
{{- end}}
class {{ .Decl.Wire.Name }} final {
public:
  constexpr {{ .Decl.Wire.Name }}() : value_(0) {}
  constexpr explicit {{ .Decl.Wire.Name }}({{ .Type.Wire }} value) : value_(value) {}
  constexpr {{ .Decl.Wire.Name }}(const {{ .Decl.Wire.Name }}& other) = default;
  constexpr operator {{ .Type.Wire }}() const { return value_; }

  constexpr bool IsUnknown() const {
    switch (value_) {
      {{ range .Members }}
      {{ if not .IsUnknown }}
    case {{ .Value.Wire }}:
      {{ end }}
      {{ end }}
      return false;
    default:
      return true;
    }
  }

  constexpr static {{ .Decl.Wire.Name }} Unknown() {
    return {{ .Decl.Wire.Name }}({{ .UnknownValueForTmpl | printf "%#x" }});
  }

  {{- range .Members }}
  {{range .DocComments}}
  //{{ . }}
  {{- end}}
  static const {{ $.Decl.Wire.Name }} {{ .Name }};
  {{- end }}

private:
  {{ .Type.Wire }} value_;
};

{{- range $member := .Members }}
constexpr const {{ $.Decl.Wire }} {{ $.Decl.Wire.Name }}::{{ $member.Name }} =
  {{ $.Decl.Wire }}({{ $member.Value.Wire }});
{{- end }}
{{ end }}

}  // namespace wire
using {{ .Decl.Wire.Name }} = wire::{{ .Decl.Wire.Name }};
namespace wire {

{{ end }}

{{- define "EnumTraits" }}

template <>
struct IsFidlType<{{ .Decl.Wire }}> : public std::true_type {};

{{- end }}
`
