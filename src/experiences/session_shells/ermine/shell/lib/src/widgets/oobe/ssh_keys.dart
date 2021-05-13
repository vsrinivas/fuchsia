// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';

import 'package:ermine_ui/ermine_ui.dart';
import 'package:fidl_fuchsia_ssh/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import 'package:internationalization/strings.dart';
import 'package:fuchsia_services/services.dart' show Incoming;

import 'package:fuchsia_logger/logger.dart';

import '../../utils/styles.dart';
import 'oobe_buttons.dart';
import 'oobe_header.dart';

const String kGithubApiUrl = 'api.github.com';

const double kBottomMargin = 52;
const double kTextFieldVerticalMargins = 56;
const double kBodyWidth = 904;

// Spacing between field name and field value
const double kFieldHorizontalSpacing = 24;

// Vertical space between username and key text.
const double kFieldVerticalSpacing = 24;

// Spacing between field name and field value offset by whitespace around radio button.
const double kFieldHorizontalSpacingOffset = 11;

// Vertical space between fields offset by whitespace around radio button.
const double kFieldVerticalSpacingOffset = 18;

// Horizontal padding between radio buttons for selecting the input method.
const double kRadioButtonPadding = 48;

// Padding to align expand icon with radio button.
const double kIconPaddingTop = 9;

// Padding between key and expand icon
const double kIconPaddingLeft = 12;

// Padding above key to align with radio button
const double kKeyPaddingTop = 8;

const double kIconSize = 24;

// Number of characters per line when displaying a key.
const int kLineLength = 63;

// Maximum number of lines in text input field.
const int kMaxLinesGithub = 1;
const int kMaxLinesManual = 99;

class SshKeys extends StatelessWidget {
  final VoidCallback onBack;
  final VoidCallback onNext;
  final SshKeysModel model;

  const SshKeys(
      {@required this.onBack, @required this.onNext, @required this.model});

  factory SshKeys.withSvcPath(VoidCallback onBack, VoidCallback onNext) {
    final control = AuthorizedKeysProxy();
    Incoming.fromSvcPath().connectToService(control);
    final sshKeysModel = SshKeysModel(control: control, client: http.Client());
    return SshKeys(onBack: onBack, onNext: onNext, model: sshKeysModel);
  }

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: model.visibleScreen,
      builder: (context, _) {
        switch (model.visibleScreen.value) {
          case Screen.add:
            return buildAddScreen();
          case Screen.confirm:
            return buildConfirmScreen();
          case Screen.error:
            return buildErrorScreen();
          case Screen.exit:
            return buildExitScreen();
          default:
            return buildAddScreen();
        }
      },
    );
  }

  Widget buildAddScreen() => Column(
        children: <Widget>[
          OobeHeader(Strings.oobeSshKeysAddTitle,
              [DescriptionModel(text: Strings.oobeSshKeysAddDesc)]),
          // Radio buttons
          AnimatedBuilder(
            animation: model.importMethod,
            builder: (context, _) => Expanded(
              child: Container(
                width: ErmineStyle.kOobeDescriptionWidth,
                margin: EdgeInsets.only(
                    top: ErmineStyle.kOobeBodyVerticalMargins,
                    bottom: kBottomMargin),
                child: Column(
                  children: <Widget>[
                    // Radio buttons.
                    Row(
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: <Widget>[
                        ErmineRadio<ImportMethod>(
                          value: ImportMethod.github,
                          groupValue: model.importMethod.value,
                          onChanged: (ImportMethod method) {
                            model.controller.clear();
                            model.importMethod.value = method;
                          },
                        ),
                        Text(
                          Strings.oobeSshKeysGithubMethod,
                          style: ErmineTextStyles.headline4,
                        ),
                        Padding(
                          padding: EdgeInsets.only(right: kRadioButtonPadding),
                        ),
                        ErmineRadio<ImportMethod>(
                          value: ImportMethod.manual,
                          groupValue: model.importMethod.value,
                          onChanged: (ImportMethod method) {
                            model.controller.clear();
                            model.importMethod.value = method;
                          },
                        ),
                        Text(
                          Strings.oobeSshKeysManualMethod,
                          style: ErmineTextStyles.headline4,
                        ),
                      ],
                    ),
                    // Text field.
                    model.importMethod.value == ImportMethod.github
                        ? buildUsernameInput()
                        : buildManualInput(),
                  ],
                ),
              ),
            ),
          ),
          // Buttons
          OobeButtons([
            OobeButtonModel(Strings.back, onBack),
            OobeButtonModel(Strings.skip, onNext),
            OobeButtonModel(Strings.add, model.onAdd, filled: true)
          ]),
        ],
      );

  Widget buildUsernameInput() => Container(
        padding: EdgeInsets.symmetric(vertical: kTextFieldVerticalMargins),
        child: ErmineTextField(
          controller: model.controller,
          autofocus: true,
          maxLines: kMaxLinesGithub,
          labelText: Strings.username,
        ),
      );

  Widget buildManualInput() => Expanded(
        child: Container(
          padding: EdgeInsets.symmetric(vertical: kTextFieldVerticalMargins),
          child: ErmineTextField(
            controller: model.controller,
            autofocus: true,
            expands: true,
            maxLines: kMaxLinesManual,
            labelText: Strings.key,
          ),
        ),
      );

  Widget buildConfirmScreen() {
    final _scrollController = ScrollController();
    final isExpanded = List.generate(
        model.keyList.length, (_) => ValueNotifier(false),
        growable: false);
    return Column(
      children: <Widget>[
        OobeHeader(
          Strings.oobeSshKeysConfirmTitle,
          [
            DescriptionModel(
                text: Strings.oobeSshKeysSelectionDesc(model.keyList.length)),
          ],
        ),
        Expanded(
          child: Container(
            margin: EdgeInsets.only(
                top: ErmineStyle.kOobeBodyVerticalMargins,
                bottom: kBottomMargin),
            child: AnimatedBuilder(
              animation: Listenable.merge([
                model.currentKey,
                _scrollController,
                for (final expanded in isExpanded) expanded,
              ]),
              builder: (context, _) {
                return Container(
                  width: kBodyWidth,
                  child: Row(
                    children: <Widget>[
                      Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: <Widget>[
                          Container(
                            margin:
                                EdgeInsets.only(bottom: kFieldVerticalSpacing),
                            child: Text(
                              Strings.username,
                              style: ErmineTextStyles.headline4
                                  .copyWith(fontWeight: FontWeight.w700),
                            ),
                          ),
                          Text(
                            Strings.sshKeys,
                            style: ErmineTextStyles.headline4
                                .copyWith(fontWeight: FontWeight.w700),
                          ),
                        ],
                      ),
                      Expanded(
                        child: Column(
                          children: <Widget>[
                            Container(
                              margin: EdgeInsets.only(
                                left: kFieldHorizontalSpacing,
                                bottom: kFieldVerticalSpacingOffset,
                              ),
                              alignment: Alignment.centerLeft,
                              child: Text(
                                '${model.username}',
                                style: ErmineTextStyles.headline4,
                              ),
                            ),
                            Expanded(
                              child: Container(
                                margin: EdgeInsets.only(
                                  left: kFieldHorizontalSpacingOffset,
                                ),
                                child: Scrollbar(
                                  controller: _scrollController,
                                  isAlwaysShown: true,
                                  thickness: 4.0,
                                  child: SingleChildScrollView(
                                    controller: _scrollController,
                                    scrollDirection: Axis.vertical,
                                    child: Column(
                                      children: <Widget>[
                                        for (int i = 0;
                                            i < model.keyList.length;
                                            i++)
                                          buildKeyRow(i, isExpanded[i]),
                                      ],
                                    ),
                                  ),
                                ),
                              ),
                            ),
                          ],
                        ),
                      ),
                    ],
                  ),
                );
              },
            ),
          ),
        ),
        // Buttons
        OobeButtons([
          OobeButtonModel(Strings.retry, model.showAdd),
          OobeButtonModel(Strings.add, model.confirmKey),
        ]),
      ],
    );
  }

  Widget buildKeyRow(int i, ValueNotifier<bool> isExpanded) => Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: <Widget>[
          ErmineRadio<int>(
            value: i,
            groupValue: model.currentKey.value,
            onChanged: (int j) {
              model.currentKey.value = j;
            },
          ),
          Container(
            // Push text down a bit to account for whitespace around radio button
            padding: EdgeInsets.only(top: kKeyPaddingTop),
            child: Text(
              // Clip key manually as automatic overflow prevention cuts by word
              // Since the key is interpreted as one long word the entire thing
              // will be cut, or if it contains a '/' it will be clipped there.
              isExpanded.value
                  ? splitKeyIntoLines(model.keyList[i])
                  : '${model.keyList[i].substring(0, kLineLength - 3)}...',
              style: ErmineTextStyles.headline4,
            ),
          ),
          GestureDetector(
            child: Container(
              padding: EdgeInsets.only(
                top: kIconPaddingTop,
                left: kIconPaddingLeft,
              ),
              child: Icon(
                isExpanded.value ? Icons.expand_less : Icons.expand_more,
                color: ErmineColors.grey100,
                size: kIconSize,
              ),
            ),
            onTap: () {
              isExpanded.value = !isExpanded.value;
            },
          ),
        ],
      );

  // Manually split key into lines since the Text widget automatic wrapping
  // breaks at the '/' character which is not the behaviour we want for keys
  String splitKeyIntoLines(String key) {
    final buffer = StringBuffer();
    int i = 0;
    for (; i < key.length ~/ kLineLength; i++)
      buffer
          .write('${key.substring(i * kLineLength, (i + 1) * kLineLength)}\n');
    if ((key.length % kLineLength) != 0) {
      buffer.write(key.substring(i * kLineLength));
    }
    return buffer.toString();
  }

  Widget buildErrorScreen() => Column(
        children: <Widget>[
          OobeHeader(Strings.oobeSshKeysErrorTitle,
              [DescriptionModel(text: model.errorMessage)]),
          const Spacer(),
          // Buttons
          OobeButtons([
            OobeButtonModel(Strings.retry, model.showAdd),
            OobeButtonModel(Strings.skip, onNext),
          ]),
        ],
      );

  Widget buildExitScreen() => Column(children: <Widget>[
        OobeHeader(
          Strings.oobeSshKeysSuccessTitle,
          [DescriptionModel(text: Strings.oobeSshKeysSuccessDesc)],
        ),
        const Spacer(),
        // Buttons
        OobeButtons([
          OobeButtonModel(Strings.ok, onNext),
        ]),
      ]);
}

enum ImportMethod { github, manual }

enum Screen { add, confirm, error, exit }

class SshKeysModel {
  final AuthorizedKeysProxy control;
  final http.Client client;
  final TextEditingController controller = TextEditingController();

  ValueNotifier<ImportMethod> importMethod = ValueNotifier(ImportMethod.github);
  ValueNotifier<Screen> visibleScreen = ValueNotifier(Screen.add);

  List<String> keyList = [];
  ValueNotifier<int> currentKey = ValueNotifier(0);

  String username;
  String errorMessage = '';

  SshKeysModel({this.control, this.client});

  Future<void> onAdd() async {
    return importMethod.value == ImportMethod.github
        ? selectKey()
        : addManual();
  }

  Future<void> selectKey() async {
    currentKey.value = 0;
    username = controller.text;
    keyList = await fetchKey();
    if (keyList.isEmpty) {
      if (errorMessage.isEmpty) {
        errorMessage = Strings.oobeSshKeysGithubErrorDesc(username);
      }
      visibleScreen.value = Screen.error;
      return;
    }
    visibleScreen.value = Screen.confirm;
  }

  Future<void> addManual() async {
    currentKey.value = 0;
    keyList = [controller.text];
    return confirmKey();
  }

  void showAdd() {
    errorMessage = '';
    visibleScreen.value = Screen.add;
  }

  Future<void> confirmKey() {
    final newKey = SshAuthorizedKeyEntry(key: keyList[currentKey.value]);
    return control.addKey(newKey).then((_) {
      visibleScreen.value = Screen.exit;
    }).catchError((_) {
      errorMessage = Strings.oobeSshKeysFidlErrorDesc;
      visibleScreen.value = Screen.error;
    });
  }

  void dispose() {
    control.ctrl.close();
    controller.dispose();
  }

  Future<List<String>> fetchKey() async {
    String path = '/users/$username/keys';
    final response = await client.get(Uri.https(kGithubApiUrl, path));

    if (response.statusCode == 200) {
      List<String> keys = getKeysFromJson(response.body);
      return keys;
    } else {
      // A 404 response indicates that the user does not exist or has no public keys.
      if (response.statusCode != 404) {
        errorMessage = Strings.oobeSshKeysHttpErrorDesc(
            response.statusCode, response.reasonPhrase);
        log.info(
            'Workstation OOBE: request to get keys from github returned ${response.statusCode}: ${response.reasonPhrase}.');
      }
      return [];
    }
  }

  List<String> getKeysFromJson(String json) {
    final data = jsonDecode(json);
    List<String> keys = [];

    for (final item in data) {
      keys.add(item['key']);
    }
    return keys;
  }
}
