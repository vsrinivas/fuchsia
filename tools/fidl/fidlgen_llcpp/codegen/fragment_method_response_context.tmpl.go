// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

// fragmentMethodResponseContextTmpl contains the definition for
// fidl::WireResponseContext<Method>.
const fragmentMethodResponseContextTmpl = `
{{- define "Method:ResponseContext:MessagingHeader" }}
{{- EnsureNamespace "" }}
{{- IfdefFuchsia }}
template<>
class {{ .WireResponseContext }} : public ::fidl::internal::ResponseContext {
 public:
  {{ .WireResponseContext.Self }}();

  virtual void OnResult({{ .WireUnownedResult }}& result) = 0;

 private:
  ::cpp17::optional<::fidl::UnbindInfo> OnRawResult(::fidl::IncomingMessage&& msg) override;
};
{{- EndifFuchsia }}
{{- end }}



{{- define "Method:ResponseContext:MessagingSource" }}
{{- EnsureNamespace "" }}
{{- IfdefFuchsia }}
{{ .WireResponseContext }}::{{ .WireResponseContext.Self }}()
    : ::fidl::internal::ResponseContext({{ .OrdinalName }}) {}

::cpp17::optional<::fidl::UnbindInfo>
{{ .WireResponseContext.NoLeading }}::OnRawResult(::fidl::IncomingMessage&& msg) {
  if (unlikely(!msg.ok())) {
    {{ .WireUnownedResult }} result{msg.error()};
    OnResult(result);
    return cpp17::nullopt;
  }
  ::fidl::DecodedMessage<{{ .WireResponse }}> decoded{std::move(msg)};
  ::fidl::Result maybe_error = decoded;
  {{ .WireUnownedResult }} result{std::move(decoded)};
  OnResult(result);
  if (unlikely(!maybe_error.ok())) {
    return ::fidl::UnbindInfo(maybe_error);
  }
  return cpp17::nullopt;
}
{{- EndifFuchsia }}
{{- end }}
`
