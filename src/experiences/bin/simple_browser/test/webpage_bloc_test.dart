// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import 'package:fidl_fuchsia_web/fidl_async.dart' as web;
import 'package:fuchsia_logger/logger.dart';
import 'package:test/test.dart';
import 'package:mockito/mockito.dart';

// ignore_for_file: implementation_imports
import 'package:simple_browser/src/blocs/webpage_bloc.dart';
import 'package:simple_browser/src/models/webpage_action.dart';
import 'package:simple_browser/src/services/simple_browser_web_service.dart';
import 'package:simple_browser/src/services/simple_browser_navigation_event_listener.dart';

void main() {
  setupLogger(name: 'webpage_bloc_test');

  MockSimpleBrowserWebService mockSimpleBrowserWebService;
  MockNavigationState mockNavigationState;
  WebPageBloc webPageBloc;
  SimpleBrowserNavigationEventListener simpleBrowserNavigationEventListener;

  setUp(() {
    mockSimpleBrowserWebService = MockSimpleBrowserWebService();
    mockNavigationState = MockNavigationState();
    simpleBrowserNavigationEventListener =
        SimpleBrowserNavigationEventListener();
    when(mockSimpleBrowserWebService.navigationEventListener)
        .thenReturn(simpleBrowserNavigationEventListener);
    webPageBloc = WebPageBloc(
      webService: mockSimpleBrowserWebService,
    );
  });

  /// Tests if relavent getters in [WebPageBloc] are properly getting updated values
  /// in [SimpleBrowserNavigationEventListener] when [NavigationState] is changed.
  group('onNavigationStateChanged', () {
    test('''WebPageBloc.url should be updated
        when NavigationState.url changed.''', () {
      //  url should be an empty string by default.
      expect(webPageBloc.url, '',
          reason: '''The initial value of url is expected to be a blank, 
          but is actually ${webPageBloc.url}.''');

      // When NavigationState.url is changed to 'https://www.flutter.dev'.
      String testUrl = 'https://www.flutter.dev';
      when(mockNavigationState.url).thenReturn(testUrl);
      simpleBrowserNavigationEventListener
          .onNavigationStateChanged(mockNavigationState);
      expect(webPageBloc.url, testUrl,
          reason: '''url value is expected to be updated to $testUrl,
          but is actually ${webPageBloc.url}.''');
    });

    test('''WebPageBloc.forwardState should be updated
        when NavigationState.canGoForward changed.''', () {
      // forwardState should be false by default.
      expect(webPageBloc.forwardState, false,
          reason: '''The initial value of forwardState is expected to be false,
          but is actually ${webPageBloc.forwardState}.''');

      // when NavigationState.canGoForward is changed to true.
      when(mockNavigationState.canGoForward).thenReturn(true);
      simpleBrowserNavigationEventListener
          .onNavigationStateChanged(mockNavigationState);
      expect(webPageBloc.forwardState, true,
          reason: '''forwardState is expected to be updated to true,
          but is actually ${webPageBloc.forwardState}.''');
    });

    test('''WebPageBloc.backState should be updated
        when NavigationState.canGoBack changed.''', () {
      // backState should be false by default.
      expect(webPageBloc.backState, false,
          reason: '''The initial value of backState is expected to be false,
          but is actually ${webPageBloc.backState}.''');

      // when NavigationState.canGoBack is changed to true.
      when(mockNavigationState.canGoBack).thenReturn(true);
      simpleBrowserNavigationEventListener
          .onNavigationStateChanged(mockNavigationState);
      expect(webPageBloc.backState, true,
          reason: '''backState is expected to be updated to true,
          but is actually ${webPageBloc.backState}.''');
    });

    test('''WebPageBloc.isLoadedState should be updated
        when NavigationState.isMainDocumentLoaded changed.''', () {
      // isLoadedState should be true by default.
      expect(webPageBloc.isLoadedState, true,
          reason: '''The initial value of isLoadedState is expected to be true,
          but is actually ${webPageBloc.isLoadedState}.''');

      // when NavigationState.isMainDocumentLoaded is changed to false.
      when(mockNavigationState.isMainDocumentLoaded).thenReturn(false);
      simpleBrowserNavigationEventListener
          .onNavigationStateChanged(mockNavigationState);
      expect(webPageBloc.isLoadedState, false,
          reason: '''isLoadedState is expected to be updated to false,
          but is actually ${webPageBloc.isLoadedState}.''');
    });

    test('''WebPageBloc.pageTitle should be updated
        when NavigationState.title changed.''', () {
      // pageTitle has no default value.

      // when NavigationState.title is changed to 'test'.
      String testTitle = 'test';
      when(mockNavigationState.title).thenReturn(testTitle);
      simpleBrowserNavigationEventListener
          .onNavigationStateChanged(mockNavigationState);
      expect(webPageBloc.pageTitle, testTitle,
          reason: '''pageTitle is expected to be updated to $testTitle,
          but is actually ${webPageBloc.pageTitle}.''');
    });

    test('''WebPageBloc.pageType should be updated
        when NavigationState.pageType changed.''', () {
      //  pageType should be PageType.empty by default.
      expect(webPageBloc.pageType, PageType.empty,
          reason:
              '''The initial value of pageType is expected to be PageType.empty,
              but is actually ${webPageBloc.pageType.toString()}.''');

      // when NavigationState.pageType is changed to web.PageType.normal.
      web.PageType testPageType = web.PageType.normal;
      when(mockNavigationState.pageType).thenReturn(testPageType);
      simpleBrowserNavigationEventListener
          .onNavigationStateChanged(mockNavigationState);
      expect(webPageBloc.pageType, PageType.normal,
          reason: '''pageType is expected to be updated to PageType.normal,
              but is actually ${webPageBloc.pageType.toString()}.''');

      // when NavigationState.pageType is changed to web.PageType.error.
      testPageType = web.PageType.error;
      when(mockNavigationState.pageType).thenReturn(testPageType);
      simpleBrowserNavigationEventListener
          .onNavigationStateChanged(mockNavigationState);
      expect(webPageBloc.pageType, PageType.error,
          reason: '''pageType is expected to be updated to PageType.error,
              but is actually ${webPageBloc.pageType.toString()}.''');
    });
  });

  /// Tests [WebPageBloc]'s [StreamController] and its callback [_onActionChanged].
  ///
  /// Verify if relavent methods in [SimpleBrowserWebService] are called,
  /// and irrelavent ones are not called through the callback when a [WebPageAction]
  /// is added to the bloc.
  group('Handling actions', () {
    test('''
      Should call NavigationControllerProxy.loadUrl() with the given url
      when NavigateToAction is added to the webPageBloc with a normal url.
      ''', () async {
      String testUrl = 'https://www.google.com';

      webPageBloc.request.add(NavigateToAction(url: testUrl));

      await untilCalled(webPageBloc.webService.loadUrl(any));
      verify(webPageBloc.webService.loadUrl(testUrl)).called(1);
      verifyNever(webPageBloc.webService.goBack());
      verifyNever(webPageBloc.webService.goForward());
      verifyNever(webPageBloc.webService.refresh());
    });

    test('''
      Should call NavigationControllerProxy.loadUrl() with the given url
      when NavigateToAction is added to the webPageBloc with a search query url.
      ''', () async {
      String testUrl = 'https://www.google.com/search?q=cat';

      webPageBloc.request.add(NavigateToAction(url: testUrl));

      await untilCalled(webPageBloc.webService.loadUrl(any));
      verify(webPageBloc.webService.loadUrl(testUrl)).called(1);
      verifyNever(webPageBloc.webService.goBack());
      verifyNever(webPageBloc.webService.goForward());
      verifyNever(webPageBloc.webService.refresh());
    });

    test('''
      Should call NavigationControllerProxy.goBack()
      when GoBackAction is added to the webPageBloc.
      ''', () async {
      webPageBloc.request.add(GoBackAction());

      await untilCalled(webPageBloc.webService.goBack());
      verify(webPageBloc.webService.goBack()).called(1);
      verifyNever(webPageBloc.webService.loadUrl(any));
      verifyNever(webPageBloc.webService.goForward());
      verifyNever(webPageBloc.webService.refresh());
    });

    test('''
      Should call NavigationControllerProxy.goForward()
      when GoBackAction is added to the webPageBloc.
      ''', () async {
      webPageBloc.request.add(GoForwardAction());

      await untilCalled(webPageBloc.webService.goForward());
      verify(webPageBloc.webService.goForward()).called(1);
      verifyNever(webPageBloc.webService.loadUrl(any));
      verifyNever(webPageBloc.webService.goBack());
      verifyNever(webPageBloc.webService.refresh());
    });

    test('''
      Should call NavigationControllerProxy.reload()
      when RefreshAction is added to the webPageBloc.
      ''', () async {
      webPageBloc.request.add(RefreshAction());

      await untilCalled(webPageBloc.webService.refresh());
      verify(webPageBloc.webService.refresh()).called(1);
      verifyNever(webPageBloc.webService.loadUrl(any));
      verifyNever(webPageBloc.webService.goBack());
      verifyNever(webPageBloc.webService.goForward());
    });
  });
}

class MockSimpleBrowserWebService extends Mock
    implements SimpleBrowserWebService {}

class MockNavigationState extends Mock implements web.NavigationState {}
