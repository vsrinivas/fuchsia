// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const ReplyCFlavor = `
{{- define "ReplyCFlavorMethodSignature" -}}
Reply({{ template "Params" .Response }})
{{- end }}

{{- define "ReplyWithStatusCFlavorMethodSignature" -}}
ReplyWithStatus({{ template "Params" .Response }})
{{- end }}

{{- define "ReplyCFlavorMethodDefinition" }}
zx_status_t {{ .LLProps.ProtocolName }}::Interface::{{ .Name }}CompleterBase::{{ template "ReplyWithStatusCFlavorMethodSignature" . }} {
  {{- if .LLProps.LinearizeResponse }}
  {{/* tracking_ptr destructors will be called when _response goes out of scope */}}
  {{ .Name }}Response _response{
  {{- template "PassthroughMessageParams" .Response -}}
  };
  {{- else }}
  {{/* tracking_ptrs won't free allocated memory because destructors aren't called.
  This is ok because there are no tracking_ptrs, since LinearizeResponse is true when
  there are pointers in the object. */}}
  // Destructors can't be called because it will lead to handle double close
  // (here and in fidl::Encode).
  FIDL_ALIGNDECL uint8_t _response_buffer[sizeof({{ .Name }}Response)];
  auto& _response = *new (_response_buffer) {{ .Name }}Response{
  {{- template "PassthroughMessageParams" .Response -}}
  };
  {{- end }}

  auto _encoded = ::fidl::internal::LinearizedAndEncoded<{{ .Name }}Response>(&_response);
  auto& _encode_result = _encoded.result();
  if (_encode_result.status != ZX_OK) {
    CompleterBase::InternalError({::fidl::UnbindInfo::kEncodeError, _encode_result.status});
    return _encode_result.status;
  }
  return CompleterBase::SendReply(std::move(_encode_result.message));
}

void {{ .LLProps.ProtocolName }}::Interface::{{ .Name }}CompleterBase::{{ template "ReplyCFlavorMethodSignature" . }} {
  ReplyWithStatus({{ template "SyncClientMoveParams" .Response }});
}
{{- end }}

{{- define "ReplyCFlavorResultSuccessMethodSignature" -}}
ReplySuccess({{ template "Params" .Result.ValueMembers }})
{{- end }}

{{- define "ReplyWithStatusCFlavorResultSuccessMethodSignature" -}}
ReplySuccessWithStatus({{ template "Params" .Result.ValueMembers }})
{{- end }}

{{- define "ReplyCFlavorResultSuccessMethodDefinition" }}
zx_status_t {{ .LLProps.ProtocolName }}::Interface::{{ .Name }}CompleterBase::{{ template "ReplyWithStatusCFlavorResultSuccessMethodSignature" . }} {
  ::fidl::aligned<{{ .Result.ValueStructDecl }}> response;
  {{- range .Result.ValueMembers }}
  response.value.{{ .Name }} = std::move({{ .Name }});
  {{- end }}

  return ReplyWithStatus({{ .Result.ResultDecl }}::WithResponse(::fidl::unowned_ptr(&response)));
}

void {{ .LLProps.ProtocolName }}::Interface::{{ .Name }}CompleterBase::{{ template "ReplyCFlavorResultSuccessMethodSignature" . }} {
  ::fidl::aligned<{{ .Result.ValueStructDecl }}> response;
  {{- range .Result.ValueMembers }}
  response.value.{{ .Name }} = std::move({{ .Name }});
  {{- end }}

  ReplyWithStatus({{ .Result.ResultDecl }}::WithResponse(::fidl::unowned_ptr(&response)));
}
{{- end }}

{{- define "ReplyCFlavorResultErrorMethodSignature" -}}
ReplyError({{ .Result.ErrorDecl }} error)
{{- end }}

{{- define "ReplyWithStatusCFlavorResultErrorMethodSignature" -}}
ReplyErrorWithStatus({{ .Result.ErrorDecl }} error)
{{- end }}

{{- define "ReplyCFlavorResultErrorMethodDefinition" }}
zx_status_t {{ .LLProps.ProtocolName }}::Interface::{{ .Name }}CompleterBase::{{ template "ReplyWithStatusCFlavorResultErrorMethodSignature" . }} {
  return ReplyWithStatus({{ .Result.ResultDecl }}::WithErr(::fidl::unowned_ptr(&error)));
}

void {{ .LLProps.ProtocolName }}::Interface::{{ .Name }}CompleterBase::{{ template "ReplyCFlavorResultErrorMethodSignature" . }} {
  ReplyWithStatus({{ .Result.ResultDecl }}::WithErr(::fidl::unowned_ptr(&error)));
}
{{- end }}
`
