// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:meta/meta.dart';

// Base class for actions handled by the application's BLOC
class WebPageAction {
  final WebPageActionType op;
  const WebPageAction(this.op);
}

// Operations allowed for browsing
enum WebPageActionType { goForward, goBack, navigateTo }

// Instructs to go to the next page.
class GoForwardAction extends WebPageAction {
  const GoForwardAction() : super(WebPageActionType.goForward);
}

// Instructs to go to the previous page.
class GoBackAction extends WebPageAction {
  const GoBackAction() : super(WebPageActionType.goBack);
}

// Instructs to navigate to some url.
class NavigateToAction extends WebPageAction {
  final String url;
  NavigateToAction({@required this.url}) : super(WebPageActionType.navigateTo);
}
