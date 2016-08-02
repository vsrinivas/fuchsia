// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:developer';
import 'dart:typed_data';

import 'package:args/args.dart';
import 'package:common/mojo_uri_loader.dart';
import 'package:common/uri_loader.dart';
import 'package:handler/inspector_json_server.dart';
import 'package:handler/graph/session_graph_store.dart';
import 'package:handler/graph/session_graph.dart';
import 'package:handler/handler.dart';
import 'package:handler/module_instance.dart';
import 'package:handler/module_runner.dart';
import 'package:handler/session.dart';
import 'package:handler/session_debug.dart';
import 'package:indexer_client/indexer_client.dart';
import 'package:modular_services/ledger2/ledger2.mojom.dart'
    show LedgerProxy, LedgerStub;
import 'package:modular/modular/handler.mojom.dart'
    show HandlerServiceStub, SessionGraphServiceStub;
import 'package:modular/modular/module.mojom.dart' show Module, ModuleProxy;
import 'package:modular_core/util/timeline_helper.dart';
import 'package:modular_core/uuid.dart';
import 'package:mojo_services/mojo/content_handler.mojom.dart';
import 'package:mojo_services/mojo/ui/view_provider.mojom.dart';
import 'package:mojo/application.dart';
import 'package:mojo/core.dart';
import 'package:mojo/mojo/application.mojom.dart' as application_mojom;
import 'package:mojo/mojo/url_response.mojom.dart';
import 'package:mojo/mojo/service_provider.mojom.dart';
import 'package:parser/manifest.dart' show Manifest;
import 'package:parser/parser.dart';
import 'package:parser/recipe.dart';

import 'composition_tree.dart';
import 'handler_service.dart';
import 'in_memory_ledger.dart';
import 'ledger_graph_store.dart';
import 'module_runner.dart';
import 'session_graph_service.dart';
import 'user_manager.dart';

/// A runner for a given instance of the handler. Runs a given session in its
/// own composition tree.
class HandlerRunner {
  final Application _application;
  Future<Handler> _handler;
  CompositionTree _compositionTree;
  Future<UserManager> _userManager;
  ModuleInstance _launcherInstance;
  final bool _watchSessions;
  final String _rootEmbodiment;
  final List<String> _sessionData;
  final InspectorJSONServer _inspector;

  HandlerRunner(
      this._application,
      final String url,
      final Future<List<Manifest>> manifestsFuture,
      final SessionGraphStore graphStore,
      this._watchSessions,
      this._rootEmbodiment,
      this._sessionData,
      final bool startInspector,
      {final bool hasLedger: false})
      : _inspector = startInspector ? new InspectorJSONServer() : null {
    _compositionTree = new CompositionTree(this, _inspector);
    _initializeHandler(url, manifestsFuture, graphStore, hasLedger: hasLedger);
  }

  ApplicationConnection connectToApplication(final String url) =>
      _application.connectToApplication(url);

  // TODO(alhaad): Having launcher specific functionality is not a good idea
  // and should go away.
  Future<Null> _provideHandlerServiceToLauncher(
      final ApplicationConnection connection) async {
    final Handler handler = await _handler;

    connection.provideService(HandlerService.serviceName,
        (final MojoMessagePipeEndpoint endpoint) {
      new HandlerServiceStub.fromEndpoint(
          endpoint, new HandlerServiceImpl(handler, _launcherInstance));
    });
  }

  // TODO(armansito): Expanding on alhaad's TODO above, we explicitly provide
  // services to suggestinator but in the future we will want a generic way to
  // determine who has access to these services.
  Future<Null> _provideServicesToSuggestinator(
      final ApplicationConnection connection) async {
    final Handler handler = await _handler;

    connection.provideService(HandlerService.serviceName,
        (final MojoMessagePipeEndpoint endpoint) {
      new HandlerServiceStub.fromEndpoint(
          endpoint, new HandlerServiceImpl(handler, null));
    });
    connection.provideService(SessionGraphService.serviceName,
        (final MojoMessagePipeEndpoint endpoint) {
      new SessionGraphServiceStub.fromEndpoint(
          endpoint, new SessionGraphServiceImpl(handler));
    });
  }

  /// Runs a recipe.
  Future<Session> handleRecipe(final Recipe recipe) {
    return traceAsync('$runtimeType handleRecipe', () async {
      // Display a banner that says "Modular" along with the URL for restoring
      // the session (be it newly minted or restored).
      print('-' * 80);
      print('  |\\/| _  _|   | _  _ ');
      print('  |  |(_)(_||_||(_||  ');
      print('');
      print('-' * 80);

      final Handler handler = await _handler;
      final Session session = await handler.createSession(recipe);
      if (_watchSessions) {
        new SessionWatcher(session);
      }
      session.start();

      return session;
      // The handler continues to execute in its callback. It's called back from
      // the module runner whenever it receives an update from the module app.
    });
  }

  /// Connects to a module implementation from the module URL and the required
  /// verb.
  ModuleProxy _createProxy(final ModuleInstance instance) {
    if (instance.manifest.url.toString().endsWith('launcher.flx')) {
      _launcherInstance = instance;
    }

    /// Connects to the module capable of composing UI through composer. We call
    /// these display modules. Any modules which specify a 'display' field in
    /// manifest is treated as display module.
    if (instance.isDisplayModule) {
      return _compositionTree.addModule(instance, _rootEmbodiment);
    }

    final ApplicationConnection connection =
        connectToApplication(instance.manifest.url.toString());
    ModuleProxy proxy = new ModuleProxy.unbound();
    connection.requestService(proxy, Module.serviceName);
    return proxy;
  }

  void _closeProxy(final ModuleInstance instance, final ModuleProxy proxy) {
    if (instance.isDisplayModule) {
      // Composition tree holds the map of instance to proxy.
      _compositionTree.removeModule(instance);
    } else {
      proxy.close();
    }
  }

  void _provideDefaultServices(final ApplicationConnection connection) {
    connection.provideService(ViewProvider.serviceName,
        (MojoMessagePipeEndpoint endpoint) {
      new ViewProviderStub.fromEndpoint(endpoint, _compositionTree);
    });
  }

  Future<Null> startSessions(final Iterable<String> sessionIds) {
    return traceAsync('$runtimeType startSessions', () async {
      final Handler handler = await _handler;
      final List<Session> sessions =
          await Future.wait(sessionIds.map((final String sessionId) {
        return handler.restoreSession(Uuid.fromBase64(sessionId));
      }));
      if (_sessionData != null) {
        sessions.forEach((final Session s) {
          SessionDataLoader loader = new SessionDataLoader(s);
          _sessionData.forEach((String data) => loader.load(data));
        });
      }
      if (_watchSessions) {
        sessions.forEach((final Session s) => new SessionWatcher(s));
      }
      sessions.forEach((final Session s) => s.start());
      // The handler continues to execute in its callback. It's called back
      // from the module runner whenever it receives an update from the
      // module app.
    });
  }

  Future<Null> startRootSession() async {
    final UserManager userManager = await _userManager;
    final Uuid userRootSessionId =
        await userManager.getOrCreateUserRootSessionId();
    return startSessions([userRootSessionId.toBase64()]);
  }

  Future<Null> startModule(final String moduleUrl) async {
    final Uri uri = Uri.parse(moduleUrl);
    final Handler handler = await _handler;

    // Find the manifest for the module.
    final Manifest manifest = handler.manifests
        .firstWhere((Manifest m) => (m.url == uri), orElse: () => null);

    if (manifest == null) {
      print("Couldn't find $moduleUrl in the manifest index.");
      print("The index includes:");
      handler.manifests.forEach((Manifest m) => print(" ${m.url}"));
      assert(false);
    }

    // Create a recipe containing just that module manifest.
    final Recipe recipe = new Recipe([new Step.fromManifest(manifest)],
        title: "Run module $moduleUrl", use: manifest.use);

    // Make a session for that recipe.
    final Session session = await handler.createSession(recipe);

    await startSessions([session.id.toBase64()]);

    final module = session.modules.firstWhere((m) => m.manifest == manifest);

    print('');
    print("-" * 80);
    print("RUN MODULE: ${module.manifest.url}");
    print('');
    if (module.instances.first == null) {
      print("Not running. Required input(s) unsatisfied:");
      for (var input in module.inputs) {
        if (!input.isComplete) {
          print(" - ${input.pathExpr}");
        }
      }
    }
    print("-" * 80);
    print('');
  }

  /// Asynchronously load the manifest index, then instantiate the Handler.
  void _initializeHandler(
      final String url,
      final Future<List<Manifest>> manifestsFuture,
      final SessionGraphStore graphStore,
      {final bool hasLedger: false}) {
    final Completer<Handler> handlerCompleter = new Completer<Handler>();
    final Completer<UserManager> userManagerCompleter =
        new Completer<UserManager>();
    _handler = handlerCompleter.future;
    _userManager = userManagerCompleter.future;

    traceAsync('$runtimeType _initializeHandler()', () async {
      // If graph from syncbase is not found with in 'metadataInitTimeout',
      // memGraph will be returned so that we don't get into conflicts with
      // edges that are expected to be only one like 'internal:metadata' edges.
      // This mean that any modifications on memgraph will not be persisted.
      final Future<SessionGraph> userManagerGraph =
          graphStore.findGraph(UserManager.sessionId);

      final ModuleRunnerFactory runnerFactory = () => new MojoModuleRunner(
          _createProxy, _closeProxy,
          composeModuleCallback: _compositionTree.updateModule);

      final Handler handler = new Handler(
          manifests: await manifestsFuture,
          graphStore: graphStore,
          runnerFactory: runnerFactory,
          inspector: _inspector);
      handlerCompleter.complete(handler);

      // Create user manager session to read all user data from ledger.
      final SessionGraph graph = await userManagerGraph;
      userManagerCompleter.complete(new UserManager(
          _application, handler, graph, graph.root, Uri.parse(url)));
    });
  }
}

/// The main handler application.
///
/// By default it exposes a ContentHandler for recipes.
///
/// For a recipe to be handled by this handler, it must have a #!mojo first line
/// that points to the URL of this application.
///
/// When accessed with specific query parameters, this application will start
/// the user root session, or restart a given session.
class HandlerProviderApplication extends Application {
  String _url;
  Future<List<Manifest>> _manifestsFuture;
  LedgerProxy _ledgerProxy;
  SessionGraphStore _graphStore;
  bool _hasLedger = false;
  List<String> _sessionData;
  // The arguments passed to this mojo application in initialize call.
  ArgResults _argResults;

  // TODO(armansito|alhaad): We hold on to the first HandlerRunner that gets
  // created and use that to provide services to other mojo apps but
  // there can potentially be many of these. We need to better define what
  // it means to have multiple HandlerRunners.
  HandlerRunner _handlerRunner;

  HandlerProviderApplication.fromHandle(final MojoHandle handle)
      : super.fromHandle(handle);

  HandlerRunner newHandlerRunner() {
    assert(_argResults != null);
    return new HandlerRunner(
        this,
        _url,
        _manifestsFuture,
        _graphStore,
        _argResults['watch'] as bool,
        // TODO(ksimbili) : Make this as real URL.
        _argResults['embodiment'] as String ?? 'root',
        _sessionData,
        _argResults['inspector'],
        hasLedger: _hasLedger);
  }

  @override
  Future<Null> close({final bool immediate: false}) async {
    if (_ledgerProxy != null && _ledgerProxy.ctrl.isBound) {
      _ledgerProxy.close();
    }
    await super.close(immediate: immediate);
  }

  // Mojo application initialization method.
  // [args] are the arguments for the handler mojo application.
  // [url] is the url of the handler mojo application.
  @override
  void initialize(List<String> args, final String url) {
    return Timeline.timeSync('$runtimeType initialize()', () {
      if (args.length > 0) {
        // Drop argv[0].
        args = args.sublist(1);
      }

      // Use the standard args package to parse arguments.
      final ArgParser argParser = new ArgParser();
      argParser.addFlag('inspector');
      argParser.addFlag('watch');
      argParser.addFlag('sync');
      argParser.addFlag('create-user-manager-graph');
      argParser.addOption('embodiment');
      argParser.addOption('session-data', allowMultiple: true);
      _argResults = argParser.parse(args);

      _sessionData = _argResults['session-data'];

      _url = url;

      _manifestsFuture = new IndexerClient(
              Uri.parse(url), new MojoUriLoader(connectToService),
              auxIndex:
                  _argResults.rest.isNotEmpty ? _argResults.rest.first : null)
          .initializeAndGetIndex();

      if (_argResults['sync']) {
        _hasLedger = true;
        _ledgerProxy = new LedgerProxy.unbound();
        connectToService(
            'https://tq.mojoapps.io/firebase_ledger.mojo', _ledgerProxy);
      } else {
        final MojoMessagePipe pipe = new MojoMessagePipe();
        new LedgerStub.fromEndpoint(pipe.endpoints[0], new InMemoryLedger());
        _ledgerProxy = new LedgerProxy.fromEndpoint(pipe.endpoints[1]);
      }

      _graphStore = new SessionGraphStore(new LedgerGraphStore(_ledgerProxy));

      if (!_hasLedger || _argResults['create-user-manager-graph']) {
        // This creates the metadata edge in the user manager graph. This
        // command line flag should be used only to create the user manager
        // graph in the cloud for the first time syncbase is started or the
        // syncbase data is empty.
        // WARNING: Calling with this flag multiple times, will end up with a
        // crash in #711.
        _graphStore.createGraph(UserManager.sessionId);
      }
    }, arguments: {'args': args.join(', '), 'url': url});
  }

  /// Mojo application connection callback. The behavior of this application
  /// depends on the query string of the connection URL:
  ///
  /// - If 'session' is given, the value must be a comma separated list of
  ///   base64 encoded session ids that will be restored.
  ///
  /// - If 'root_session' is given, the user root session will be
  ///   created/restored.
  ///
  /// - Otherwise, the content handler will be exposed.
  @override
  void acceptConnection(final String requestorUrl, final String resolvedUrl,
      final ApplicationConnection connection) {
    return traceSync('$runtimeType acceptConnection', () {
      final Uri resolvedUri = Uri.parse(resolvedUrl);
      final List<String> sessions = resolvedUri.queryParametersAll['session'];
      final String rootSession = resolvedUri.queryParameters['root_session'];
      final String module = resolvedUri.queryParameters['module'];

      if ((sessions != null && sessions.isNotEmpty) ||
          rootSession != null ||
          module != null) {
        final HandlerRunner handlerRunner = newHandlerRunner();
        if (_handlerRunner == null) _handlerRunner = handlerRunner;

        handlerRunner._provideDefaultServices(connection);

        if (sessions != null && sessions.isNotEmpty) {
          handlerRunner.startSessions(sessions);
          return;
        }

        if (rootSession != null) {
          handlerRunner.startRootSession();
          return;
        }

        if (module != null) {
          handlerRunner.startModule(module);
          return;
        }

        assert(false);
      }

      // TODO(armansito): We pick the first HandlerRunner to provide services to
      // launcher.flx and suggestinator. Not sure if this is the right thing; we
      // generally need to define what it means to have multiple HandlerRunners
      // and how their services should be exposed.
      if (_handlerRunner != null) {
        // Provide HandlerService to launcher.flx .
        if (requestorUrl.endsWith('launcher.flx')) {
          _handlerRunner._provideHandlerServiceToLauncher(connection);
        }

        // Provide HandlerService and GraphService to suggestinator.mojo
        if (requestorUrl.endsWith('suggestinator.mojo')) {
          _handlerRunner._provideServicesToSuggestinator(connection);
        }
      }

      connection.provideService(ContentHandler.serviceName,
          (final MojoMessagePipeEndpoint endpoint) {
        new _ContentHandler(endpoint, this);
      });
    }, arguments: {'requestorUrl': requestorUrl, 'resolvedUrl': resolvedUrl});
  }
}

/// Implementation of [application_mojom.Application] for a given recipe
/// launched through the content handler.
class _HandlerApplication implements application_mojom.Application {
  final Application _application;
  final HandlerRunner _handlerRunner;

  _HandlerApplication(
      this._application, this._handlerRunner, final UrlResponse response) {
    _start(response);
  }

  Future<Null> _start(final UrlResponse response) {
    return traceAsync("_HandlerApplication _start", () async {
      try {
        final ByteBuffer buffer =
            (await DataPipeDrainer.drainHandle(response.body)).buffer;
        final String contents =
            new String.fromCharCodes(new Uint8List.view(buffer));
        final _MojoImporter mojoImporter = new _MojoImporter(response.url,
            contents, new MojoUriLoader(_application.connectToService));
        final Recipe recipe = await parseRecipeFile(response.url, mojoImporter);
        await _handlerRunner.handleRecipe(recipe);
      } catch (error) {
        print('');
        print('Error loading recipe: ${response.url}');
        print('$error');
        print((error as Error).stackTrace);
        print('');
        rethrow;
      }
    });
  }

  @override
  void initialize(
      final Object shellProxy, final List<String> args, final String url) {}

  @override
  void acceptConnection(final String requestorUrl, final String resolvedUrl,
      final ServiceProviderInterfaceRequest services) {
    final ApplicationConnection connection =
        new ApplicationConnection(services, null);
    _handlerRunner._provideDefaultServices(connection);
  }

  @override
  void requestQuit() {}
}

/// A content handler service for recipes.
class _ContentHandler implements ContentHandler {
  HandlerProviderApplication _application;

  _ContentHandler(final MojoMessagePipeEndpoint endpoint, this._application) {
    new ContentHandlerStub.fromEndpoint(endpoint, this);
  }

  @override
  void startApplication(final Object stub, final UrlResponse response) {
    (stub as application_mojom.ApplicationStub).impl = new _HandlerApplication(
        _application, _application.newHandlerRunner(), response);
  }
}

/// An implementation of the parser Importer that loads files imported in a
/// recipe through the mojo network service from within the mojo content handler
/// for recipes.
class _MojoImporter {
  /// This Uri loaded is used to load imported files from an absolute URLs. The
  /// absolute URL is computed from the relative URL in the import: statement by
  /// the parser.
  final UriLoader _uriLoader;

  /// The top level file and its content has to be supplied by the importer too
  /// (because the parser treats all files the same at some point, and accesses
  /// all through the importer interface), but it doesn't have to be loaded
  /// through the loader above because it's been loaded by mojo already and
  /// supplied to the content handler by content.
  final String _initialName;
  final String _initialContent;

  _MojoImporter(this._initialName, this._initialContent, this._uriLoader);

  Future<String> call(final String name) {
    if (name == _initialName) {
      return new Future<String>.value(_initialContent);
    }

    return _uriLoader.getString(Uri.parse(name));
  }
}
