// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentEnumTmpl = `
{{- define "EnumForwardDeclaration" }}
{{ if .IsStrict }}
{{range .DocComments}}
//{{ . }}
{{- end}}
enum class {{ .Name }} : {{ .Type }} {
  {{- range .Members }}
  {{range .DocComments}}
  //{{ . }}
  {{- end}}
  {{ .Name }} = {{ .Value }},
  {{- end }}
};
{{ else }}
{{range .DocComments}}
//{{ . }}
{{- end}}
class {{ .Name }} final {
public:
  constexpr {{ .Name }}() : value_(0) {}
  constexpr explicit {{ .Name }}({{ .Type }} value) : value_(value) {}
  constexpr {{ .Name }}(const {{ .Name }}& other) = default;
  constexpr operator {{ .Type }}() const { return value_; };

  constexpr bool IsUnknown() const {
    switch (value_) {
      {{ range .Members }}
      {{ if not .IsUnknown }}
    case {{ .Value }}:
      {{ end }}
      {{ end }}
      return false;
    default:
      return true;
    }
  }

  constexpr static {{ .Name }} Unknown() {
    return {{ .Name }}({{ .UnknownValueForTmpl | printf "%#x" }});
  }

  {{- range .Members }}
  {{range .DocComments}}
  //{{ . }}
  {{- end}}
  static const {{ $.Name }} {{ .Name }};
  {{- end }}

private:
  {{ .Type }} value_;
};

{{- range $member := .Members }}
constexpr const {{ $.Namespace }}::{{ $.Name }} {{ $.Name }}::{{ $member.Name }} = {{ $.Namespace }}::{{ $.Name }}({{ $member.Value }});
{{- end }}
{{ end }}

{{ end }}

{{- define "EnumTraits" }}

template <>
struct IsFidlType<{{ .Namespace }}::{{ .Name }}> : public std::true_type {};

{{- end }}
`
