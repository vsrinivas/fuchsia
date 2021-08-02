// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';
import 'package:internationalization/strings.dart';
import 'package:mobx/mobx.dart';
import 'package:oobe/src/states/oobe_state.dart';

/// Defines a widget to configure software update channels.
class SshKeys extends StatelessWidget {
  final OobeState oobe;
  final VoidCallback onFinish;

  const SshKeys(this.oobe, {required this.onFinish});

  @override
  Widget build(BuildContext context) {
    final textController = TextEditingController();
    return Padding(
      padding: EdgeInsets.all(16),
      child: FocusScope(
        child: Observer(builder: (context) {
          return Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              // Title.
              Text(
                oobe.sshKeyTitle.value,
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.headline3,
              ),

              // Description.
              Container(
                alignment: Alignment.center,
                padding: EdgeInsets.all(24),
                child: SizedBox(
                  width: 600,
                  child: Text(
                    oobe.sshKeyDescription.value,
                    textAlign: TextAlign.center,
                    style: Theme.of(context)
                        .textTheme
                        .bodyText1!
                        .copyWith(height: 1.55),
                  ),
                ),
              ),

              Expanded(
                child: Builder(builder: (context) {
                  switch (oobe.sshScreen.value) {
                    case SshScreen.add:
                      return _buildAddScreen(textController);
                    case SshScreen.confirm:
                      return _buildConfirmScreen(textController);
                    default:
                      return Container();
                  }
                }),
              ),

              // Buttons.
              Container(
                alignment: Alignment.center,
                padding: EdgeInsets.all(24),
                child: Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    // Back button.
                    Padding(
                      padding: EdgeInsets.symmetric(horizontal: 12),
                      child: OutlinedButton(
                        onPressed: oobe.sshBackScreen,
                        child: Text(Strings.back.toUpperCase()),
                      ),
                    ),

                    // Skip button.
                    if (oobe.sshScreen.value != SshScreen.exit)
                      Padding(
                        padding: EdgeInsets.symmetric(horizontal: 12),
                        child: OutlinedButton(
                          onPressed: oobe.skip,
                          child: Text(Strings.skip.toUpperCase()),
                        ),
                      ),

                    // Add button.
                    if (oobe.sshScreen.value != SshScreen.exit &&
                        oobe.sshScreen.value != SshScreen.error)
                      Padding(
                        padding: EdgeInsets.symmetric(horizontal: 12),
                        child: OutlinedButton(
                          onPressed: () => oobe.sshAdd([textController.text]),
                          child: Text(Strings.add.toUpperCase()),
                        ),
                      ),

                    // Ok button.
                    if (oobe.sshScreen.value == SshScreen.exit)
                      Padding(
                        padding: EdgeInsets.symmetric(horizontal: 12),
                        child: OutlinedButton(
                          onPressed: onFinish,
                          child: Text(Strings.ok.toUpperCase()),
                        ),
                      ),
                  ],
                ),
              ),
            ],
          );
        }),
      ),
    );
  }

  Widget _buildAddScreen(TextEditingController textController) =>
      Observer(builder: (context) {
        return Container(
          padding: EdgeInsets.only(top: 48),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              // Github and manual radio buttons.
              Row(
                mainAxisAlignment: MainAxisAlignment.center,
                crossAxisAlignment: CrossAxisAlignment.center,
                children: [
                  Radio<SshImport>(
                    value: SshImport.github,
                    groupValue: oobe.importMethod.value,
                    onChanged: (method) => oobe.sshImportMethod([method]),
                  ),
                  Text(Strings.oobeSshKeysGithubMethod),
                  SizedBox(width: 24),
                  Radio<SshImport>(
                    value: SshImport.manual,
                    groupValue: oobe.importMethod.value,
                    onChanged: (method) => oobe.sshImportMethod([method]),
                  ),
                  Text(Strings.oobeSshKeysManualMethod),
                ],
              ),
              SizedBox(height: 24),

              // Github username.
              if (oobe.importMethod.value == SshImport.github)
                Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    Text(Strings.username),
                    Container(
                      margin: EdgeInsets.only(left: 24),
                      width: 512,
                      decoration: BoxDecoration(
                        border: Border.all(color: Colors.white),
                        borderRadius: BorderRadius.all(Radius.circular(4)),
                      ),
                      child: TextField(
                        controller: textController,
                        autofocus: true,
                        onSubmitted: (text) => oobe.sshAdd([text]),
                        decoration: InputDecoration(
                          border: InputBorder.none,
                          focusedBorder: InputBorder.none,
                          enabledBorder: InputBorder.none,
                          errorBorder: InputBorder.none,
                          disabledBorder: InputBorder.none,
                          contentPadding: EdgeInsets.symmetric(horizontal: 12),
                        ),
                      ),
                    ),
                  ],
                ),

              // Manual key.
              if (oobe.importMethod.value == SshImport.manual)
                Expanded(
                  child: Row(
                    mainAxisAlignment: MainAxisAlignment.center,
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      Text(Strings.key),
                      Container(
                        margin: EdgeInsets.only(left: 24),
                        width: 512,
                        decoration: BoxDecoration(
                          border: Border.all(color: Colors.white),
                          borderRadius: BorderRadius.all(Radius.circular(4)),
                        ),
                        child: TextField(
                          autofocus: true,
                          controller: textController,
                          maxLines: null,
                          onSubmitted: (text) => oobe.sshAdd([text]),
                          decoration: InputDecoration(
                            border: InputBorder.none,
                            focusedBorder: InputBorder.none,
                            enabledBorder: InputBorder.none,
                            errorBorder: InputBorder.none,
                            disabledBorder: InputBorder.none,
                            contentPadding:
                                EdgeInsets.symmetric(horizontal: 12),
                          ),
                        ),
                      ),
                    ],
                  ),
                ),
            ],
          ),
        );
      });

  Widget _buildConfirmScreen(TextEditingController textController) =>
      Observer(builder: (context) {
        return oobe.sshKeys.status == FutureStatus.fulfilled
            ? Padding(
                padding: EdgeInsets.only(top: 48),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.center,
                  children: [
                    // Github username.
                    SizedBox(
                      width: 904,
                      child: Row(
                        children: [
                          SizedBox(width: 133, child: Text(Strings.username)),
                          Text(textController.text),
                        ],
                      ),
                    ),
                    SizedBox(height: 24),
                    // SSh Keys.
                    Expanded(
                      child: SizedBox(
                        width: 904,
                        child: Row(
                          crossAxisAlignment: CrossAxisAlignment.stretch,
                          children: [
                            Container(
                              width: 133,
                              padding: EdgeInsets.only(top: 24),
                              child: Text(Strings.sshKeys),
                            ),
                            Expanded(
                              child: ListView.builder(
                                  itemCount: oobe.sshKeys.value!.length,
                                  itemBuilder: (context, index) {
                                    return Observer(builder: (context) {
                                      return ListTile(
                                          contentPadding: EdgeInsets.zero,
                                          selected:
                                              index == oobe.sshKeyIndex.value,
                                          leading: Radio<int>(
                                            value: index,
                                            groupValue: oobe.sshKeyIndex.value,
                                            onChanged: (_) {
                                              runInAction(() {
                                                oobe.sshKeyIndex.value = index;
                                              });
                                            },
                                          ),
                                          title: Text(
                                            oobe.sshKeys.value!
                                                .elementAt(index),
                                            softWrap: true,
                                            maxLines: 3,
                                          ),
                                          onTap: () {
                                            runInAction(() {
                                              oobe.sshKeyIndex.value = index;
                                            });
                                          });
                                    });
                                  }),
                            ),
                          ],
                        ),
                      ),
                    ),
                  ],
                ),
              )
            : Center(child: CircularProgressIndicator());
      });
}
