// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(http://fxbug.dev/107480): Resolve lint issues and reenable analysis for file
// ignore_for_file: deprecated_member_use

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
      final availableNetworks = state.availableNetworks;
      final savedNetworks = state.savedNetworks;
      return Column(
        children: [
          Expanded(
            child: SettingDetails(
              title: Strings.wifi,
              trailing: _buildWifiToggle(context),
              onBack: state.showAllSettings,
              child: SingleChildScrollView(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    if (savedNetworks.isNotEmpty)
                      Padding(
                        padding:
                            EdgeInsets.symmetric(horizontal: 24, vertical: 16),
                        child: Text(Strings.savedNetworks),
                      ),
                    ListView.builder(
                        shrinkWrap: true,
                        primary: false,
                        itemCount: savedNetworks.length,
                        itemBuilder: (context, index) {
                          final network = savedNetworks[index];
                          final networkName = savedNetworks[index].name;
                          bool currentNetwork =
                              networkName == state.currentNetwork;
                          final networkIcon = savedNetworks[index].icon;
                          final networkCompatible =
                              savedNetworks[index].compatible;
                          final networkHasFailedCredentials =
                              savedNetworks[index].credentialsFailed;
                          // Add 'connnected' or 'credentials failed' subtitle if applicable
                          String? networkSubtitle;
                          if (currentNetwork) {
                            networkSubtitle = Strings.connected;
                          }
                          if (networkHasFailedCredentials) {
                            networkSubtitle = Strings.incorrectPassword;
                          }
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
                                state.setTargetNetwork(savedNetworks[index]),
                            trailing: PopupMenuButton(
                              itemBuilder: (context) {
                                return [
                                  PopupMenuItem(
                                    child: Text(Strings.forget),
                                    value: network,
                                  ),
                                ];
                              },
                              onSelected: state.removeNetwork,
                              tooltip: Strings.forget,
                            ),
                            subtitle: networkSubtitle != null
                                ? Text(networkSubtitle)
                                : null,
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
                            onTap: () => state
                                .setTargetNetwork(availableNetworks[index]),
                            trailing: ((state.targetNetwork.name != '') &&
                                    (state.targetNetwork.name == networkName))
                                ? Icon(Icons.check_outlined)
                                : null,
                          );
                        }),
                  ],
                ),
              ),
            ),
          ),
          _buildNetworkSelection(context),
        ],
      );
    });
  }

  Widget _buildNetworkSelection(BuildContext context) {
    if (state.targetNetwork.name == '') {
      return _buildSelectNetworkPrompt(context);
    } else {
      if (state.targetNetwork.isOpen || state.targetNetwork.isSaved) {
        return _buildNoPasswordNetworkPrompt(context);
      } else {
        return _buildPasswordEntryPrompt(context);
      }
    }
  }

  Widget _buildSelectNetworkPrompt(BuildContext context) {
    return AppBar(
      elevation: 0,
      title: Text(
        Strings.selectNetwork,
        style: Theme.of(context).textTheme.bodyText2,
      ),
      shape: Border(top: BorderSide(color: Theme.of(context).indicatorColor)),
    );
  }

  Widget _buildNoPasswordNetworkPrompt(BuildContext context) {
    return AppBar(
      elevation: 0,
      title: Text(
        Strings.connectToNetwork(state.targetNetwork.name),
        style: Theme.of(context).textTheme.bodyText2,
      ),
      shape: Border(top: BorderSide(color: Theme.of(context).indicatorColor)),
      actions: [
        Padding(
          padding: EdgeInsets.fromLTRB(8, 12, 24, 12),
          child: ElevatedButton(
            onPressed: _connectToNetwork,
            child: Text(Strings.connect),
          ),
        ),
      ],
    );
  }

  void _connectToNetwork() {
    state
      ..connectToNetwork()
      ..clearTargetNetwork();
  }

  Widget _buildPasswordEntryPrompt(BuildContext context) {
    return AppBar(
      elevation: 0,
      title: TextField(
        controller: state.networkPasswordTextController,
        maxLines: 1,
        decoration: InputDecoration(
          border: InputBorder.none,
          hintText: Strings.enterPasswordForNetwork(state.targetNetwork.name),
        ),
      ),
      shape: Border(top: BorderSide(color: Theme.of(context).indicatorColor)),
      actions: [
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
    state
      ..connectToNetwork(textController.text)
      ..clearTargetNetwork();
    textController.clear();
  }

  Widget _buildWifiToggle(BuildContext context) {
    final clientConnectionsFailure =
        state.clientConnectionsEnabled != state.clientConnectionsMonitor;
    final timePassed = state.wifiToggleMillisecondsPassed;

    return Row(
      children: [
        // Show loading indicator if client state mismatch has occured 0.5 - 5 seconds
        if (timePassed >= 500 && timePassed < 5000 && clientConnectionsFailure)
          SizedBox(
            child: CircularProgressIndicator(),
            height: 24,
            width: 24,
          ),
        // Show warning icon if client state mismatch is longer than 5 seconds
        if (timePassed >= 5000 && clientConnectionsFailure)
          _buildWarningIcon(context),
        SizedBox(width: 24),
        Switch(
          value: state.clientConnectionsMonitor,
          onChanged: (clientConnectionsFailure && timePassed <= 5000)
              ? null
              : (value) => state.setClientConnectionsEnabled(enabled: value),
        ),
      ],
    );
  }

  Widget _buildWarningIcon(BuildContext context) {
    String tooltipWarning = '';
    // Case where toggle is off but client state shows enabled
    if (state.clientConnectionsEnabled == true &&
        state.clientConnectionsMonitor == false) {
      tooltipWarning = Strings.failedToTurnOnWifi;
    }
    // Case where toggle is on but client state shows disabled
    if (state.clientConnectionsEnabled == false &&
        state.clientConnectionsMonitor == true) {
      tooltipWarning = Strings.failedToTurnOffWifi;
    }
    return Tooltip(
      message: tooltipWarning,
      child: Icon(
        Icons.warning,
        color: Colors.red[300],
      ),
    );
  }
}
