// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is an L10N developer experience study fragment.
// Let's see how it works when you offload all strings to a file.

// The Intl.message texts have been offloaded to a separate file so as to
// minimize the amount of code needed to be exported for localization.
// While the individual string messages have been left in their original form,
// the case-ness of the text is part of the presentation so should probably be
// handled in the view code.

import 'package:intl/intl.dart';

String brightness() => Intl.message(
      'Brightness',
      name: 'brightness',
      desc: 'The label for the screen brightness settings widget',
    );

String cancel() => Intl.message(
      'Cancel',
      name: 'cancel',
      desc: 'The label for canceling an offered command',
    );

String done() => Intl.message(
      'Done',
      name: 'done',
      desc: 'The label for saying we are done with a command',
    );

String sleep() => Intl.message(
      'Sleep',
      name: 'sleep',
      desc: 'The label for the sleep settings widget',
    );

String restart() => Intl.message(
      'Restart',
      name: 'restart',
      desc: 'The label for the restart button',
    );

String powerOff() => Intl.message(
      'Power Off',
      name: 'powerOff',
      desc: 'The label for the restart button',
    );

String settings() => Intl.message(
      'Settings',
      name: 'settings',
      desc: 'The label for the settings button',
    );

String volume() => Intl.message(
      'Volume',
      name: 'volume',
      desc: 'The label for the settings button',
    );

String min() => Intl.message(
      'Min',
      name: 'min',
      desc:
          'The short label for "minimal" label for the volume button',
    );

String max() => Intl.message(
      'Max',
      name: 'max',
      desc:
          'The shortened label for "maximal" label for the volume button',
    );

String music() => Intl.message(
      'Music',
      name: 'music',
      desc: 'The label used for the music button',
    );

String back() => Intl.message(
      'Back',
      name: 'back',
      desc: 'The label for the "Back" button',
    );

String pause() => Intl.message(
      'Pause',
      name: 'pause',
      desc: 'The label for the "Pause" button',
    );

String skip() => Intl.message(
      'Skip',
      name: 'skip',
      desc: 'The label for the "Skip" button',
    );

String topProcesses() => Intl.message(
      'Top Processes',
      name: 'topProcesses',
      desc: 'The label for the "Top processes" label',
    );

String shutdown() => Intl.message(
      'Shutdown',
      name: 'shutdown',
      desc: 'The label for the "System shutdown" label',
    );

String memory() => Intl.message(
      'Memory',
      name: 'memory',
      desc: 'The label for the "memory" label',
    );

String cpu() => Intl.message(
      'CPU',
      name: 'cpu',
      desc: 'The short name for the "Central Processing Unit" label',
    );

String mem() => Intl.message(
      'MEM',
      name: 'mem',
      desc: 'The short name for the "Memory" label',
    );

String ide() => Intl.message(
      'IDE',
      name: 'ide',
      desc: 'The short name for the "Integrated Development Environment" label',
    );

String chrome() => Intl.message(
      'Chrome',
      name: 'chrome',
      desc: 'The short name for the "Google Chrome" browser',
    );


String pid() => Intl.message(
      'PID',
      name: 'pid',
      desc: 'The short name for the "Process identifier" label',
    );


String tasks() => Intl.message(
      'TASKS',
      name: 'tasks',
      desc: 'The short name for the "Tasks" label',
    );

String weather() => Intl.message(
      'Weather',
      name: 'weather',
      desc: 'The short name for the "Weather" label',
    );

String date() => Intl.message(
      'Date',
      name: 'date',
      desc: 'The short name for the "date" label',
    );

String network() => Intl.message(
      'Network',
      name: 'network',
      desc: 'The short name for the "network" label',
    );

String fps() => Intl.message(
      'FPS',
      name: 'fps',
      desc: 'The short name for the "Frames per Second" label',
    );

String batt() => Intl.message(
      'Batt',
      name: 'batt',
      desc: 'The short name for the "Battery level" label',
    );

String battery() => Intl.message(
      'Battery',
      name: 'battery',
      desc: 'The long name for the "Battery level" label',
    );

String wireless() => Intl.message(
      'Wireless',
      name: 'wireless',
      desc: 'The short name for the "Wireless network" label',
    );

String signalStrong() => Intl.message(
      'Strong Signal',
      name: 'signalStrong',
      desc: 'The short name for the "Strong signal" label',
    );

String sunny() => Intl.message(
      'Sunny',
      name: 'sunny',
      desc: 'The short name for the "Weather is sunny" label',
    );

String runningTasks(int numTasks) => Intl.plural(
      numTasks,
      zero: '$numTasks RUNNING',
      one: '$numTasks RUNNING',
      other: '$numTasks RUNNING',
      name: 'runningTasks',
      args: [numTasks],
      desc: 'How many tasks are currently running',
      examples: const {'numTasks': 42},
    );

String totalTasks(int numTasks) => Intl.plural(
      numTasks,
      zero: '$numTasks',
      one: '$numTasks',
      other: '$numTasks',
      name: 'totalTasks',
      args: [numTasks],
      desc: 'How many tasks are currently running',
      examples: const {'numTasks': 42},
    );

String numThreads(int numThreads) => Intl.plural(
      numThreads,
      zero: '$numThreads THR',
      one: '$numThreads THR',
      other: '$numThreads THR',
      name: 'numThreads',
      args: [numThreads],
      desc: 'How many threads are currently there, short label',
      examples: const {'numThreads': 42},
    );

String name() => Intl.message(
      'Name',
      name: 'name',
      desc: 'A generic label for a name, sentence case.',
    );

String ask() => Intl.message(
      'ASK',
      name: 'ask',
      desc: 'A generic short label for "query", sentence case.',
    );

String typeToAsk() => Intl.message(
      'TYPE TO ASK',
      name: 'typeToAsk',
      desc: 'Shown in the ask text entry box in Ermine shell',
    );

String overview() => Intl.message(
      'Overview',
      name: 'overview',
      desc: 'Shown in top bar on the Ermine shell',
    );

String recents() => Intl.message(
      'Recents',
      name: 'recents',
      desc: 'A list of recent apps',
    );

String browser() => Intl.message(
      'Browser',
      name: 'browser',
      desc: 'A button to invoke a browser with',
    );

String auto() => Intl.message(
      'Auto',
      name: 'auto',
      desc: 'A shorthand for "automatic" setting.',
    );

String mockWirelessNetwork() => Intl.message(
      'Wireless_Network',
      name: 'mockWirelessNetwork',
      desc: 'A mock label for a wireless network name that the device is connected to',
    );

String nameThisStory() => Intl.message(
      'Name this story',
      name: 'nameThisStory',
      desc: 'A hint appearing in a window for naming a story. This text should not include padding, but now it does.',
    );
