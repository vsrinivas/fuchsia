// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:apps.modular.lib.app.dart/app.dart';
import 'package:apps.modular.services.application/service_provider.fidl.dart';
import 'package:apps.modular.services.story/link.fidl.dart';
import 'package:apps.modular.services.story/module.fidl.dart';
import 'package:apps.modular.services.story/story.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';

import 'package:flutter/material.dart';
import 'package:flutter/http.dart' as http;

final ApplicationContext _appContext =
    new ApplicationContext.fromStartupInfo();

ModuleImpl _moduleImpl;

void _log(String msg) {
  print('[HTTPS Flutter Example] $msg');
}

/// An implementation of the [Module] interface.
class ModuleImpl extends Module {
  final ModuleBinding _binding = new ModuleBinding();

  /// Bind an [InterfaceRequest] for a [Module] interface to this object.
  void bind(InterfaceRequest<Module> request) {
    _binding.bind(this, request);
  }

  /// Implementation of the Initialize(Story story, Link link) method.
  @override
  void initialize(
      InterfaceHandle<Story> storyHandle,
      InterfaceHandle<Link> linkHandle,
      InterfaceHandle<ServiceProvider> incomingServices,
      InterfaceRequest<ServiceProvider> outgoingServices) {
    _log('ModuleImpl::initialize call');
  }

  /// Implementation of the Stop() => (); method.
  @override
  void stop(void callback()) {
    _log('ModuleImpl::stop call');

    // Do some clean up here.

    // Invoke the callback to signal that the clean-up process is done.
    callback();
  }
}

/// Entry point for this module.
void main() {
  _log('Module started with ApplicationContext: $_appContext');

  /// Add [ModuleImpl] to this application's outgoing ServiceProvider.
  _appContext.outgoingServices.addServiceForName(
    (request) {
      _log('Received binding request for Module');
      _moduleImpl = new ModuleImpl();
      _moduleImpl.bind(request);
    },
    Module.serviceName,
  );

  runApp(new MaterialApp(
    title: 'HTTPS Example',
    home: new Home(),
  ));
}

class Home extends StatefulWidget {
  @override
  _HomeState createState() => new _HomeState();
}

enum Status {
  LOADING,
  SUCCESS,
  ERROR,
}

const String url = 'https://www.google.com/';

class _HomeState extends State<Home> {
  Status status = Status.LOADING;
  Exception exception;
  Error error;
  http.Response res;

  Future<Null> _request() async {
    res = null;
    error = null;
    exception = null;

    try {
      _log('requesting: $url');
      res = await http.get(url);
      status = Status.SUCCESS;
    } catch (e) {
      _log('HTTPS request errored: $e');
      status = Status.ERROR;
      if (e is Exception) {
        exception = e;
      } else if (e is Error) {
        error = e;
      } else {
        throw e;
      }
    }

    setState(() {});
  }

  @override
  void initState() {
    _request();
    super.initState();
  }

  @override
  Widget build(BuildContext context) {
    AppBar appBar = new AppBar(title: new Text('HTTPS Example'));
    Widget child;

    switch (status) {
      case Status.LOADING:
        return new Scaffold(
            appBar: appBar,
            body: new Center(
                child: new Padding(
                    padding: const EdgeInsets.symmetric(horizontal: 16.0),
                    child: new Text('Loading...'))));
        break;
      case Status.SUCCESS:
        child = new Center(
            child: new Padding(
                padding: const EdgeInsets.symmetric(horizontal: 16.0),
                child: new Text('Success: $url - ${res.statusCode}')));
        break;
      case Status.ERROR:
        if (exception != null) {
          child = new Center(
              child: new Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 16.0),
                  child: new Text('Exception => $exception')));
        } else if (error != null) {
          child = new Center(
              child: new Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 16.0),
                  child: new Text('Error => ${error.stackTrace}')));
        }
        break;
    }

    return new Scaffold(
      appBar: new AppBar(title: new Text('HTTPS Example')),
      body: child,
      floatingActionButton: new FloatingActionButton(
        child: new Icon(Icons.refresh),
        onPressed: _request,
      ),
    );
  }
}
