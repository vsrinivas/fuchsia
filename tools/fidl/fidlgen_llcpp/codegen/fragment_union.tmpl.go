// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentUnionTmpl = `
{{- define "UnionForwardDeclaration" }}
{{ EnsureNamespace . }}
class {{ .Name }};
{{- end }}

{{/* TODO(fxbug.dev/36441): Remove __Fuchsia__ ifdefs once we have non-Fuchsia
     emulated handles for C++. */}}
{{- define "UnionDeclaration" }}
{{ EnsureNamespace . }}
{{ if .IsResourceType }}
#ifdef __Fuchsia__
{{- PushNamespace }}
{{- end }}
extern "C" const fidl_type_t {{ .CodingTableType }};
{{ range .DocComments }}
//{{ . }}
{{- end }}
class {{ .Name }} {
  public:
  {{ .Name }}() : ordinal_({{ .WireInvalidOrdinal }}), envelope_{} {}

  {{ .Name }}({{ .Name }}&&) = default;
  {{ .Name }}& operator=({{ .Name }}&&) = default;

  enum class {{ .TagEnum.Self }} : fidl_xunion_tag_t {
  {{- range .Members }}
    {{ .TagName.Self }} = {{ .Ordinal }},  // {{ .Ordinal | printf "%#x" }}
  {{- end }}
  {{- if .IsFlexible }}
    {{ .TagUnknown.Self }} = ::std::numeric_limits<::fidl_union_tag_t>::max(),
  {{- end }}
  };

  bool has_invalid_tag() const { return ordinal_ == {{ .WireInvalidOrdinal }}; }

  {{- range $index, $member := .Members }}

  bool is_{{ .Name }}() const { return ordinal_ == {{ .WireOrdinalName }}; }

  static {{ $.Name }} With{{ .UpperCamelCaseName }}(::fidl::ObjectView<{{ .Type }}> val) {
    {{ $.Name }} result;
    result.set_{{ .Name }}(val);
    return result;
  }

  template <typename... Args>
  static {{ $.Name }} With{{ .UpperCamelCaseName }}(::fidl::AnyAllocator& allocator, Args&&... args) {
    {{ $.Name }} result;
    result.set_{{ .Name }}(::fidl::ObjectView<{{ .Type }}>(allocator,
                           std::forward<Args>(args)...));
    return result;
  }
{{ "" }}
  {{- range .DocComments }}
  //{{ . }}
  {{- end }}
  void set_{{ .Name }}(::fidl::ObjectView<{{ .Type }}> elem) {
    ordinal_ = {{ .WireOrdinalName }};
    envelope_.data = ::fidl::ObjectView<void>::FromExternal(static_cast<void*>(elem.get()));
  }

  template <typename... Args>
  void set_{{ .Name }}(::fidl::AnyAllocator& allocator, Args&&... args) {
    ordinal_ = {{ .WireOrdinalName }};
    set_{{ .Name }}(::fidl::ObjectView<{{ .Type }}>(allocator, std::forward<Args>(args)...));
  }
{{ "" }}
  {{- range .DocComments }}
  //{{ . }}
  {{- end }}
  {{ .Type }}& mutable_{{ .Name }}() {
    ZX_ASSERT(ordinal_ == {{ .WireOrdinalName }});
    return *static_cast<{{ .Type }}*>(envelope_.data.get());
  }
  const {{ .Type }}& {{ .Name }}() const {
    ZX_ASSERT(ordinal_ == {{ .WireOrdinalName }});
    return *static_cast<{{ .Type }}*>(envelope_.data.get());
  }
  {{- end }}

  {{- if .IsFlexible }}
  {{ .TagEnum }} which() const;
  {{- else }}
  {{ .TagEnum }} which() const {
    ZX_ASSERT(!has_invalid_tag());
    return static_cast<{{ .TagEnum }}>(ordinal_);
  }
  {{- end }}

  static constexpr const fidl_type_t* Type = &{{ .CodingTableType }};
  static constexpr uint32_t MaxNumHandles = {{ .MaxHandles }};
  static constexpr uint32_t PrimarySize = {{ .InlineSize }};
  [[maybe_unused]]
  static constexpr uint32_t MaxOutOfLine = {{ .MaxOutOfLine }};
  static constexpr bool HasPointer = {{ .HasPointer }};

  {{- if .IsResourceType }}

  void _CloseHandles();
  {{- end }}

 private:
  enum class {{ .WireOrdinalEnum.Self }} : fidl_xunion_tag_t {
    {{ .WireInvalidOrdinal.Self }} = 0,
  {{- range .Members }}
    {{ .WireOrdinalName.Self }} = {{ .Ordinal }},  // {{ .Ordinal | printf "%#x" }}
  {{- end }}
  };

  static void SizeAndOffsetAssertionHelper();

  {{- /* All fields are private to maintain standard layout */}}
  {{ .WireOrdinalEnum }} ordinal_;
  FIDL_ALIGNDECL
  ::fidl::Envelope<void> envelope_;
};

{{- if .IsResourceType }}
{{- PopNamespace }}
#endif  // __Fuchsia__
{{- end }}
{{- end }}

{{/* TODO(fxbug.dev/36441): Remove __Fuchsia__ ifdefs once we have non-Fuchsia
     emulated handles for C++. */}}
{{- define "UnionDefinition" }}
{{- if .IsResourceType }}
#ifdef __Fuchsia__
{{- PushNamespace }}
{{- end }}
{{- if .IsFlexible }}
auto {{ . }}::which() const -> {{ .TagEnum }} {
  ZX_ASSERT(!has_invalid_tag());
  switch (ordinal_) {
  {{- range .Members }}
  case {{ .WireOrdinalName }}:
  {{- end }}
    return static_cast<{{ .TagEnum }}>(ordinal_);
  default:
    return {{ .TagUnknown }};
  }
}
{{- end }}

void {{ . }}::SizeAndOffsetAssertionHelper() {
  static_assert(sizeof({{ .Name }}) == sizeof(fidl_xunion_t));
  static_assert(offsetof({{ .Name }}, ordinal_) == offsetof(fidl_xunion_t, tag));
  static_assert(offsetof({{ .Name }}, envelope_) == offsetof(fidl_xunion_t, envelope));
}

{{- if .IsResourceType }}
void {{ . }}::_CloseHandles() {
  switch (ordinal_) {
  {{- range .Members }}
    {{- if .Type.IsResource }}
      case {{ .WireOrdinalName }}: {
        {{- CloseHandles . false true }}
        break;
      }
    {{- end }}
  {{- end }}
  default:
    break;
  }
}
{{- end }}

{{- if .IsResourceType }}
{{- PopNamespace }}
#endif  // __Fuchsia__
{{- end }}
{{- end }}

{{/* TODO(fxbug.dev/36441): Remove __Fuchsia__ ifdefs once we have non-Fuchsia
     emulated handles for C++. */}}
{{- define "UnionTraits" }}
{{ if .IsResourceType }}
#ifdef __Fuchsia__
{{- PushNamespace }}
{{- end }}
template <>
struct IsFidlType<{{ . }}> : public std::true_type {};
template <>
struct IsUnion<{{ . }}> : public std::true_type {};
static_assert(std::is_standard_layout_v<{{ . }}>);
{{- if .IsResourceType }}
{{- PopNamespace }}
#endif  // __Fuchsia__
{{- end }}
{{- end }}
`
