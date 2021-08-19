// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const protocolTmpl = `
{{- define "Params" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end -}}{{ $param.Type.Decl }} {{ $param.Name }}
  {{- end -}}
{{ end }}

{{/* Generate a parameter list (eg "int foo, String baz") with AsyncDecl types */}}
{{- define "AsyncParams" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end -}}{{ $param.Type.Decl }} {{ $param.Name }}
  {{- end -}}
{{ end }}

{{- define "AsyncReturn" -}}
{{- if .HasResponse -}}
$async.Future<{{ .AsyncResponseType }}>
{{- else -}}
$async.Future<void>
{{- end -}}
{{- end -}}

{{- define "ForwardParams" -}}
{{ range $index, $param := . }}{{ if $index }}, {{ end }}{{ $param.Name }}{{ end }}
{{- end -}}

{{/*
  Decode a method response message.
  The current object is the method (ir.Method).
  The Dart local variables are:
    List<$fidl.MemberType> $types - the table for the response.
    $fidl.Message $message - the message being decoded.
  This template expands to an expression so it can be assigned or passed as an argument.
*/}}
{{- define "DecodeResponse" -}}
  {{- if .Response.HasError }}
    $fidl.decodeMessage($message, {{ .TypeSymbol }}.responseInlineSize($message.wireFormat), $types[0])
  {{- else }}
    {{- if .AsyncResponseClass -}}
      $fidl.decodeMessageWithCallback<{{ .AsyncResponseClass }}>(
				$message,
				{{ .TypeSymbol }}.responseInlineSize($message.wireFormat),
				($fidl.Decoder decoder) {
					return {{ .AsyncResponseClass }}(
						{{- range $index, $response := .Response.WireParameters }}
							$types[{{ $index }}].decode(decoder, $fidl.kMessageHeaderSize, 1),
						{{- end -}}
					);
				}
      )
    {{- else -}}
      {{- if .Response.WireParameters -}}
        $fidl.decodeMessage(
          $message, {{ .TypeSymbol }}.responseInlineSize($message.wireFormat), $types[0])
      {{- else -}}
        null
      {{- end -}}
    {{- end -}}
  {{- end -}}
{{ end -}}


{{/*
  Encode a method response message.
  The current object is the method (ir.Method).
  The Dart local variables are:
    List<$fidl.MemberType> $types - the table for the response.
    $fidl.Encoder $encoder - the encoder for the message.
    $response - the Dart response type.
  This template expands to a statement.
*/}}
{{- define "EncodeResponse" -}}
  {{- if (and .AsyncResponseClass (not .Response.HasError)) -}}
		$fidl.encodeMessageWithCallback(
			$encoder,
			{{ .TypeSymbol }}.responseInlineSize($encoder.wireFormat),
			() {
				{{- range $index, $response := .Response.WireParameters }}
					$types[{{ $index }}].encode($encoder, $response.{{ .Name }}, $fidl.kMessageHeaderSize, 1);
				{{- end -}}
			}
		);
  {{- else -}}
    {{- if .Response.WireParameters -}}
			$fidl.encodeMessage(
        $encoder,
        {{ .TypeSymbol }}.responseInlineSize($encoder.wireFormat),
        $types[0],
        $response);
    {{- end -}}
  {{- end -}}
{{ end -}}

{{- define "ProtocolAsyncDeclaration" -}}

{{ range .Methods }}
// {{ .Name }}: {{ if .HasRequest }}({{ template "AsyncParams" .Request }}){{ end -}}
                {{- if .HasResponse }} -> ({{ template "AsyncParams" .Response.MethodParameters }}){{ end }}
const int {{ .OrdinalName }} = {{ .Ordinal | printf "%#x" }};
const $fidl.MethodType {{ .TypeSymbol }} = {{ .TypeExpr }};
{{- end }}

{{- range .Methods }}
  {{- if .AsyncResponseClass }}
class {{ .AsyncResponseClass }} {
    {{- range .Response.MethodParameters }}
  final {{ .Type.Decl }} {{ .Name }};
    {{- end }}
  {{ .AsyncResponseClass }}(
    {{- range .Response.MethodParameters }}
      this.{{ .Name }},
    {{- end -}}
    );
}
  {{- end }}
{{- end }}


{{- range .Doc }}
///{{ . -}}
{{- end }}
abstract class {{ .Name }} 
{{- if .ServiceName }}
      extends $fidl.Service
{{- end }}
      {
  {{- if .ServiceName }}
    static const String $serviceName = {{ .ServiceName }};
    @override
  {{- end }}
  $fidl.ServiceData? get $serviceData => {{ .ServiceData }}();

{{- range .Methods }}
  {{- if .HasRequest }}
    {{- range .Doc }}
    ///{{ . -}}
    {{- end }}
    {{ template "AsyncReturn" . }} {{ .Name }}({{ template "AsyncParams" .Request }})
    {{- if .Transitional }}
      { return $async.Future.error(UnimplementedError(), StackTrace.current); }
    {{- else }}
      ;
    {{- end }}
  {{- else }}
    {{- range .Doc }}
    ///{{ . -}}
    {{- end }}
    $async.Stream<{{ .AsyncResponseType}}>? get {{ .Name }}
    {{- if .Transitional }}
      { return $async.Stream.empty(); }
    {{- else }}
      ;
    {{- end }}
  {{- end }}
{{- end }}
}

// TODO: Remove ServiceData for non-service
class {{ .ServiceData }} implements $fidl.ServiceData<{{ .Name }}> {

  const {{ .ServiceData }}();

  @override
  String getName() {
    {{- if .ServiceName }}
    return {{ .Name }}.$serviceName;
    {{- else }}
    return "";
    {{- end }}
  }

  @override
  $fidl.AsyncBinding getBinding() {
    return {{ .BindingName }}();
  }
}

{{- range .Doc }}
///{{ . -}}
{{- end }}
class {{ .ProxyName }} extends $fidl.AsyncProxy<{{ .Name }}>
    implements {{ .Name }} {
  {{ .ProxyName }}() : super($fidl.AsyncProxyController<{{ .Name }}>(
      {{- if .ServiceName }}
        $serviceName: {{ .ServiceName }},
      {{- end }}
      $interfaceName: r'{{ .Name }}')
    ) {
    ctrl.onResponse = _handleResponse;

    {{- if .HasEvents }}
      ctrl.whenClosed.then((_) {
        {{- range .Methods }}
          {{- if not .HasRequest }}
            {{- if .HasResponse }}
              _{{ .Name }}EventStreamController.close();
            {{- end }}
          {{- end }}
        {{- end }}
      }, onError: (_) { });
    {{- end }}
  }

  {{- if .ServiceName }}
    @override
    $fidl.ServiceData get $serviceData => {{ .ServiceData }}();
  {{- else }}
    @override
    Null get $serviceData => null;
  {{- end }}

  void _handleEvent($fidl.IncomingMessage $message) {
    switch ($message.ordinal) {
{{- range .Methods }}
{{- if not .HasRequest }}
  {{- if .HasResponse }}
      case {{ .OrdinalName }}:
        final String _name = {{ .TypeSymbol }}.name;
        $fidl.performCtrlWithExceptionHandling(_name, ctrl, () {
          final List<$fidl.MemberType> $types = {{ .TypeSymbol }}.response!;
          _{{ .Name }}EventStreamController.add(
            {{- template "DecodeResponse" . -}}
          );
        }, 'event');
        break;
  {{- end }}
{{- end }}
{{- end }}
      default:
        $fidl.handleCtrlError(ctrl, 'Unexpected message ordinal: ${$message.ordinal}');
        break;
    }
  }

  void _handleResponse($fidl.IncomingMessage $message) {
    final int $txid = $message.txid;
    if ($txid == 0) {
      _handleEvent($message);
      return;
    }
    final $async.Completer? $completer = ctrl.getCompleter($txid);
    if ($completer == null) {
      $message.closeHandles();
      return;
    }
    switch ($message.ordinal) {
{{- range .Methods }}
  {{- if .HasRequest }}
    {{- if .HasResponse }}
      case {{ .OrdinalName }}:
        final String _name = {{ .TypeSymbol }}.name;
        $fidl.performCtrlWithExceptionHandling(_name, ctrl, () {
          final List<$fidl.MemberType> $types = {{ .TypeSymbol }}.response!;
          // ignore: prefer_const_declarations
          final $response = {{- template "DecodeResponse" . -}};
          {{ if .Response.HasError }}
            if ($response.$tag == {{ .Response.ResultType.TagName }}.response) {
              {{ if .AsyncResponseClass }}
                $completer.complete(
                  {{ .AsyncResponseClass }}(
                  {{ range $param := .Response.MethodParameters }}
                    $response.response.{{ $param.Name }},
                  {{ end }}
                  ));
              {{ else }}
                {{ if (eq .AsyncResponseType "void") }}
                  $completer.complete(null);
                {{ else }}
                  $completer.complete($response.response.{{ (index .Response.MethodParameters 0).Name }});
                {{ end }}
              {{ end }}
            } else {
              $completer.completeError($fidl.MethodException($response.err));
            }
          {{ else }}
            $completer.complete($response);
          {{ end }}
        }, 'method response');
        break;
    {{- end }}
  {{- end }}
{{- end }}
      default:
        $fidl.handleCtrlError(ctrl,'Unexpected message ordinal: ${$message.ordinal}');
        break;
    }
  }

{{- range .Methods }}
  {{- if .HasRequest }}
    {{- range .Doc }}
    ///{{ . -}}
    {{- end }}
    @override
    {{ template "AsyncReturn" . }} {{ .Name }}({{ template "AsyncParams" .Request }}) async {
      if (!ctrl.isBound) {
        return $async.Future.error($fidl.FidlStateException('Proxy<${ctrl.$interfaceName}> is closed.'), StackTrace.current);
      }

      final $fidl.Encoder $encoder = $fidl.Encoder($fidl.kWireFormatDefault);
      $encoder.encodeMessageHeader({{ .OrdinalName }}, 0);
      {{- if .Request }}
        final List<$fidl.MemberType> $types = {{ .TypeSymbol }}.request!;
				$fidl.encodeMessageWithCallback(
					$encoder,
					{{ .TypeSymbol }}.requestInlineSize($encoder.wireFormat),
					() {
						{{- range $index, $request := .Request }}
							$types[{{ $index }}].encode($encoder, {{ .Name }}, $fidl.kMessageHeaderSize, 1);
						{{- end -}}
					}
				);
      {{- end }}

      {{- if .HasResponse }}
        final $completer = $async.Completer<{{ .AsyncResponseType }}>();
        ctrl.sendMessageWithResponse($encoder.message, $completer);
        return $completer.future;
      {{- else }}
        return $async.Future.sync(() {
          ctrl.sendMessage($encoder.message);
        });
      {{- end }}
    }
  {{ else }}
    final _{{ .Name }}EventStreamController = $async.StreamController<{{ .AsyncResponseType }}>.broadcast();
    {{- range .Doc }}
    ///{{ . -}}
    {{- end }}
    @override
    $async.Stream<{{ .AsyncResponseType }}> get {{ .Name }} => _{{ .Name }}EventStreamController.stream;
  {{ end }}
{{- end }}
}

class {{ .BindingName }} extends $fidl.AsyncBinding<{{ .Name }}> {
  {{ .BindingName }}() : super(r"{{ .Name }}")
  {{- if .HasEvents }} {
    final List<$async.StreamSubscription<dynamic>> $subscriptions = [];
    void $unsubscribe() {
      for (final $sub in $subscriptions) {
        $sub.cancel();
      }
      $subscriptions.clear();
    }
    whenBound.then((_) {
      final impl = this.impl;
      if (impl != null) {
        {{- range .Methods }}
          {{- if not .HasRequest }}
            final _{{ .Name }}_stream = impl.{{ .Name }};
            if (_{{ .Name }}_stream != null) {
              $subscriptions.add(_{{ .Name }}_stream.listen(($response) {
                final $fidl.Encoder $encoder = $fidl.Encoder($fidl.kWireFormatDefault);
                $encoder.encodeMessageHeader({{ .OrdinalName }}, 0);
                final List<$fidl.MemberType> $types = {{ .TypeSymbol }}.response!;
                {{ template "EncodeResponse" . }}
                sendMessage($encoder.message);
              }));
            }
          {{- end }}
        {{- end }}
      }
    });
    whenClosed.then((_) => $unsubscribe());
  }
  {{- else -}}
    ;
  {{- end }}

  @override
  void handleMessage($fidl.IncomingMessage $message, $fidl.OutgoingMessageSink $respond) {
    switch ($message.ordinal) {
    {{- range .Methods }}
      {{- if .HasRequest }}
          case {{ .OrdinalName }}:
            final String _name = {{ .TypeSymbol }}.name;
            $fidl.performWithExceptionHandling(_name, () {
              final List<$fidl.MemberType> $types = {{ .TypeSymbol }}.request!;
							// ignore: prefer_const_declarations
							final _impl = impl!;
							final {{ template "AsyncReturn" . }} $future = $fidl.decodeMessageWithCallback<{{ template "AsyncReturn" . }}>(
								$message,
								{{ .TypeSymbol }}.requestInlineSize($message.wireFormat),
								($fidl.Decoder decoder) {
									return _impl.{{ .Name }}(
										{{- range $index, $request := .Request }}
											$types[{{ $index }}].decode(decoder, $fidl.kMessageHeaderSize, 1),
										{{- end }}
									);
								}
							);

              {{- if .HasResponse }}
                $future
                {{ if .Response.HasError }}
                .then(($responseValue) {
                  {{ if .AsyncResponseClass }}
                    return {{ .Response.ResultType.Name }}.withResponse(
                      {{ .Response.ValueType.Decl }}(
                      {{ range $param := .Response.MethodParameters }}
                        {{ $param.Name }}: $responseValue.{{ $param.Name }},
                      {{ end }}
                      ));
                  {{ else }}
                    return {{ .Response.ResultType.Name }}.withResponse(
                      {{ .Response.ValueType.Decl }}(
                        {{ if (ne .AsyncResponseType "void") }}
                          {{ (index .Response.MethodParameters 0).Name }}: $responseValue
                        {{ end }}
                        ));
                  {{ end }}
                }, onError: ($error) {
                  if ($error is $fidl.MethodException) {
                    return {{ .Response.ResultType.Name }}.withErr($error.value);
                  } else {
                    return Future.error($error, StackTrace.current);
                  }
                })
                {{ end }}
                .then(($response) {
                  final $fidl.Encoder $encoder = $fidl.Encoder($fidl.kWireFormatDefault);
                  $encoder.encodeMessageHeader({{ .OrdinalName }}, $message.txid);
                  {{- if .Response.WireParameters }}
                    final List<$fidl.MemberType> $types = {{ .TypeSymbol }}.response!;
                    {{ template "EncodeResponse" . -}}
                  {{- end }}
                  $respond($encoder.message);
                }, onError: (_e) {
                  $fidl.handleException(_name, _e, close);
                });
              {{- end }}
          }, close);
            break;
      {{- end }}
    {{- end }}
      default:
        throw $fidl.FidlError(r'Unexpected message name for {{ .BindingName }}');
    }
  }
}

{{ end }}



{{- define "ProtocolTestDeclaration" -}}

class {{ .Name }}$TestBase extends {{ .Name }} {
  {{- $protocolName := .Name }}
  {{- range .Methods }}
  {{- $exceptionMessage := printf "r'%s not implemented on %s test base. Please implement.'"  .Name  $protocolName }}
  @override
  {{- if .HasRequest }}
  {{ template "AsyncReturn" . }} {{ .Name }}({{ template "AsyncParams" .Request }}) {
    return $async.Future.error(UnimplementedError({{ $exceptionMessage }}), StackTrace.current);
  }
  {{- else }}
  $async.Stream<{{ .AsyncResponseType }}> get {{ .Name }} {
    return $async.Stream.fromFuture($async.Future.error(UnimplementedError({{ $exceptionMessage }}), StackTrace.current));
  }
  {{- end }}
{{- end }}

}

{{ end }}

`
