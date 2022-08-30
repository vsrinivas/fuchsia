// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(http://fxbug.dev/107480): Resolve lint issues and reenable analysis for file
// ignore_for_file: deprecated_member_use

import 'package:ermine/src/states/settings_state.dart';
import 'package:ermine/src/widgets/settings/setting_details.dart';
import 'package:ermine_utils/ermine_utils.dart';
import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';
import 'package:internationalization/strings.dart';

/// Defines a widget to list all channels in [SettingDetails] widget.
class ChannelSettings extends StatelessWidget {
  final SettingsState state;
  final VoidCallback updateAlert;
  final ValueChanged<String> onChange;

  const ChannelSettings(
      {required this.state, required this.onChange, required this.updateAlert});

  @override
  Widget build(BuildContext context) {
    return Observer(builder: (_) {
      final channels = state.availableChannels;
      bool idleState = state.channelState == ChannelState.idle;
      return Column(
        children: [
          Expanded(
            child: SettingDetails(
              title: Strings.channel,
              onBack: state.showAllSettings,
              child: ListView.builder(
                physics:
                    idleState ? null : const NeverScrollableScrollPhysics(),
                itemCount: channels.length,
                itemBuilder: (context, index) {
                  final channel = channels[index];
                  return ListTile(
                    title: Text(
                      channel,
                      style: idleState
                          ? null
                          : TextStyle(
                              color: Theme.of(context).disabledColor,
                            ),
                    ),
                    subtitle: channel == state.currentChannel
                        ? Text(
                            Strings.currentChannel,
                            style: idleState
                                ? null
                                : TextStyle(
                                    color: Theme.of(context).disabledColor,
                                  ),
                          )
                        : null,
                    onTap: idleState ? () => onChange(channels[index]) : null,
                    trailing: ((state.targetChannel != '') &&
                            (state.targetChannel == channel))
                        ? Icon(
                            Icons.check_outlined,
                            color: idleState
                                ? null
                                : Theme.of(context).disabledColor,
                          )
                        : null,
                  );
                },
              ),
            ),
          ),
          _buildUpdateProgress(context),
        ],
      );
    });
  }

  Widget _buildUpdateProgress(BuildContext context) {
    switch (state.channelState) {
      case ChannelState.checkingForUpdates:
        return _buildCheckingForUpdates(context);
      case ChannelState.errorCheckingForUpdate:
        return _buildErrorCheckingForUpdate(context);
      case ChannelState.noUpdateAvailable:
        return _buildNoUpdateAvailable(context);
      case ChannelState.installationDeferredByPolicy:
        return _buildInstallationDeferredByPolicy(context);
      case ChannelState.installingUpdate:
        return _buildInstallingUpdate(context);
      case ChannelState.waitingForReboot:
        return _buildWaitingForReboot(context);
      case ChannelState.installationError:
        return _buildInstallationError(context);
      default:
        return _buildIdle(context);
    }
  }

  Widget _buildIdle(BuildContext context) {
    final targetMessage = state.targetChannel == ''
        ? Strings.selectAnUpdateChannel
        : Strings.downloadTargetChannel(state.targetChannel);
    return AppBar(
      elevation: 0,
      title: Text(
        targetMessage,
        style: Theme.of(context).textTheme.bodyText2,
      ),
      shape: Border(top: BorderSide(color: Theme.of(context).indicatorColor)),
      actions: [
        Padding(
          padding: EdgeInsets.fromLTRB(8, 12, 24, 12),
          child: ElevatedButton(
            style: ErmineButtonStyle.elevatedButton(Theme.of(context)),
            onPressed: state.targetChannel == '' ? null : updateAlert,
            child: Text(Strings.update.toUpperCase()),
          ),
        ),
      ],
    );
  }

  Widget _buildCheckingForUpdates(BuildContext context) {
    return AppBar(
      elevation: 0,
      title: Text(
        Strings.checkingForUpdate,
        style: Theme.of(context).textTheme.bodyText2,
      ),
      shape: Border(top: BorderSide(color: Theme.of(context).indicatorColor)),
      bottom: PreferredSize(
        preferredSize: Size(double.infinity, 1.0),
        child: Padding(
          padding: EdgeInsets.fromLTRB(18, 0, 18, 18),
          child: LinearProgressIndicator(),
        ),
      ),
    );
  }

  Widget _buildErrorCheckingForUpdate(BuildContext context) {
    return AppBar(
      elevation: 0,
      title: Text(
        Strings.errorCheckingForUpdate,
        style: Theme.of(context).textTheme.bodyText2,
      ),
      shape: Border(top: BorderSide(color: Theme.of(context).indicatorColor)),
    );
  }

  Widget _buildNoUpdateAvailable(BuildContext context) {
    // TODO(fxb/79588): Add button to return to selection state
    return AppBar(
      elevation: 0,
      title: ListTile(
        title: Text(Strings.noUpdateAvailableTitle),
        subtitle: Text(Strings.noUpdateAvailableSubtitle),
      ),
      shape: Border(top: BorderSide(color: Theme.of(context).indicatorColor)),
    );
  }

  Widget _buildInstallationDeferredByPolicy(BuildContext context) {
    return AppBar(
      elevation: 0,
      title: ListTile(
        title: Text(Strings.installationDeferredByPolicyTitle),
        subtitle: Text(Strings.installationDeferredByPolicyBody),
      ),
      shape: Border(top: BorderSide(color: Theme.of(context).indicatorColor)),
    );
  }

  Widget _buildInstallingUpdate(BuildContext context) {
    int progress = state.systemUpdateProgress != 0
        ? (state.systemUpdateProgress * 100).floor()
        : 0;
    return AppBar(
      elevation: 0,
      title: Text(
        Strings.updating(progress),
        style: Theme.of(context).textTheme.bodyText2,
      ),
      shape: Border(top: BorderSide(color: Theme.of(context).indicatorColor)),
      bottom: PreferredSize(
        preferredSize: Size(double.infinity, 1.0),
        child: Padding(
          padding: EdgeInsets.fromLTRB(18, 0, 18, 18),
          child: LinearProgressIndicator(value: state.systemUpdateProgress),
        ),
      ),
    );
  }

  Widget _buildWaitingForReboot(BuildContext context) {
    return AppBar(
      elevation: 0,
      title: Text(
        Strings.waitingForReboot,
        style: Theme.of(context).textTheme.bodyText2,
      ),
      shape: Border(top: BorderSide(color: Theme.of(context).indicatorColor)),
      bottom: PreferredSize(
        preferredSize: Size(double.infinity, 1.0),
        child: Padding(
          padding: EdgeInsets.fromLTRB(18, 0, 18, 18),
          child: LinearProgressIndicator(),
        ),
      ),
    );
  }

  Widget _buildInstallationError(BuildContext context) {
    return AppBar(
      elevation: 0,
      title: ListTile(
        title: Text(Strings.installationErrorTitle),
        subtitle: Text(Strings.installationErrorBody),
      ),
      shape: Border(top: BorderSide(color: Theme.of(context).indicatorColor)),
    );
  }
}
