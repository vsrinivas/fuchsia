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

{{- define "ResponseMethodSignature" -}}
{{ .Name }}({{ template "Params" .Response }})
{{ end -}}

{{- define "InterfaceDeclaration" -}}
abstract class {{ .Name }} {
  static const String $serviceName = {{ .ServiceName }};

{{- range .Methods }}
  {{- if .HasRequest }}
  void {{ template "RequestMethodSignature" . }};
  {{- end }}
{{- end }}
}

{{ range .Methods }}
// {{ .Name }}: {{ if .HasRequest }}({{ template "Params" .Request }}){{ end }}{{ if .HasResponse }} -> ({{ template "Params" .Response }}){{ end }}
const int {{ .OrdinalName }} = {{ .Ordinal }};
const $fidl.MethodType {{ .TypeSymbol }} = {{ .TypeExpr }};
{{- end }}

{{ range .Methods }}
  {{- if not .HasRequest }}
    {{- if .HasResponse }}
typedef void {{ .CallbackType }}({{ template "Params" .Response }});
    {{- end }}
  {{- end }}
{{- end }}

class {{ .ProxyName }} extends $fidl.Proxy<{{ .Name }}>
    implements {{ .Name }} {

  {{ .ProxyName }}() : super(new $fidl.ProxyController<{{ .Name }}>($serviceName: {{ .ServiceName }})) {
    ctrl.onResponse = _handleResponse;
  }

  void _handleEvent($fidl.Message $message) {
    final $fidl.Decoder $decoder = new $fidl.Decoder($message);
    switch ($message.ordinal) {
{{- range .Methods }}
{{- if not .HasRequest }}
  {{- if .HasResponse }}
      case {{ .OrdinalName }}:
        final Function $callback = {{ .Name }};
        if ($callback == null) {
          $message.closeHandles();
          return;
        }
        final List<$fidl.MemberType> $types = {{ .TypeSymbol }}.response;
        $decoder.claimMemory({{ .ResponseSize }});
        $callback(
    {{- range $index, $response := .Response }}
          $types[{{ $index }}].decode($decoder, 0),
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

  void _handleResponse($fidl.Message $message) {
    final int $txid = $message.txid;
    if ($txid == 0) {
      _handleEvent($message);
      return;
    }
    final Function $callback = ctrl.getCallback($txid);
    if ($callback == null) {
      $message.closeHandles();
      return;
    }
    final $fidl.Decoder $decoder = new $fidl.Decoder($message);
    switch ($message.ordinal) {
{{- range .Methods }}
  {{- if .HasRequest }}
    {{- if .HasResponse }}
      case {{ .OrdinalName }}:
        final List<$fidl.MemberType> $types = {{ .TypeSymbol }}.response;
        $decoder.claimMemory({{ .ResponseSize }});
        $callback(
      {{- range $index, $response := .Response }}
          $types[{{ $index }}].decode($decoder, 0),
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
      ctrl.proxyError('The proxy is closed.');
      return;
    }

    final $fidl.Encoder $encoder = new $fidl.Encoder({{ .OrdinalName }});
    {{- if .Request }}
    $encoder.alloc({{ .RequestSize }} - $fidl.kMessageHeaderSize);
    final List<$fidl.MemberType> $types = {{ .TypeSymbol }}.request;
    {{- end }}
    {{- range $index, $request := .Request }}
    $types[{{ $index }}].encode($encoder, {{ .Name }}, 0);
    {{- end }}
    {{- if .HasResponse }}
    Function $zonedCallback;
    if ((callback == null) || identical(Zone.current, Zone.root)) {
      $zonedCallback = callback;
    } else {
      Zone $z = Zone.current;
      {{- if .Response }}
      $zonedCallback = (({{ template "Params" .Response }}) {
        $z.bindCallback(() {
          callback(
        {{- range .Response -}}
            {{ .Name }},
        {{- end -}}
          );
        })();
      });
      {{- else }}
      $zonedCallback = $z.bindCallback(callback);
      {{- end }}
    }
    ctrl.sendMessageWithResponse($encoder.message, $zonedCallback);
    {{- else }}
    ctrl.sendMessage($encoder.message);
    {{- end }}
  }
  {{- else if .HasResponse }}
  {{ .CallbackType }} {{ .Name }};
  {{- end }}
{{- end }}
}

{{- if .HasEvents }}

class {{ .EventsName }} {
  $fidl.Binding<{{ .Name }}> _binding;

{{- range .Methods }}
  {{- if not .HasRequest }}
    {{- if .HasResponse }}
  void {{ template "ResponseMethodSignature" . }} {
    final $fidl.Encoder $encoder = new $fidl.Encoder({{ .OrdinalName }});
      {{- if .Response }}
    $encoder.alloc({{ .ResponseSize }} - $fidl.kMessageHeaderSize);
    final List<$fidl.MemberType> $types = {{ .TypeSymbol }}.response;
      {{- end }}
      {{- range $index, $response := .Response }}
    $types[{{ $index }}].encode($encoder, {{ .Name }}, 0);
      {{- end }}
    _binding.sendMessage($encoder.message);
  }
    {{- end }}
  {{- end }}
{{- end }}
}

{{- end }}

class {{ .BindingName }} extends $fidl.Binding<{{ .Name }}> {
{{- if .HasEvents }}
  {{ .BindingName }}() {
    events._binding = this;
  }

  final {{ .EventsName }} events = new {{ .EventsName }}();
{{- end }}

{{ range .Methods }}
  {{- if .HasRequest }}
    {{- if .HasResponse }}
  Function _{{ .Name }}Responder($fidl.MessageSink $respond, int $txid) {
    return ({{ template "Params" .Response }}) {
      final $fidl.Encoder $encoder = new $fidl.Encoder({{ .OrdinalName }});
      {{- if .Response }}
      $encoder.alloc({{ .ResponseSize }} - $fidl.kMessageHeaderSize);
      final List<$fidl.MemberType> $types = {{ .TypeSymbol }}.response;
      {{- end }}
      {{- range $index, $response := .Response }}
      $types[{{ $index }}].encode($encoder, {{ .Name }}, 0);
      {{- end }}
      $fidl.Message $message = $encoder.message;
      $message.txid = $txid;
      $respond($message);
    };
  }
    {{- end }}
  {{- end }}
{{- end }}

  @override
  void handleMessage($fidl.Message $message, $fidl.MessageSink $respond) {
    final $fidl.Decoder $decoder = new $fidl.Decoder($message);
    switch ($message.ordinal) {
{{- range .Methods }}
  {{- if .HasRequest }}
      case {{ .OrdinalName }}:
        final List<$fidl.MemberType> $types = {{ .TypeSymbol }}.request;
        $decoder.claimMemory({{ .RequestSize }});
        impl.{{ .Name }}(
    {{- range $index, $request := .Request }}
          $types[{{ $index }}].decode($decoder, 0),
    {{- end }}
    {{- if .HasResponse }}
          _{{ .Name }}Responder($respond, $message.txid),
    {{- end }}
        );
        break;
  {{- end }}
{{- end }}
      default:
        throw new $fidl.FidlError('Unexpected message name');
    }
  }
}

{{ end }}

{{- define "AsyncReturn" -}}
{{- if .HasResponse -}}
Future<{{ .AsyncResponseType }}>
{{- else -}}
Future<Null>
{{- end -}}
{{- end -}}

{{- define "ForwardParams" -}}
{{ range $index, $param := . }}{{ if $index }}, {{ end }}{{ $param.Name }}{{ end }}
{{- end -}}

{{- define "ForwardParamsConvert" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end -}}
    {{- if $param.Convert -}}
      {{- $param.Convert -}}({{- $param.Name -}})
    {{- else -}}
      {{- $param.Name -}}
    {{- end -}}
  {{- end }}
{{- end -}}

{{- define "ForwardResponseParams" -}}
  {{- if .AsyncResponseClass -}}
    {{- range $index, $param := .Response }}{{ if $index }}, {{ end }}_response.{{ $param.Name }}{{ end -}}
  {{- else -}}
    {{- if .AsyncResponseType -}}
      _response
    {{- end -}}
  {{- end -}}
{{- end -}}

{{- define "ForwardResponseParamsConvert" -}}
  {{- if .AsyncResponseClass -}}
    {{- range $index, $param := .Response -}}
      {{- if $index }}, {{ end -}}
      {{- if $param.Convert -}}
        {{- $param.Convert }}(_response.{{ $param.Name -}})
      {{- else -}}
        _response.{{ $param.Name -}}
      {{- end -}}
    {{- end -}}
  {{- else -}}
    {{- if .AsyncResponseType -}}
      {{- with $param := index .Response 0 -}}
        {{- if $param.Convert -}}
          {{- $param.Convert }}(_response)
        {{- else -}}
          _response
        {{- end -}}
      {{- end -}}
    {{- end -}}
  {{- end -}}
{{- end -}}

{{- define "FuturizeParams" -}}
  {{- range $index, $param := . -}}
    {{- if $index }}, {{ end -}}
    {{- if $param.Type.SyncDecl -}}
      {{- $param.Type.SyncDecl -}}
    {{- else -}}
      {{- $param.Type.Decl -}}
    {{- end }} {{ $param.Name }}
  {{- end -}}
{{ end }}

{{- define "FuturizeRequestMethodSignature" -}}
  {{- if .HasResponse -}}
{{ .Name }}({{ template "FuturizeParams" .Request }}{{ if .Request }}, {{ end }}void callback({{ template "FuturizeParams" .Response }}))
  {{- else -}}
{{ .Name }}({{ template "FuturizeParams" .Request }})
  {{- end -}}
{{ end -}}

{{- define "InterfaceAsyncDeclaration" -}}
{{- range .Methods }}
  {{- if .AsyncResponseClass }}
class {{ .AsyncResponseClass }} {
    {{- range .Response }}
  final {{ .Type.Decl }} {{ .Name }};
    {{- end }}
  {{ .AsyncResponseClass }}(
    {{- range .Response }}
      this.{{ .Name }},
    {{- end -}}
    );
}
  {{- end }}
{{- end }}

abstract class {{ .Name }} {
  static const String $serviceName = {{ .ServiceName }};

{{- range .Methods }}
  {{- if .HasRequest }}
  {{ template "AsyncReturn" . }} {{ .Name }}({{ template "Params" .Request }});
  {{- else }}
  Stream<{{ .AsyncResponseType}}> get {{ .Name }};
  {{- end }}
{{- end }}
}

class {{ .Name }}Proxy implements {{ .Name }} {
  final $sync.{{ .Name }}Proxy _syncProxy;
  $fidl.ProxyControllerWrapper<{{ .Name }}, $sync.{{ .Name }}> _ctrl;
  final _completers = new Set<Completer<dynamic>>();
  {{ .Name }}Proxy() : _syncProxy = new $sync.{{ .Name }}Proxy()
  {
    _ctrl = new $fidl.ProxyControllerWrapper<{{ .Name }}, $sync.{{ .Name }}>(_syncProxy.ctrl);
    {{- range .Methods }}
      {{- if not .HasRequest }}
        _syncProxy.{{ .Name }} = ({{ template "Params" .Response }}) =>
          _{{ .Name }}EventStreamController.add(
            {{- if .AsyncResponseClass -}}
              new {{ .AsyncResponseClass }}({{ template "ForwardParams" .Response }})
            {{- else -}}
              {{- if .Response -}}
                {{- template "ForwardParams" .Response -}}
              {{- else -}}
                null
              {{- end -}}
            {{- end -}});
      {{- end }}
    {{- end }}
    _syncProxy.ctrl.error.then(_errorOccurred);
  }

  $fidl.ProxyControllerWrapper<{{ .Name }}, $sync.{{ .Name }}> get ctrl => _ctrl;

  void _errorOccurred($fidl.ProxyError err) {
    // Make a copy of the set of completers and then clear it.
    final errorCompleters = new List<Completer<dynamic>>.from(_completers);
    _completers.clear();
    // Dispatch the error to all of the completers.
    for (var c in errorCompleters) {
      c.completeError(err);
    }
    // Ask for the next error.
    _syncProxy.ctrl.error.then(_errorOccurred);
  }

{{- range .Methods }}
  {{- if .HasRequest }}
  @override
  {{ template "AsyncReturn" . }} {{ .Name }}({{ template "Params" .Request }}) {
    {{- if .HasResponse }}
    final _completer = new Completer<{{ .AsyncResponseType }}>();
    _completers.add(_completer);
    void _call() {
        _syncProxy.{{ .Name }}({{ template "ForwardParamsConvert" .Request -}}
        {{- if .Request }}, {{ end -}}
        ({{ template "FuturizeParams" .Response }}) {
          _completers.remove(_completer);
          _completer.complete(
        {{- if .AsyncResponseClass -}}
          new {{ .AsyncResponseClass }}({{ template "ForwardParamsConvert" .Response }})
        {{- else -}}
          {{- template "ForwardParamsConvert" .Response -}}
        {{- end -}}
        );
      });
    }
    try {
      if (_syncProxy.ctrl.isBound) {
        _call();
      } else {
        _syncProxy.ctrl.bound.then((_) =>_call());
      }
    } catch(err) {
      _completers.remove(_completer);
      _completer.completeError(err);
    }
    return _completer.future;
    {{- else }}
    final _completer = new Completer<Null>();
    _completers.add(_completer);
    if (_syncProxy.ctrl.isBound) {
      _syncProxy.{{ .Name }}({{ template "ForwardParamsConvert" .Request}});
      _completers.remove(_completer);
      _completer.complete();
    } else {
      _syncProxy.ctrl.bound.then((_) {
        _syncProxy.{{ .Name }}({{ template "ForwardParamsConvert" .Request}});
        _completers.remove(_completer);
        _completer.complete();
      }, onError: (dynamic error, StackTrace stackTrace) {
        _completers.remove(_completer);
        _completer.completeError(error, stackTrace);
      });
    }
    return _completer.future;
    {{- end }}
  }
  {{ else }}
  final _{{ .Name }}EventStreamController = new StreamController<{{ .AsyncResponseType }}>.broadcast();
  @override
  Stream<{{ .AsyncResponseType}}> get {{ .Name }} => _{{ .Name }}EventStreamController.stream;
  {{ end }}
{{- end }}
}

class _{{ .Name }}Futurize implements $sync.{{ .Name }} {
  final {{ .Name }} _inner;
  _{{ .Name }}Futurize(this._inner);

{{- range .Methods }}
  {{- if .HasRequest }}
  @override
  void {{ template "FuturizeRequestMethodSignature" . }} =>
    {{- if .HasResponse }}
    _inner.{{ .Name }}({{ template "ForwardParamsConvert" .Request }}).then(
  ({{ if .AsyncResponseType }}{{ .AsyncResponseType }} _response{{ end }}) =>
  callback(
      {{- if .Response -}}
        {{- template "ForwardResponseParamsConvert" . -}}
      {{- end -}}));
    {{- else }}
    _inner.{{ .Name }}({{ template "ForwardParamsConvert" .Request}});
    {{- end }}
  {{- else }}
  {{- end }}
{{- end }}
}

class {{ .Name }}Binding {
  final $sync.{{ .Name }}Binding _syncBinding;
  {{ .Name }}Binding() : _syncBinding = new $sync.{{ .Name }}Binding();

  _VoidCallback get onBind => _syncBinding.onBind;
  set onBind(_VoidCallback f) => _syncBinding.onBind = f;
  _VoidCallback get onUnbind => _syncBinding.onUnbind;
  set onUnbind(_VoidCallback f) => _syncBinding.onUnbind = f;
  _VoidCallback get onClose => _syncBinding.onClose;
  set onClose(_VoidCallback f) => _syncBinding.onClose = f;
  _VoidCallback get onConnectionError => _syncBinding.onConnectionError;
  set onConnectionError(_VoidCallback f) => _syncBinding.onConnectionError = f;

  $fidl.InterfaceHandle<{{ .Name }}> wrap({{ .Name }} impl) {
    final handle = new $fidl.InterfaceHandle<{{ .Name }}>(_syncBinding.wrap(new _{{ .Name }}Futurize(impl)).passChannel());
    _bind(impl);
    return handle;
  }

  void bind({{ .Name }} impl, $fidl.InterfaceRequest<{{ .Name }}> interfaceRequest) {
    _syncBinding.bind(new _{{ .Name }}Futurize(impl), new $fidl.InterfaceRequest<$sync.{{ .Name }}>(interfaceRequest.passChannel()));
    _bind(impl);
   }

  $fidl.InterfaceRequest<{{ .Name }}> unbind() {
    final req = new $fidl.InterfaceRequest<{{ .Name }}>(_syncBinding.unbind().passChannel());
    _unbind();
    return req;
  }

  bool get isBound => _syncBinding.isBound;

  {{- range .Methods }}
  {{- if not .HasRequest }}
  StreamSubscription<{{ .AsyncResponseType }}> _{{ .Name }}_subscription;
  {{- end }}
  {{- end }}

  void _bind({{ .Name }} impl) {
    {{- range .Methods }}
    {{- if not .HasRequest }}
    _{{ .Name }}_subscription = impl.{{ .Name }}.listen(
  ({{ if .AsyncResponseType }}{{ .AsyncResponseType }} _response{{ end }}) =>
  _syncBinding.events.{{ .Name }}({{ if .Response }}{{ template "ForwardResponseParams" . }}{{ end }}));
    {{- end }}
    {{- end }}
  }

  void _unbind() {
    {{- range .Methods }}
    {{- if not .HasRequest }}
    _{{ .Name }}_subscription.cancel();
    _{{ .Name }}_subscription = null;
    {{- end }}
    {{- end }}
  }

  void close() {
    _unbind();
    _syncBinding.close();
  }
}

{{ end }}
`
