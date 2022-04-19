// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine_utils/ermine_utils.dart';
import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';
import 'package:ermine/src/states/app_state.dart';
import 'package:internationalization/strings.dart';

const _kPageVerticalPaddings = 100.0;
const _kContentWidth = 744.0;

/// Full-screen pages to handle the user feedback flow.
class UserFeedback extends StatelessWidget {
  final AppState app;

  const UserFeedback(this.app);

  @override
  Widget build(BuildContext context) => Observer(builder: (context) {
        // TODO(fxb/88445): Implement other pages
        switch (app.feedbackPage) {
          case FeedbackPage.ready:
            return UserFeedbackForm(app);
          case FeedbackPage.submitted:
            return UserFeedbackSubmitted(app);
          default:
            return Offstage();
        }
      });
}

/// A page that displays a form to get the user data to file their feedback
class UserFeedbackForm extends StatelessWidget {
  final AppState app;
  final _formKey = GlobalKey<FormState>();
  final _summaryController = TextEditingController();
  final _descController = TextEditingController();
  final _usernameController = TextEditingController();

  UserFeedbackForm(this.app);

  @override
  Widget build(BuildContext context) {
    final _theme = Theme.of(context);
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
                        SizedBox(height: 24),
                        // Warning
                        Text(
                          Strings.noPII,
                          style: _theme.textTheme.bodyText1!
                              .copyWith(color: _theme.errorColor),
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
                              // Summary
                              TextFormField(
                                maxLines: 1,
                                controller: _summaryController,
                                autofocus: true,
                                decoration: InputDecoration(
                                  labelText: Strings.summary,
                                  border: OutlineInputBorder(
                                      borderRadius: BorderRadius.zero),
                                ),
                              ),
                              SizedBox(height: 36),
                              // Description
                              // TODO(fxb/97851): Enter doesn't add a new line in the field.
                              TextFormField(
                                minLines: 5,
                                maxLines: 5,
                                textAlignVertical: TextAlignVertical.top,
                                keyboardType: TextInputType.multiline,
                                textInputAction: TextInputAction.newline,
                                controller: _descController,
                                autovalidateMode:
                                    AutovalidateMode.onUserInteraction,
                                decoration: InputDecoration(
                                  labelText: '${Strings.description}*',
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
                              // TODO(fxb/88445): Add text links
                              Text('${Strings.dataSharingLegalStatement}',
                                  style: _theme.textTheme.bodyText1),
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
                        child: Text(Strings.cancel),
                        onPressed: app.closeUserFeedback,
                      ),
                      SizedBox(width: 16),
                      ElevatedButton(
                        child: Text(Strings.submit),
                        onPressed: () =>
                            _formKey.currentState?.validate() ?? false
                                ? app.userFeedbackSubmit(
                                    summary: _summaryController.text,
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
                        Text(Strings.submittedDesc1,
                            style: Theme.of(context).textTheme.bodyText1),
                        SizedBox(height: 24),
                        Text(app.feedbackUuid,
                            style: Theme.of(context).textTheme.headline6),
                        SizedBox(height: 24),
                        Text(Strings.submittedDesc2,
                            style: Theme.of(context).textTheme.bodyText1),
                      ],
                    ),
                  ),
                  OutlinedButton(
                    child: Text(Strings.close),
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
