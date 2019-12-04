import 'package:fidl_fuchsia_web/fidl_async.dart' as web;
import 'package:fuchsia_logger/logger.dart';
import 'package:test/test.dart';
import 'package:mockito/mockito.dart';

// ignore_for_file: implementation_imports
import 'package:simple_browser/src/blocs/webpage_bloc.dart';
import 'package:simple_browser/src/models/webpage_action.dart';

void main() {
  setupLogger(name: 'webpage_bloc_test');

  WebPageBloc webPageBloc;
  MockNavigationControllerProxy mockController;

  setUp(() {
    mockController = MockNavigationControllerProxy();
    webPageBloc = WebPageBloc(
      controller: mockController,
      listener: MockNavigationEventListenerBinding(),
      popupListener: MockPopupFrameCreationListenerBinding(),
    );
  });

  /// Tests [onNavigationStateChange] method in [WebPageBloc].
  ///
  /// Checks if relavent members are updated according to the value change events on
  /// the [NavigationState].
  group('onNavigationStateChange', () {
    MockNavigationState mockNavigationState = MockNavigationState();

    test('''WebPageBloc.url should be updated
        when NavigationState.url changed.''', () {
      //  url should be an empty string by default.
      expect(webPageBloc.url, '');

      // When NavigationState.url is changed to 'https://www.flutter.dev'.
      String testUrl = 'https://www.flutter.dev';
      when(mockNavigationState.url).thenReturn(testUrl);
      webPageBloc.onNavigationStateChanged(mockNavigationState);
      expect(webPageBloc.url, testUrl);
    });

    test('''WebPageBloc.forwardState should be updated
        when NavigationState.canGoForward changed.''', () {
      // forwardState should be false by default.
      expect(webPageBloc.forwardState, false);

      // when NavigationState.canGoForward is changed to true.
      when(mockNavigationState.canGoForward).thenReturn(true);
      webPageBloc.onNavigationStateChanged(mockNavigationState);
      expect(webPageBloc.forwardState, true);
    });

    test('''WebPageBloc.backState should be updated
        when NavigationState.canGoBack changed.''', () {
      // backState should be false by default.
      expect(webPageBloc.backState, false);

      // when NavigationState.canGoBack is changed to true.
      when(mockNavigationState.canGoBack).thenReturn(true);
      webPageBloc.onNavigationStateChanged(mockNavigationState);
      expect(webPageBloc.backState, true);
    });

    test('''WebPageBloc.isLoadedState should be updated
        when NavigationState.isMainDocumentLoaded changed.''', () {
      // isLoadedState should be true by default.
      expect(webPageBloc.isLoadedState, true);

      // when NavigationState.isMainDocumentLoaded is changed to false.
      when(mockNavigationState.isMainDocumentLoaded).thenReturn(false);
      webPageBloc.onNavigationStateChanged(mockNavigationState);
      expect(webPageBloc.isLoadedState, false);
    });

    test('''WebPageBloc.pageTitle should be updated
        when NavigationState.title changed.''', () {
      // pageTitle has no default value.

      // when NavigationState.title is changed to 'test'.
      String testTitle = 'test';
      when(mockNavigationState.title).thenReturn(testTitle);
      webPageBloc.onNavigationStateChanged(mockNavigationState);
      expect(webPageBloc.pageTitle, testTitle);
    });

    test('''WebPageBloc.pageType should be updated
        when NavigationState.pageType changed.''', () {
      //  pageType should be PageType.empty by default.
      expect(webPageBloc.pageType, PageType.empty);

      // when NavigationState.pageType is changed to web.PageType.normal.
      web.PageType testPageType = web.PageType.normal;
      when(mockNavigationState.pageType).thenReturn(testPageType);
      webPageBloc.onNavigationStateChanged(mockNavigationState);
      expect(webPageBloc.pageType, PageType.normal);

      // when NavigationState.pageType is changed to web.PageType.error.
      testPageType = web.PageType.error;
      when(mockNavigationState.pageType).thenReturn(testPageType);
      webPageBloc.onNavigationStateChanged(mockNavigationState);
      expect(webPageBloc.pageType, PageType.error);
    });
  });

  /// Tests [WebPageBloc]'s [StreamController] and its callback [_handleAction].
  ///
  /// Verify if relavent methods of [NavigationController] are called,
  /// and irrelavent ones are not called through the callback when an [WebPageAction]
  /// is added to the bloc.
  group('Handling actions', () {
    test('''
      Should call NavigationControllerProxy.loadUrl()
      when NavigateToAction is added to the webPageBloc.
      ''', () async {
      String testUrl = 'https://www.google.com';

      webPageBloc.request.add(NavigateToAction(url: testUrl));

      await untilCalled(mockController.loadUrl(any, any));
      verify(mockController.loadUrl(testUrl, any)).called(1);
      verifyNever(mockController.goBack());
      verifyNever(mockController.goForward());
      verifyNever(mockController.reload(any));
    });

    test('''
      Should call NavigationControllerProxy.goBack()
      when GoBackAction is added to the webPageBloc.
      ''', () async {
      webPageBloc.request.add(GoBackAction());

      await untilCalled(mockController.goBack());
      verify(mockController.goBack()).called(1);
      verifyNever(mockController.loadUrl(any, any));
      verifyNever(mockController.goForward());
      verifyNever(mockController.reload(any));
    });

    test('''
      Should call NavigationControllerProxy.goForward()
      when GoBackAction is added to the webPageBloc.
      ''', () async {
      webPageBloc.request.add(GoForwardAction());

      await untilCalled(mockController.goForward());
      verify(mockController.goForward()).called(1);
      verifyNever(mockController.loadUrl(any, any));
      verifyNever(mockController.goBack());
      verifyNever(mockController.reload(any));
    });

    test('''
      Should call NavigationControllerProxy.reload()
      when RefreshAction is added to the webPageBloc.
      ''', () async {
      webPageBloc.request.add(RefreshAction());

      await untilCalled(mockController.reload(any));
      verify(mockController.reload(web.ReloadType.partialCache)).called(1);
      verifyNever(mockController.loadUrl(any, any));
      verifyNever(mockController.goBack());
      verifyNever(mockController.goForward());
    });
  });
}

class MockNavigationControllerProxy extends Mock
    implements web.NavigationControllerProxy {}

class MockNavigationEventListenerBinding extends Mock
    implements web.NavigationEventListenerBinding {}

class MockPopupFrameCreationListenerBinding extends Mock
    implements web.PopupFrameCreationListenerBinding {}

class MockNavigationState extends Mock implements web.NavigationState {}
