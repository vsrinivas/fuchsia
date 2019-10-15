import 'dart:async';

import 'package:fidl_fuchsia_auth/fidl_async.dart';
import 'package:fidl_fuchsia_sys/fidl_async.dart';
import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:fidl/fidl.dart';
import 'package:flutter/material.dart';
import 'package:fuchsia_inspect/inspect.dart' as inspect;
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_scenic_flutter/child_view.dart' show ChildView;
import 'package:fuchsia_scenic_flutter/child_view_connection.dart'
    show ChildViewConnection;
import 'package:fuchsia_services/services.dart';
import 'package:lib.widgets/model.dart';

const String testerUrl = '/system/test/google_oauth_demo';
const String userId = 'test_user';
const String userProfileId = 'test_user_profile';

const String googleAuthProviderType = 'Google';
const String googleAuthProviderUrl =
    'fuchsia-pkg://fuchsia.com/google_auth_provider#meta/google_auth_provider.cmx';
const List<String> accessTokenScopes = [
  'https://www.googleapis.com/auth/plus.login'
];

const String glifFlag = 'glif';
const String dedicatedEndpointFlag = 'dedicated-endpoint';

const String authStatusField = 'auth-status';
const String successAuthStatus = 'success';
const String failureAuthStatus = 'failure';
const String unknownAuthStatus = 'unknown';

const AppConfig googleAppConfig = AppConfig(
    clientId: null,
    clientSecret: null,
    authProviderType: googleAuthProviderType);

/// Thin app used for invoking token manager
void main(List<String> args) {
  setupLogger();

  final model = GoogleLoginTesterModel(inspect.Inspect().root);

  final app = ScopedModel(
    model: model,
    child: ServiceTesterWidget(),
  );
  // A hack to run the authorize flow asynchronously on startup.
  model.authorizeFlow();
  runApp(MaterialApp(
      color: Colors.white,
      home: Material(type: MaterialType.transparency, child: app)));
}

class GoogleLoginTesterModel extends Model {
  final TokenManagerFactoryProxy tokenManagerFactory =
      TokenManagerFactoryProxy();
  final TokenManagerProxy tokenManager = TokenManagerProxy();
  final ComponentControllerProxy controller = ComponentControllerProxy();

  // Inspect node used to report if the authentication flow has succeeded.
  final inspect.Node _inspectNode;

  ChildViewConnection childViewConnection;
  String userProfileId;

  GoogleLoginTesterModel(this._inspectNode) {
    _inspectNode.stringProperty(authStatusField).setValue(unknownAuthStatus);
  }

  Future<void> authorize() async {
    final response =
        await tokenManager.authorize(googleAppConfig, null, [], '', '');
    final status = response.status;
    if (status != Status.ok) {
      throw ServiceTesterException('Token manager authorize failed');
    }

    final info = response.userProfileInfo;
    userProfileId = info.id;
  }

  List<AuthProviderConfig> _buildAuthProviderConfigs() {
    return [
      AuthProviderConfig(
          authProviderType: googleAuthProviderType,
          url: googleAuthProviderUrl,
          params: <String>[])
    ];
  }

  Future<void> connectToTokenManager() async {
    final incoming = Incoming();

    final LaunchInfo launchInfo = LaunchInfo(
        url:
            'fuchsia-pkg://fuchsia.com/token_manager_factory#meta/token_manager_factory.cmx',
        directoryRequest: incoming.request().passChannel());

    // launch token manager factory using the launcher service.
    final launcherProxy = LauncherProxy();
    StartupContext.fromStartupInfo().incoming.connectToService(launcherProxy);
    await launcherProxy.createComponent(launchInfo, controller.ctrl.request());
    launcherProxy.ctrl.close();
    incoming.connectToService(tokenManagerFactory);

    await tokenManagerFactory.getTokenManager(
        userId,
        testerUrl,
        _buildAuthProviderConfigs(),
        AuthenticationContextProviderBinding()
            .wrap(_AuthenticationContextProviderImpl(this)),
        tokenManager.ctrl.request());
  }

  Future<void> getAccessToken() async {
    final response = await tokenManager.getAccessToken(
        googleAppConfig, userProfileId, accessTokenScopes);
    final status = response.status;
    if (status != Status.ok) {
      throw ServiceTesterException('Token manager getAccessToken failed');
    }
  }

  Future<void> authorizeFlow() async {
    try {
      await connectToTokenManager();
      await authorize();
      await getAccessToken();
      _inspectNode.stringProperty(authStatusField).setValue(successAuthStatus);
    } on ServiceTesterException {
      _inspectNode.stringProperty(authStatusField).setValue(failureAuthStatus);
    }
    notifyListeners();
  }
}

class _AuthenticationContextProviderImpl extends AuthenticationContextProvider {
  final _AuthenticationUiContextImpl _contextImpl;

  _AuthenticationContextProviderImpl(GoogleLoginTesterModel model)
      : _contextImpl = _AuthenticationUiContextImpl(model);

  @override
  Future<void> getAuthenticationUiContext(
      InterfaceRequest<AuthenticationUiContext> request) async {
    AuthenticationUiContextBinding().bind(_contextImpl, request);
  }
}

class _AuthenticationUiContextImpl extends AuthenticationUiContext {
  final GoogleLoginTesterModel _model;

  _AuthenticationUiContextImpl(this._model);

  @override
  Future<void> startOverlay(ViewHolderToken viewHolderToken) async {
    var viewConnection = ChildViewConnection(
      viewHolderToken,
      onAvailable: (ChildViewConnection connection) {
        connection.requestFocus();
        _model.notifyListeners();
      },
      onUnavailable: (ChildViewConnection connection) {
        stopOverlay();
      },
    );
    _model
      ..childViewConnection = viewConnection
      ..notifyListeners();
  }

  @override
  Future<Null> stopOverlay() async {
    _model
      ..childViewConnection = null
      ..notifyListeners();
  }
}

class ServiceTesterWidget extends StatelessWidget {
  @override
  Widget build(BuildContext context) =>
      ScopedModelDescendant<GoogleLoginTesterModel>(
          builder: _buildServiceTester);

  Widget _buildServiceTester(
      BuildContext context, Widget child, GoogleLoginTesterModel model) {
    return Container(
        child: model.childViewConnection != null
            ? ChildView(connection: model.childViewConnection)
            : Placeholder());
  }
}

class ServiceTesterException implements Exception {
  final String message;

  ServiceTesterException(this.message);

  @override
  String toString() {
    return 'ServiceTesterException: $message';
  }
}
