// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

// fragmentMethodResponseContextTmpl contains the definition for
// fidl::WireResponseContext<Method>.
const fragmentMethodResponseContextTmpl = `
{{- define "MethodResponseContextDeclaration" }}
{{- EnsureNamespace "" }}
{{- IfdefFuchsia }}
template<>
class {{ .WireResponseContext }} : public ::fidl::internal::ResponseContext {
 public:
  {{ .WireResponseContext.Self }}();

  virtual void OnReply({{ .WireResponse }}* message) = 0;

 private:
  void OnReply(uint8_t* reply) override;
};
{{- EndifFuchsia }}
{{- end }}



{{- define "MethodResponseContextDefinition" }}
{{- EnsureNamespace "" }}
{{- IfdefFuchsia }}
{{ .WireResponseContext }}::{{ .WireResponseContext.Self }}()
    : ::fidl::internal::ResponseContext({{ .WireResponse }}::Type, {{ .OrdinalName }}) {}

void {{ .WireResponseContext }}::OnReply(uint8_t* reply) {
  OnReply(reinterpret_cast<{{ .WireResponse }}*>(reply));
}
{{- EndifFuchsia }}
{{- end }}
`
