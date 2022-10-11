// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(http://fxbug.dev/107480): Resolve lint issues and reenable analysis for file
// ignore_for_file: deprecated_member_use

import 'package:ermine/src/states/app_state.dart';
import 'package:ermine_utils/ermine_utils.dart';
import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';
import 'package:internationalization/strings.dart';

const _kPageVerticalPaddings = 100.0;
const _kContentWidth = 744.0;

/// Full-screen pages to handle the user feedback flow.
class UserFeedback extends StatelessWidget {
  final AppState app;

  const UserFeedback(this.app);

  @override
  Widget build(BuildContext context) => Observer(builder: (context) {
        final isScrim = app.feedbackPage == FeedbackPage.scrim;
        // TODO(fxb/88445): Implement other pages
        switch (app.feedbackPage) {
          case FeedbackPage.scrim:
          case FeedbackPage.ready:
            return Stack(
              children: [
                UserFeedbackForm(app, isOutFocused: isScrim),
                if (app.feedbackPage == FeedbackPage.scrim)
                  Container(
                    color: Theme.of(context).canvasColor.withOpacity(0.6),
                  ),
              ],
            );
          case FeedbackPage.submitted:
            return UserFeedbackSubmitted(app);
          case FeedbackPage.failed:
            return UserFeedbackError(app);
          default:
            return Offstage();
        }
      });
}

/// A page that displays a form to get the user data to file their feedback
class UserFeedbackForm extends StatelessWidget {
  final AppState app;
  final bool isOutFocused;
  final _formKey = GlobalKey<FormState>();
  final _titleController = TextEditingController();
  final _descController = TextEditingController();
  final _usernameController = TextEditingController();
  final _descFocusNode = FocusNode();

  UserFeedbackForm(this.app, {required this.isOutFocused});

  @override
  Widget build(BuildContext context) {
    final _theme = Theme.of(context);
    final _lang = app.simpleLocale;
    final _legalHelpUrl = _lang.isEmpty
        ? 'https://policies.google.com/terms'
        : 'https://policies.google.com/terms?hl=$_lang';
    final _privacyPolicyUrl = _lang.isEmpty
        ? 'https://policies.google.com/privacy'
        : 'https://policies.google.com/privacy?hl=$_lang';
    final _termsOfServiceUrl = _lang.isEmpty
        ? 'https://policies.google.com/terms'
        : 'https://policies.google.com/terms?hl=$_lang';

    return FocusScope(
      child: Container(
        padding: EdgeInsets.symmetric(vertical: _kPageVerticalPaddings),
        color: _theme.bottomAppBarColor,
        child: Center(
          child: SizedBox(
            width: _kContentWidth,
            child: Observer(builder: (context) {
              return Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  // Header
                  Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        // Title
                        Text(
                          Strings.sendFeedback,
                          style: Theme.of(context).textTheme.headline5,
                        ),
                        SizedBox(height: 40),
                      ]),

                  // Scrollable body
                  Expanded(
                    child: SingleChildScrollView(
                      child: SizedBox(
                        width: _kContentWidth,
                        child: Form(
                          key: _formKey,
                          child: Column(
                            children: [
                              // IssueTitle
                              TextFormField(
                                maxLines: 1,
                                controller: _titleController,
                                autofocus: !isOutFocused,
                                decoration: InputDecoration(
                                  labelText: Strings.issueTitle,
                                  border: OutlineInputBorder(
                                      borderRadius: BorderRadius.zero),
                                ),
                              ),
                              SizedBox(height: 36),
                              // Description
                              TextFormField(
                                minLines: 5,
                                maxLines: 5,
                                textAlignVertical: TextAlignVertical.top,
                                keyboardType: TextInputType.multiline,
                                textInputAction: TextInputAction.newline,
                                controller: _descController,
                                focusNode: _descFocusNode,
                                autovalidateMode:
                                    AutovalidateMode.onUserInteraction,
                                decoration: InputDecoration(
                                  labelText: '${Strings.description}*',
                                  hintText: Strings.noPII,
                                  alignLabelWithHint: true,
                                  border: OutlineInputBorder(
                                      borderRadius: BorderRadius.zero),
                                ),
                                validator: (value) =>
                                    value == null || value.isEmpty
                                        ? Strings.needDescription
                                        : null,
                              ),
                              SizedBox(height: 36),
                              // Username
                              Row(
                                mainAxisSize: MainAxisSize.max,
                                children: [
                                  Expanded(
                                      child: TextFormField(
                                    maxLines: 1,
                                    controller: _usernameController,
                                    autovalidateMode:
                                        AutovalidateMode.onUserInteraction,
                                    decoration: InputDecoration(
                                      labelText: '${Strings.username}*',
                                      border: OutlineInputBorder(
                                          borderRadius: BorderRadius.zero),
                                    ),
                                    validator: (value) =>
                                        value == null || value.isEmpty
                                            ? Strings.needUsername
                                            : null,
                                  )),
                                  SizedBox(width: 16),
                                  Text('@google.com',
                                      style: _theme.textTheme.bodyText1),
                                ],
                              ),
                              // TODO(fxb/97464): Add the screenshot UX when the bug is fixed.
                              SizedBox(height: 40),
                              MarkdownRichText(
                                Strings.dataSharingLegalStatement(_legalHelpUrl,
                                    _privacyPolicyUrl, _termsOfServiceUrl),
                                urlLauncher: (url) {
                                  app.launch(url, url);
                                },
                              ),
                            ],
                          ),
                        ),
                      ),
                    ),
                  ),
                  SizedBox(height: 40),

                  // Footer (CTAs)
                  Row(
                    mainAxisAlignment: MainAxisAlignment.start,
                    children: [
                      OutlinedButton(
                        child: Text(Strings.cancel.toUpperCase()),
                        onPressed: app.closeUserFeedback,
                      ),
                      SizedBox(width: 16),
                      ElevatedButton(
                        child: Text(Strings.submit.toUpperCase()),
                        onPressed: () =>
                            _formKey.currentState?.validate() ?? false
                                ? app.userFeedbackSubmit(
                                    title: _titleController.text,
                                    desc: _descController.text,
                                    username: _usernameController.text,
                                  )
                                : null,
                      ),
                    ],
                  ),
                ],
              );
            }),
          ),
        ),
      ),
    );
  }
}

/// A page displayed when the feedback report submission is completed.
class UserFeedbackSubmitted extends StatelessWidget {
  final AppState app;

  const UserFeedbackSubmitted(this.app);

  @override
  Widget build(BuildContext context) => Container(
        padding: EdgeInsets.symmetric(vertical: _kPageVerticalPaddings),
        color: Theme.of(context).bottomAppBarColor,
        child: FocusScope(
          child: Center(
            child: SizedBox(
              width: _kContentWidth,
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Expanded(
                    child: Column(
                      mainAxisSize: MainAxisSize.min,
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(Strings.submittedTitle,
                            style: Theme.of(context).textTheme.headline5),
                        SizedBox(height: 32),
                        Text(Strings.submittedDesc(app.feedbackUuid),
                            style: Theme.of(context).textTheme.bodyText1),
                      ],
                    ),
                  ),
                  OutlinedButton(
                    child: Text(Strings.close.toUpperCase()),
                    style: ErmineButtonStyle.outlinedButton(Theme.of(context)),
                    onPressed: app.closeUserFeedback,
                  ),
                ],
              ),
            ),
          ),
        ),
      );
}

/// A page displayed when filing report has been failed.
class UserFeedbackError extends StatelessWidget {
  final AppState app;

  const UserFeedbackError(this.app);

  @override
  Widget build(BuildContext context) => Container(
        padding: EdgeInsets.symmetric(vertical: _kPageVerticalPaddings),
        color: Theme.of(context).bottomAppBarColor,
        child: FocusScope(
          child: Center(
            child: SizedBox(
              width: _kContentWidth,
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Expanded(
                    child: Column(
                      mainAxisSize: MainAxisSize.min,
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(Strings.failedToFileTitle,
                            style: Theme.of(context).textTheme.headline5),
                        SizedBox(height: 32),
                        Text(app.feedbackErrorMsg,
                            style: Theme.of(context).textTheme.bodyText1),
                        SizedBox(height: 24),
                        Text(
                            Strings.failedToFileDesc('go/workstation-feedback'),
                            style: Theme.of(context).textTheme.bodyText1),
                      ],
                    ),
                  ),
                  OutlinedButton(
                    child: Text(Strings.close.toUpperCase()),
                    style: ErmineButtonStyle.outlinedButton(Theme.of(context)),
                    onPressed: app.closeUserFeedback,
                  ),
                ],
              ),
            ),
          ),
        ),
      );
}
