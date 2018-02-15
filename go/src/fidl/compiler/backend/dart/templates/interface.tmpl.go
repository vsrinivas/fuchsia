// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Interface = `
{{- define "Params" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end -}}{{ $param.Type.Decl }} {{ $param.Name }}
  {{- end -}}
{{ end }}

{{- define "RequestMethodSignature" -}}
  {{- if .HasResponse -}}
{{ .Name }}({{ template "Params" .Request }}{{ if .Request }}, {{ end }}void callback({{ template "Params" .Response }}))
  {{- else -}}
{{ .Name }}({{ template "Params" .Request }})
  {{- end -}}
{{ end -}}

{{- define "InterfaceDeclaration" -}}
abstract class {{ .Name }} {
{{- range .Methods }}
  {{- if .HasRequest }}
  void {{ template "RequestMethodSignature" . }};
  {{- end }}
{{- end }}
}

{{ range .Methods }}
  {{- if .HasRequest }}
const int {{ .OrdinalName }} = {{ .Ordinal }};
  {{- end }}
{{- end }}

const int _kMessageHeaderSize = 32;

class {{ .ProxyName }} extends $b.Proxy<{{ .Name }}>
    implements {{ .Name }} {

  {{ .ProxyName }}() : super(new $b.ProxyController<{{ .Name }}>()) {
    ctrl.onResponse = _handleResponse;
  }

  void _handleResponse($b.Message $message) {
    final Function $callback = ctrl.getCallback($message.txid);
    if ($callback == null) {
      $message.closeHandles();
      return;
    }
    final $b.Decoder $decoder = new $b.Decoder($message)
      ..claimMemory(_kMessageHeaderSize);
    const int $offset = _kMessageHeaderSize;
    switch ($message.ordinal) {
{{- range .Methods }}
  {{- if .HasRequest }}
    {{- if .HasResponse }}
      case {{ .OrdinalName }}:
        $decoder.claimMemory({{ .ResponseSize }});
        $callback(
      {{- range .Response }}
          {{ .Type.Decode .Offset }},
      {{- end }}
        );
        break;
    {{- end }}
  {{- end }}
{{- end }}
      default:
        ctrl.proxyError('Unexpected message ordinal: ${$message.ordinal}');
        ctrl.close();
        break;
    }
  }

{{- range .Methods }}
  {{- if .HasRequest }}
  @override
  void {{ template "RequestMethodSignature" . }} {
    if (!ctrl.isBound) {
      ctrl.proxyError("The proxy is closed.");
      return;
    }

    final $b.Encoder $encoder = new $b.Encoder({{ if .HasResponse }}ctrl.getNextTxid(){{ else }}0{{ end }}, {{ .OrdinalName }});
    {{- if .Request }}
    final int $offset = $encoder.alloc({{ .RequestSize }});
    {{- end }}
    {{- range .Request }}
    {{ .Type.Encode .Name .Offset }};
    {{- end }}
    {{- if .HasResponse }}
    Function $zonedCallback;
    if ((callback == null) || identical(Zone.current, Zone.ROOT)) {
      $zonedCallback = callback;
    } else {
      Zone $z = Zone.current;
      $zonedCallback = (({{ template "Params" .Response }}) {
        $z.bindCallback(() {
          callback(
      {{- range .Response -}}
        {{ .Name }},
      {{- end -}}
          );
        })();
      });
    }
    ctrl.sendMessageWithResponse($encoder.message, $zonedCallback);
    {{- else }}
    ctrl.sendMessage($encoder.message);
    {{- end }}
  }
  {{- end }}
{{- end }}
}

class {{ .BindingName }} extends $b.Binding<{{ .Name }}> {

{{ range .Methods }}
  {{- if .HasRequest }}
    {{- if .HasResponse }}
  Function _{{ .Name }}Responder($b.MessageSink $respond, int $txid) {
    return ({{ template "Params" .Response }}) {
      final $b.Encoder $encoder = new $b.Encoder($txid, {{ .OrdinalName }});
      {{- if .Response }}
      final int $offset = $encoder.alloc({{ .ResponseSize }});
      {{- end }}
      {{- range .Response }}
      {{ .Type.Encode .Name .Offset }};
      {{- end }}
      $respond($encoder.message);
    };
  }
    {{- end }}
  {{- end }}
{{- end }}

  @override
  void handleMessage($b.Message $message, $b.MessageSink $respond) {
    final $b.Decoder $decoder = new $b.Decoder($message)
      ..claimMemory(_kMessageHeaderSize);
    const int $offset = _kMessageHeaderSize;
    switch ($message.ordinal) {
{{- range .Methods }}
  {{- if .HasRequest }}
      case {{ .OrdinalName }}:
        $decoder.claimMemory({{ .RequestSize }});
        impl.{{ .Name }}(
    {{- range .Request }}
          {{ .Type.Decode .Offset }},
    {{- end }}
    {{- if .HasResponse }}
          _{{ .Name }}Responder($respond, $message.txid),
    {{- end }}
        );
        break;
  {{- end }}
{{- end }}
      default:
        throw new $b.FidlCodecError('Unexpected message name');
    }
  }
}

{{ end }}
`
