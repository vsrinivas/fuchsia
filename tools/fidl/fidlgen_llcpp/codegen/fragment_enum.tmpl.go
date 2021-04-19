// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentEnumTmpl = `
{{- define "EnumForwardDeclaration" }}
{{ EnsureNamespace . }}
{{ if .IsStrict }}
{{ .Docs }}
enum class {{ .Name }} : {{ .Type }} {
  {{- range .Members }}
    {{ .Docs }}
    {{ .Name }} = {{ .Value }},
  {{- end }}
  {{- if (or (eq .Namespace.String "::fuchsia_hardware_radar::wire")
           (or (eq .Namespace.String "::fuchsia_hardware_goldfish::wire")
               (eq .Namespace.String "::fuchsia_sysmem::wire" ) ) )
  }}
    // Generate old names temporarily for out-of-tree users
    {{- range .Members }}
      {{ .Natural.Name }} = {{ .Name }},
    {{- end }}
  {{- end }}
};
{{ else }}
{{ .Docs }}
class {{ .Name }} final {
public:
  constexpr {{ .Name }}() : value_(0) {}
  constexpr explicit {{ .Name }}({{ .Type }} value) : value_(value) {}
  constexpr {{ .Name }}(const {{ .Name }}& other) = default;
  constexpr operator {{ .Type }}() const { return value_; }

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
    {{ .Docs }}
    static const {{ $.Name }} {{ .Name }};
  {{- end }}

private:
  {{ .Type }} value_;
};

{{- range $member := .Members }}
constexpr const {{ $ }} {{ $.Name }}::{{ $member.Name }} =
  {{ $ }}({{ $member.Value }});
{{- end }}
{{ end }}
{{ end }}

{{- define "EnumTraits" }}

template <>
struct IsFidlType<{{ . }}> : public std::true_type {};

{{- end }}
`
