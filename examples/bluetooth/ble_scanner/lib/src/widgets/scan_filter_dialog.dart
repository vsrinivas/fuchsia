// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:lib.bluetooth.fidl/common.fidl.dart';
import 'package:lib.bluetooth.fidl/low_energy.fidl.dart' as ble;
import 'package:flutter/material.dart';
import 'package:meta/meta.dart';

import '../manufacturer_names.dart';
import '../modular/module_model.dart';

/// Enum values to pass to Navigator.push()
enum DismissDialogAction {
  /// Cancel the scan filter
  cancel,

  /// Apply the scan filter
  save,
}

/// Widget that contains entry fields to build a BLE scan filter
class ScanFilterDialog extends StatefulWidget {
  final BLEScannerModuleModel moduleModel;

  ScanFilterDialog({Key key, @required this.moduleModel}) : super(key: key);

  @override
  _ScanFilterDialogState createState() => new _ScanFilterDialogState();
}

class _ScanFilterDialogState extends State<ScanFilterDialog> {
  int _manufacturerId;
  String _nameSubstring;
  bool _connectable;

  bool _saveNeeded = false;

  final GlobalKey<FormState> _formKey = new GlobalKey<FormState>();

  Future<bool> _onWillPop() async {
    if (!_saveNeeded) return true;

    final ThemeData theme = Theme.of(context);
    final TextStyle dialogTextStyle =
        theme.textTheme.subhead.copyWith(color: theme.textTheme.caption.color);

    return await showDialog<bool>(
            context: context,
            child: new AlertDialog(
                content: new Text('Discard filters?', style: dialogTextStyle),
                actions: <Widget>[
                  new FlatButton(
                      child: const Text('CANCEL'),
                      onPressed: () {
                        Navigator.of(context).pop(false);
                      }),
                  new FlatButton(
                      child: const Text('DISCARD'),
                      onPressed: () {
                        Navigator.of(context).pop(true);
                      })
                ])) ??
        false;
  }

  void _handleConnectableChanged(bool newValue) {
    if (_connectable != newValue) {
      setState(() {
        _saveNeeded = true;
        _connectable = newValue;
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    final ThemeData theme = Theme.of(context);
    return new Scaffold(
        appBar: new AppBar(title: const Text('Scan Filters'), actions: <Widget>[
          new FlatButton(
              child: new Text('SAVE',
                  style: theme.textTheme.body1.copyWith(color: Colors.white)),
              onPressed: () {
                _formKey.currentState.save();

                ble.ScanFilter filter = new ble.ScanFilter();

                if (_manufacturerId != null) {
                  filter.manufacturerIdentifier = new UInt16()
                    ..value = _manufacturerId;
                }

                if (_nameSubstring?.isNotEmpty ?? false) {
                  filter.nameSubstring = _nameSubstring;
                }

                if (_connectable != null) {
                  filter.connectable = new Bool()..value = _connectable;
                }

                widget.moduleModel.scanFilter = filter;
                Navigator.pop(context, DismissDialogAction.save);
              })
        ]),
        body: new Form(
            key: _formKey,
            onWillPop: _onWillPop,
            child: new Container(
                padding: const EdgeInsets.all(36.0),
                child: new Column(
                    children: <Widget>[
                  new ListTile(
                      title: const Text('Connectable'),
                      trailing: new Container(
                          width: 500.0,
                          child: new Column(
                              children: <Widget>[
                            new RadioListTile<bool>(
                                title: const Text('All'),
                                value: null,
                                groupValue: _connectable,
                                onChanged: _handleConnectableChanged),
                            new RadioListTile<bool>(
                                title: const Text('Connectable devices only'),
                                value: true,
                                groupValue: _connectable,
                                onChanged: _handleConnectableChanged),
                            new RadioListTile<bool>(
                                title:
                                    const Text('Non-connectable devices only'),
                                value: false,
                                groupValue: _connectable,
                                onChanged: _handleConnectableChanged),
                          ]
                                  .map((Widget child) =>
                                      new Expanded(child: child))
                                  .toList()))),
                  new ListTile(
                      title: const Text('Manufacturer ID'),
                      trailing: new Container(
                          width: 500.0,
                          child: new DropdownButton<int>(
                              value: _manufacturerId,
                              onChanged: (int newValue) {
                                if (newValue != _manufacturerId) {
                                  setState(() {
                                    _saveNeeded = true;
                                    _manufacturerId = newValue;
                                  });
                                }
                              },
                              items:
                                  <int>[null, 0x00E0, 0x004C].map((int value) {
                                return new DropdownMenuItem<int>(
                                  value: value,
                                  child: new Text(value == null
                                      ? 'None'
                                      : getManufacturerName(value)),
                                );
                              }).toList()))),
                  new ListTile(
                      title: const Text('Name Substring'),
                      trailing: new Container(
                          width: 500.0,
                          child: new TextFormField(onSaved: (String newValue) {
                            if (newValue != _nameSubstring) {
                              setState(() {
                                _saveNeeded = true;
                                _nameSubstring = newValue;
                              });
                            }
                          })))
                ]
                        .map((Widget child) => new Container(
                            margin: const EdgeInsets.symmetric(vertical: 16.0),
                            child: child))
                        .toList()))));
  }
}
