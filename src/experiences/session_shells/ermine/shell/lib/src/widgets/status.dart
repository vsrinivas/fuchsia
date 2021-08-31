// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine/src/states/app_state.dart';
import 'package:flutter/material.dart';
import 'package:flutter_mobx/flutter_mobx.dart';
import 'package:internationalization/strings.dart';

/// Defines a widget to display glanceable information like build verison, ip
/// addresses, battery charge or cpu metrics.
class Status extends StatelessWidget {
  final AppState app;

  const Status(this.app);

  @override
  Widget build(BuildContext context) {
    final settings = app.settingsState;
    return RepaintBoundary(
      child: Container(
        height: 208,
        decoration: BoxDecoration(
          border: Border(
            top: BorderSide(color: Theme.of(context).dividerColor),
          ),
        ),
        padding: EdgeInsets.all(8),
        child: Observer(builder: (_) {
          return Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              // IP Address, Build and Battery.
              Expanded(
                child: Row(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Expanded(
                      child: ListTile(
                        minVerticalPadding: 0,
                        title: Text(Strings.network),
                        subtitle: settings.networkAddresses.isEmpty
                            ? Text('--')
                            : settings.networkAddresses.length == 1
                                ? Text(
                                    settings.networkAddresses.first,
                                    maxLines: 2,
                                    style: TextStyle(
                                      overflow: TextOverflow.ellipsis,
                                    ),
                                  )
                                : Tooltip(
                                    message:
                                        settings.networkAddresses.join('\n'),
                                    child: Text(
                                      settings.networkAddresses.join('\n'),
                                      maxLines: 2,
                                      style: TextStyle(
                                        overflow: TextOverflow.ellipsis,
                                      ),
                                    ),
                                  ),
                      ),
                    ),
                    Expanded(
                      child: ListTile(
                        minVerticalPadding: 0,
                        title: Text(Strings.build),
                        subtitle: Text(
                          app.buildVersion,
                          maxLines: 2,
                          style: TextStyle(overflow: TextOverflow.ellipsis),
                        ),
                      ),
                    ),
                    Expanded(
                      child: ListTile(
                        minVerticalPadding: 0,
                        title: Text(Strings.power),
                        subtitle: Row(
                          children: [
                            if (settings.powerLevel != null) ...[
                              Text('${settings.powerLevel!.toInt()}%'),
                              SizedBox(width: 4),
                            ],
                            Icon(settings.powerIcon),
                          ],
                        ),
                      ),
                    ),
                  ],
                ),
              ),

              // CPU, Memory and Processes.
              Expanded(
                child: Row(
                  crossAxisAlignment: CrossAxisAlignment.baseline,
                  textBaseline: TextBaseline.alphabetic,
                  children: [
                    Expanded(
                      child: ListTile(
                        minVerticalPadding: 0,
                        title: Text(Strings.cpu),
                        subtitle: Text('n/a'),
                      ),
                    ),
                    Expanded(
                      child: ListTile(
                        minVerticalPadding: 0,
                        title: Text(Strings.memory),
                        subtitle: Column(
                          crossAxisAlignment: CrossAxisAlignment.stretch,
                          children: [
                            SizedBox(height: 8),
                            if (settings.memPercentUsed != null) ...[
                              LinearProgressIndicator(
                                value: settings.memPercentUsed,
                              ),
                              SizedBox(height: 8),
                            ],
                            Text(
                              '${settings.memUsed} / ${settings.memTotal}',
                              textAlign: TextAlign.end,
                            ),
                          ],
                        ),
                      ),
                    ),
                    Expanded(
                      child: ListTile(
                        minVerticalPadding: 0,
                        title: Text(Strings.processes),
                        subtitle: Text('n/a'),
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
}
