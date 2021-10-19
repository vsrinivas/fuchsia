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

  const WiFiSettings({required this.state});

  @override
  Widget build(BuildContext context) {
    return Observer(builder: (_) {
      final networks = state.availableNetworks;
      return SettingDetails(
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
                        : TextStyle(color: Theme.of(context).disabledColor)),
                leading: Icon(networkIcon,
                    color: networkCompatible
                        ? null
                        : Theme.of(context).disabledColor),
                onTap: null,
              );
            }),
      );
    });
  }
}
