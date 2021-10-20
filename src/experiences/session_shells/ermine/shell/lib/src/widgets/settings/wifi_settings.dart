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
      final networks = state.availableNetworks;
      return Column(
        children: [
          Expanded(
            child: SettingDetails(
              title: Strings.wifi,
              onBack: state.showAllSettings,
              child: ListView.builder(
                  itemCount: networks.length,
                  itemBuilder: (context, index) {
                    final networkName = networks[index].name;
                    final networkIcon = networks[index].icon;
                    final networkCompatible = networks[index].compatible;
                    return ListTile(
                      title: Text(networkName,
                          maxLines: 1,
                          overflow: TextOverflow.ellipsis,
                          style: networkCompatible
                              ? null
                              : TextStyle(
                                  color: Theme.of(context).disabledColor)),
                      leading: Icon(networkIcon,
                          color: networkCompatible
                              ? null
                              : Theme.of(context).disabledColor),
                      onTap: () => onChange(networks[index].name),
                      trailing: ((state.targetNetwork != '') &&
                              (state.targetNetwork == networkName))
                          ? Icon(Icons.check_outlined)
                          : null,
                    );
                  }),
            ),
          ),
          _buildPasswordPrompt(context),
        ],
      );
    });
  }

  Widget _buildPasswordPrompt(BuildContext context) {
    TextEditingController textController = TextEditingController();
    bool networkSelected = state.targetNetwork != '';
    return AppBar(
      elevation: 0,
      title: networkSelected
          ? TextField(
              controller: textController,
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
              onPressed: () => _enterPassword(textController),
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
