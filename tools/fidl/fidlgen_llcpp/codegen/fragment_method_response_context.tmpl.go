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
  zx_status_t OnRawReply(fidl_incoming_msg_t* msg) override;
};
{{- EndifFuchsia }}
{{- end }}



{{- define "MethodResponseContextDefinition" }}
{{- EnsureNamespace "" }}
{{- IfdefFuchsia }}
{{ .WireResponseContext }}::{{ .WireResponseContext.Self }}()
    : ::fidl::internal::ResponseContext({{ .OrdinalName }}) {}

zx_status_t {{ .WireResponseContext.NoLeading }}::OnRawReply(fidl_incoming_msg_t* msg) {
  zx_status_t status = fidl_decode_msg({{ .WireResponse }}::Type, msg, nullptr);
  if (unlikely(status != ZX_OK)) {
    return status;
  }
  OnReply(reinterpret_cast<{{ .WireResponse }}*>(msg->bytes));
  return ZX_OK;
}
{{- EndifFuchsia }}
{{- end }}
`
