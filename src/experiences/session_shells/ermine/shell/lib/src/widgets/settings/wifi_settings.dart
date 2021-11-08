// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine/src/states/settings_state.dart';
import 'package:ermine/src/widgets/settings/setting_details.dart';
import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';
import 'package:internationalization/strings.dart';

/// Defines a widget to control WiFi in [SettingDetails] widget.
class WiFiSettings extends StatelessWidget {
  final SettingsState state;
  final ValueChanged<String> onChange;

  const WiFiSettings({required this.state, required this.onChange});

  @override
  Widget build(BuildContext context) {
    return Observer(builder: (_) {
      final availableNetworks = state.availableNetworks;
      final savedNetworks = state.savedNetworks;
      return Column(
        children: [
          Expanded(
            child: SettingDetails(
              title: Strings.wifi,
              onBack: state.showAllSettings,
              child: SingleChildScrollView(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Padding(
                      padding:
                          EdgeInsets.symmetric(horizontal: 24, vertical: 16),
                      child: Text(Strings.savedNetworks),
                    ),
                    if (savedNetworks.isEmpty)
                      ListTile(
                        title: Text(Strings.loading.toLowerCase()),
                      ),
                    ListView.builder(
                        shrinkWrap: true,
                        primary: false,
                        itemCount: savedNetworks.length,
                        itemBuilder: (context, index) {
                          final networkName = savedNetworks[index].name;
                          final networkIcon = savedNetworks[index].icon;
                          final networkCompatible =
                              savedNetworks[index].compatible;
                          return ListTile(
                            title: Text(networkName,
                                maxLines: 1,
                                overflow: TextOverflow.ellipsis,
                                style: networkCompatible
                                    ? null
                                    : TextStyle(
                                        color:
                                            Theme.of(context).disabledColor)),
                            leading: Icon(networkIcon,
                                color: networkCompatible
                                    ? null
                                    : Theme.of(context).disabledColor),
                            onTap: () => onChange(savedNetworks[index].name),
                            trailing: PopupMenuButton(
                              itemBuilder: (context) {
                                return [
                                  PopupMenuItem(
                                    child: Text(Strings.forget),
                                    value: networkName,
                                  ),
                                ];
                              },
                              onSelected: state.removeNetwork,
                              tooltip: Strings.forget,
                            ),
                          );
                        }),
                    Padding(
                      padding:
                          EdgeInsets.symmetric(horizontal: 24, vertical: 16),
                      child: Text(Strings.otherNetworks),
                    ),
                    if (availableNetworks.isEmpty)
                      ListTile(
                        title: Text(Strings.loading.toLowerCase()),
                      ),
                    ListView.builder(
                        shrinkWrap: true,
                        primary: false,
                        itemCount: availableNetworks.length,
                        itemBuilder: (context, index) {
                          final networkName = availableNetworks[index].name;
                          final networkIcon = availableNetworks[index].icon;
                          final networkCompatible =
                              availableNetworks[index].compatible;
                          return ListTile(
                            title: Text(networkName,
                                maxLines: 1,
                                overflow: TextOverflow.ellipsis,
                                style: networkCompatible
                                    ? null
                                    : TextStyle(
                                        color:
                                            Theme.of(context).disabledColor)),
                            leading: Icon(networkIcon,
                                color: networkCompatible
                                    ? null
                                    : Theme.of(context).disabledColor),
                            onTap: () =>
                                onChange(availableNetworks[index].name),
                            trailing: ((state.targetNetwork != '') &&
                                    (state.targetNetwork == networkName))
                                ? Icon(Icons.check_outlined)
                                : null,
                          );
                        }),
                  ],
                ),
              ),
            ),
          ),
          _buildPasswordPrompt(context),
        ],
      );
    });
  }

  Widget _buildPasswordPrompt(BuildContext context) {
    bool networkSelected = state.targetNetwork != '';
    return AppBar(
      elevation: 0,
      title: networkSelected
          ? TextField(
              controller: state.networkPasswordTextController,
              maxLines: 1,
              decoration: InputDecoration(
                border: InputBorder.none,
                hintText: Strings.enterPasswordForNetwork(state.targetNetwork),
              ),
            )
          : Text(
              Strings.selectNetwork,
              style: Theme.of(context).textTheme.bodyText2,
            ),
      shape: Border(top: BorderSide(color: Theme.of(context).indicatorColor)),
      actions: [
        if (networkSelected)
          Padding(
            padding: EdgeInsets.fromLTRB(8, 12, 24, 12),
            child: ElevatedButton(
              onPressed: () =>
                  _enterPassword(state.networkPasswordTextController),
              child: Text(Strings.connect),
            ),
          ),
      ],
    );
  }

  void _enterPassword(TextEditingController textController) {
    state.connectToWPA2Network(textController.text);
    textController.clear();
  }
}
