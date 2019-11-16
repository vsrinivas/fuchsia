// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';

import '../trace_model.dart';

// This file contains common utility functions that are shared across metrics.

T _add<T extends num>(T a, T b) => a + b;

/// Get all events contained in [model], without regard for what [Process] or
/// [Thread] they belong to.
Iterable<Event> getAllEvents(Model model) => model.processes
    .expand((process) => process.threads.expand((thread) => thread.events));

/// Filter [events] for events that have a matching [category] and [name]
/// fields.
Iterable<Event> filterEvents(Iterable<Event> events,
        {String category, String name}) =>
    events.where((event) =>
        (category == null || event.category == category) &&
        (name == null || event.name == name));

/// Filter [events] for events that have a matching [category] and [name]
/// fields, that are also of type [T].
Iterable<T> filterEventsTyped<T extends Event>(Iterable<Event> events,
        {String category, String name}) =>
    Iterable.castFrom<Event, T>(events.where((event) =>
        (event is T) &&
        (category == null || event.category == category) &&
        (name == null || event.name == name)));

/// Compute the mean (https://en.wikipedia.org/wiki/Arithmetic_mean#Definition)
/// of [values].
double computeMean<T extends num>(Iterable<T> values) {
  if (values.isEmpty) {
    throw ArgumentError(
        '[values] must not be empty in order to compute its average');
  }
  return values.reduce(_add) / values.length;
}

/// Compute the population variance (https://en.wikipedia.org/wiki/Variance#Population_variance)
/// of [values].
double computeVariance<T extends num>(Iterable<T> values) {
  final mean = computeMean(values);
  return values.map((value) => pow(value - mean, 2.0)).reduce(_add) /
      values.length;
}

/// Compute the population standard deviation (https://en.wikipedia.org/wiki/Standard_deviation#Uncorrected_sample_standard_deviation)
/// of [values].
double computeStandardDeviation<T extends num>(Iterable<T> values) =>
    sqrt(computeVariance(values));

/// Compute the [percentile]th percentile (https://en.wikipedia.org/wiki/Percentile)
/// of [values].
double computePercentile<T extends num>(Iterable<T> values, int percentile) {
  if (values.isEmpty) {
    throw ArgumentError(
        '[values] must not be empty in order to compute percentile');
  }
  final valuesAsList = values.toList()..sort();
  if (percentile == 100) {
    return valuesAsList.last.toDouble();
  }
  final indexAsFloat = (valuesAsList.length - 1.0) * (0.01 * percentile);
  final index = indexAsFloat.floor();
  final fractional = indexAsFloat % 1.0;
  if (index + 1 == valuesAsList.length) {
    return valuesAsList.last.toDouble();
  }
  return valuesAsList[index] * (1.0 - fractional) +
      valuesAsList[index + 1] * fractional;
}

/// Compute the maximum (https://en.wikipedia.org/wiki/Sample_maximum_and_minimum)
/// of [values].
T computeMax<T extends num>(Iterable<T> values) => values.reduce(max);

/// Compute the minimum (# https://en.wikipedia.org/wiki/Sample_maximum_and_minimum)
/// of [values].
T computeMin<T extends num>(Iterable<T> values) => values.reduce(min);

/// Compute the 1st degree difference of [values].
///
/// I.e., [x0, x1, x2, ...] -> [(x1 - x0), (x2 - x1), ...]
List<T> differenceValues<T extends num>(Iterable<T> values) {
  final List<T> result = [];
  T previous;
  for (final value in values) {
    if (previous != null) {
      final difference = value - previous;
      result.add(difference);
    }
    previous = value;
  }
  return result;
}
