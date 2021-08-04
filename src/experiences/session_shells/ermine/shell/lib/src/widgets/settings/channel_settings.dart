// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine/src/states/settings_state.dart';
import 'package:ermine/src/widgets/settings/setting_details.dart';
import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';
import 'package:internationalization/strings.dart';

/// Defines a widget to list all channels in [SettingDetails] widget.
class ChannelSettings extends StatelessWidget {
  final SettingsState state;
  final ValueChanged<String> onChange;

  const ChannelSettings({required this.state, required this.onChange});

  @override
  Widget build(BuildContext context) {
    return Observer(builder: (_) {
      final channels = state.availableChannels.value;
      final targetChannel = state.targetChannel.value == ''
          ? state.currentChannel.value
          : state.targetChannel.value;
      return Column(
        children: [
          Expanded(
            child: SettingDetails(
              title: Strings.channel,
              onBack: state.showAllSettings,
              child: ListView.builder(
                itemCount: channels.length,
                itemBuilder: (context, index) {
                  final channel = channels[index];
                  return ListTile(
                    title: Text(channel),
                    subtitle: index == 0 ? Text(Strings.currentChannel) : null,
                    onTap: () => onChange(channels[index]),
                  );
                },
              ),
            ),
          ),
          AppBar(
            elevation: 0,
            title: Text(
              '${Strings.downloadAndApplyUpdatesOf} $targetChannel ${Strings.channel.toLowerCase()}',
              style: Theme.of(context).textTheme.bodyText2,
            ),
            shape: Border(
                top: BorderSide(color: Theme.of(context).indicatorColor)),
            // TODO(fxb/79588): Add button to start update
          ),
        ],
      );
    });
  }
}
