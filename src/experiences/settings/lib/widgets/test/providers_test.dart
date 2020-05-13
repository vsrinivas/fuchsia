// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lib.widgets/model.dart';
import 'package:mockito/mockito.dart';
import 'package:test/test.dart' as package_test;

const ProviderScope scope1 = ProviderScope('scope1');
const ProviderScope scope2 = ProviderScope('scope2');

void main() {
  Providers providers;

  setUp(() {
    providers = Providers();
  });

  group('Providers', () {
    BuildContext buildContext;

    setUp(() {
      buildContext = MockBuildContext();
    });

    test('can set and retreive a single value', () {
      const value = 'value';
      const otherValue = 'otherValue';

      providers.provideValue(value);
      final provider = providers.getFromType(String);
      expect(provider.get(buildContext), value);

      providers.provideValue(otherValue);
      final otherProvider = providers.getFromType(String);
      expect(otherProvider.get(buildContext), otherValue);
    });
    test('can provide and retreive various kinds of providers', () async {
      final streamController = StreamController<String>.broadcast();
      int functionCounter = 0;
      int factoryCounter = 0;

      providers
        ..provide(Provider.withFactory((buildContext) {
          return factoryCounter++;
        }))
        ..provideAll({
          String: Provider<String>.stream(streamController.stream),
          SampleClass: Provider<SampleClass>.function((buildContext) {
            final value = SampleClass('function $functionCounter');
            functionCounter++;
            return value;
          }),
        })
        ..provide(SampleProvider());

      // Must wait one async cycle for value to propagate.
      streamController.add('stream');
      await Future.delayed(Duration.zero);

      expect(providers.getFromType(String).get(buildContext), 'stream');
      expect(providers.getFromType(String).get(buildContext), 'stream');

      // Must wait one async cycle for value to propagate.
      streamController.add('stream2');
      await Future.delayed(Duration.zero);

      expect(providers.getFromType(String).get(buildContext), 'stream2');

      expect(providers.getFromType(SampleClass).get(buildContext).value,
          'function 0');
      expect(providers.getFromType(SampleClass).get(buildContext).value,
          'function 0');

      expect(providers.getFromType(int).get(buildContext), 0);
      expect(providers.getFromType(int).get(buildContext), 1);

      expect(providers.getFromType(double).get(buildContext), 1.1);

      // Copied providers should have the same providers as original.
      final copiedProviders = Providers()..provideFrom(providers);
      expect(copiedProviders.getFromType(String).get(buildContext), 'stream2');
      expect(copiedProviders.getFromType(SampleClass).get(buildContext).value,
          'function 0');
      expect(copiedProviders.getFromType(int).get(buildContext), 2);
      expect(copiedProviders.getFromType(double).get(buildContext), 1.1);

      await streamController.close();
    });
    test('Throws errors when type incorrect', () {
      // incorrect type
      expect(() => providers.provideAll({String: Provider.value(32)}),
          throwsA(package_test.TypeMatcher<ArgumentError>()));
      // provider type not inferred
      expect(() => providers.provideAll({String: Provider.value('')}),
          throwsA(package_test.TypeMatcher<ArgumentError>()));
    });

    test('can handle multiple scopes', () {
      providers
        ..provideValue(1, scope: scope1)
        ..provide(Provider.value(2), scope: scope2)
        ..provideValue(360);

      expect(providers.getFromType(int).get(buildContext), 360);
      expect(providers.getFromType(int, scope: scope1).get(buildContext), 1);
      expect(providers.getFromType(int, scope: scope2).get(buildContext), 2);

      final other = Providers()..provideFrom(providers);
      expect(other.getFromType(int).get(buildContext), 360);
      expect(other.getFromType(int, scope: scope1).get(buildContext), 1);
      expect(other.getFromType(int, scope: scope2).get(buildContext), 2);

      // overwriting in the same scope
      providers.provideValue(3, scope: scope1);
      expect(providers.getFromType(int, scope: scope1).get(buildContext), 3);

      providers.provideAll(
          {double: Provider<double>.function((buildContext) => 1.0)},
          scope: scope2);

      expect(
          providers.getFromType(double, scope: scope2).get(buildContext), 1.0);
    });
  });

  group('Provide', () {
    FakeModel model;
    SampleClass sampleClass;
    ValueNotifier<String> notifier;
    StreamController<int> broadcastController;
    StreamController<double> singleStreamController;

    setUp(() async {
      model = FakeModel();
      sampleClass = SampleClass('value');
      notifier = ValueNotifier<String>('valueNotifier');
      broadcastController = StreamController<int>.broadcast();
      singleStreamController = StreamController<double>();

      providers
        ..provideValue(model)
        ..provideValue(notifier)
        ..provide(Provider.stream(broadcastController.stream))
        ..provide(Provider.stream(singleStreamController.stream), scope: scope1)
        ..provideValue(sampleClass, scope: scope2)
        // a provider that uses other provided values when accessed
        ..provide(Provider.function((context) => SampleClass(
            Provide.value<SampleClass>(context, scope: scope2).value)));

      broadcastController.add(1);
      singleStreamController.add(1.0);

      // wait for the values to propagate
      await Future.delayed(Duration.zero);
    });

    testWidgets('rebuilds dependent provide widget when providers change',
        (tester) async {
      final intController = StreamController<int>();

      final otherProviders = Providers()
        ..provide(Provider.stream(intController.stream));
      int childBuilds = 0;

      intController.add(2);

      int providedValue;
      final provide = Provide<int>(
        builder: (context, child, value) {
          providedValue = value;
          childBuilds++;
          return Container();
        },
      );

      await tester
          .pumpWidget(ProviderNode(providers: providers, child: provide));

      expect(childBuilds, 1);
      expect(providedValue, 1);

      await tester
          .pumpWidget(ProviderNode(providers: providers, child: provide));

      expect(childBuilds, 1);
      expect(providedValue, 1);

      await tester
          .pumpWidget(ProviderNode(providers: otherProviders, child: provide));

      expect(childBuilds, 2);
      expect(providedValue, 2);

      await intController.close();
    });

    testWidgets('Rebuilds dependent provide.value when providers change',
        (tester) async {
      final otherProviders = Providers()..provideValue<int>(2);
      int childBuilds = 0;

      int providedValue;
      final provide = CallbackWidget(
        (context) {
          providedValue = Provide.value<int>(context);
          childBuilds++;
          return Container();
        },
      );

      await tester
          .pumpWidget(ProviderNode(providers: providers, child: provide));

      expect(childBuilds, 1);
      expect(providedValue, 1);

      await tester
          .pumpWidget(ProviderNode(providers: providers, child: provide));

      expect(childBuilds, 1);
      expect(providedValue, 1);

      await tester
          .pumpWidget(ProviderNode(providers: otherProviders, child: provide));

      expect(childBuilds, 2);
      expect(providedValue, 2);
    });

    testWidgets('can get static values', (tester) async {
      await tester.pumpWidget(ProviderNode(
          providers: providers,
          child: TesterWidget(
              expectedInt: 1,
              expectedDouble: 1.0,
              expectedSampleClass: sampleClass,
              expectedModel: model,
              expectedString: 'valueNotifier')));
    });

    testWidgets('can get listened values', (tester) async {
      bool buildCalled = false;
      int expectedValue = 0;

      final widget = ProviderNode(
        providers: providers,
        child: Provide<FakeModel>(builder: (context, child, value) {
          expect(value.value, expectedValue);
          buildCalled = true;
          return Container();
        }),
      );

      await tester.pumpWidget(widget);
      expect(buildCalled, isTrue);

      buildCalled = false;
      expectedValue++;
      model.increment();

      await tester.pumpAndSettle();
      expect(buildCalled, isTrue);
    });

    testWidgets('can get multi level dependencies', (tester) async {
      bool buildCalled = false;
      String expectedValue = sampleClass.value;

      final widget = ProviderNode(
        providers: providers,
        child: Provide<SampleClass>(builder: (context, child, value) {
          expect(value.value, expectedValue);
          buildCalled = true;
          return Container();
        }),
      );

      await tester.pumpWidget(widget);
      expect(buildCalled, isTrue);
    });

    testWidgets('can get listened streams', (tester) async {
      bool buildCalled = false;
      double expectedValue = 1.0;

      final widget = ProviderNode(
        providers: providers,
        child: Provide<double>(
            scope: scope1,
            builder: (context, child, value) {
              expect(value, expectedValue);
              buildCalled = true;
              return Container();
            }),
      );

      await tester.pumpWidget(widget);
      expect(buildCalled, isTrue);

      buildCalled = false;
      expectedValue = 2.0;
      singleStreamController.add(2.0);
      await tester.pumpAndSettle();
      expect(buildCalled, isTrue);
    });

    testWidgets('can get many listened values', (tester) async {
      bool buildCalled = false;

      var expectedString = 'valueNotifier';
      var expectedInt = 1;
      var expectedDouble = 1.0;

      final widget = ProviderNode(
        providers: providers,
        child: ProvideMulti(
            requestedValues: [
              // This seems to be a bug; can't parse a generic type in an array literal?
              ValueNotifier<String>('').runtimeType,
              int,
            ],
            requestedScopedValues: {
              scope1: [double]
            },
            builder: (context, child, value) {
              expect(value.get<ValueNotifier<String>>().value, expectedString);
              expect(value.get<int>(), expectedInt);
              expect(value.get<double>(scope: scope1), expectedDouble);

              buildCalled = true;
              return Container();
            }),
      );

      await tester.pumpWidget(widget);
      expect(buildCalled, isTrue);

      buildCalled = false;
      expectedString = 'updated';
      notifier.value = 'updated';
      await tester.pumpAndSettle();
      expect(buildCalled, isTrue);

      buildCalled = false;
      expectedInt = 2;
      broadcastController.add(2);
      await tester.pumpAndSettle();
      expect(buildCalled, isTrue);

      buildCalled = false;
      expectedDouble = 2.0;
      singleStreamController.add(2.0);
      await tester.pumpAndSettle();
      expect(buildCalled, isTrue);
    });

    testWidgets('does not rebuild child', (tester) async {
      int childBuilds = 0;
      int builderBuilds = 0;

      final callbackChild = CallbackWidget((_) {
        childBuilds++;
      });

      await tester.pumpWidget(ProviderNode(
          providers: providers,
          child: Provide<FakeModel>(
              builder: (context, child, model) {
                return CallbackWidget((_) {
                  builderBuilds++;
                }, child: child);
              },
              child: callbackChild)));
      expect(childBuilds, 1);
      expect(builderBuilds, 1);

      await tester.pumpAndSettle();
      expect(childBuilds, 1);
      expect(builderBuilds, 1);

      model.increment();
      await tester.pumpAndSettle();
      expect(childBuilds, 1);
      expect(builderBuilds, 2);
    });

    testWidgets('disposes when removed from tree', (tester) async {
      final fakeModel2 = FakeModel2();

      final build = ValueNotifier<bool>(true);

      providers
        ..provide(Provider.function((buildContext) => model, dispose: true))
        ..provide(
            Provider.function((buildContext) => fakeModel2, dispose: false))
        ..provideValue(build);

      expect(model.listenerCount, 0);
      expect(fakeModel2.listenerCount, 0);

      await tester.pumpWidget(ProviderNode(
          providers: providers,
          child: Provide<ValueNotifier<bool>>(
            builder: (context, child, value) => value.value
                ? Provide<FakeModel2>(
                    builder: (ontext, child, model2) =>
                        Provide<FakeModel>(builder: (context, child, model) {
                          return Container();
                        }))
                : Container(),
          )));

      expect(model.listenerCount, 1);
      expect(fakeModel2.listenerCount, 1);

      build.value = false;
      await tester.pumpAndSettle();
      expect(model.listenerCount, 0);
      expect(fakeModel2.listenerCount, 1);
    });

    testWidgets('disposes when all removed from tree', (tester) async {
      final fakeModel2 = FakeModel2();
      final build = ValueNotifier<bool>(true);

      providers
        ..provide(Provider.function((buildContext) => model, dispose: true))
        ..provide(
            Provider.function((buildContext) => fakeModel2, dispose: true))
        ..provideValue(build);

      await tester.pumpWidget(ProviderNode(
          providers: providers,
          child: Provide<ValueNotifier<bool>>(
            builder: (context, child, value) => value.value
                ? ProvideMulti(
                    requestedValues: [FakeModel, FakeModel2],
                    builder: (context, child, model) {
                      return Container();
                    })
                : Container(),
          )));

      expect(model.listenerCount, 1);
      expect(fakeModel2.listenerCount, 1);

      build.value = false;
      await tester.pumpAndSettle();

      expect(model.listenerCount, 0);
      expect(fakeModel2.listenerCount, 0);
    });

    tearDown(() {
      singleStreamController.close();
      broadcastController.close();
    });
  });
}

class MockBuildContext extends Mock implements BuildContext {}

class SampleClass {
  String value;
  SampleClass(this.value);
}

class SampleProvider extends TypedProvider<double> {
  @override
  double get(BuildContext context) => 1.1;
}

class FakeModel extends Model {
  int _value = 0;

  int get value => _value;

  void increment() {
    _value++;
    notifyListeners();
  }
}

class FakeModel2 extends FakeModel {}

class CallbackWidget extends StatelessWidget {
  final void Function(BuildContext) callback;
  final Widget child;

  const CallbackWidget(this.callback, {this.child});

  @override
  Widget build(BuildContext context) {
    callback(context);
    return child ?? Container();
  }
}

class TesterWidget extends StatelessWidget {
  final int expectedInt;
  final double expectedDouble;
  final String expectedString;
  final SampleClass expectedSampleClass;
  final FakeModel expectedModel;

  const TesterWidget(
      {this.expectedDouble,
      this.expectedSampleClass,
      this.expectedInt,
      this.expectedModel,
      this.expectedString});

  @override
  Widget build(BuildContext context) {
    expect(Provide.value<int>(context), expectedInt);
    expect(Provide.value<double>(context, scope: scope1), expectedDouble);
    expect(Provide.value<ValueNotifier<String>>(context).value, expectedString);
    expect(Provide.value<FakeModel>(context), expectedModel);
    expect(Provide.value<SampleClass>(context, scope: scope2),
        expectedSampleClass);

    return Container();
  }
}
