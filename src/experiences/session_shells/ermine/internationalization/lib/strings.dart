// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is an L10N developer experience study fragment.
// Let's see how it works when you offload all strings to a file.

// The Intl.message texts have been offloaded to a separate file so as to
// minimize the amount of code needed to be exported for localization.
// While the individual static String get messages have been left in their original form,
// the case-ness of the text is part of the presentation so should probably be
// handled in the view code.

import 'package:intl/intl.dart';

/// Provides access to localized strings used in Ermine code.
class Strings {
  static final Strings instance = Strings._internal();
  factory Strings() => instance;
  Strings._internal();

  static String get brightness => Intl.message(
        'Brightness',
        name: 'brightness',
        desc: 'The label for the screen brightness settings widget',
      );

  static String get cancel => Intl.message(
        'Cancel',
        name: 'cancel',
        desc: 'The label for canceling an offered command',
      );

  static String get done => Intl.message(
        'Done',
        name: 'done',
        desc: 'The label for saying we are done with a command',
      );

  static String get selected => Intl.message(
        'Selected',
        name: 'selected',
        desc: 'The label for saying an option is currently selected',
      );

  static String get darkMode => Intl.message(
        'Dark Mode',
        name: 'dark mode',
        desc: 'The label for dark mode toggle switch',
      );

  static String get sleep => Intl.message(
        'Sleep',
        name: 'sleep',
        desc: 'The label for the sleep settings widget',
      );

  static String get restart => Intl.message(
        'Restart',
        name: 'restart',
        desc: 'The label for the restart button',
      );

  static String get powerOff => Intl.message(
        'Power Off',
        name: 'powerOff',
        desc: 'The label for the restart button',
      );

  static String get settings => Intl.message(
        'Settings',
        name: 'settings',
        desc: 'The label for the settings button',
      );

  static String get volume => Intl.message(
        'Volume',
        name: 'volume',
        desc: 'The label for the settings button',
      );

  static String get min => Intl.message(
        'Min',
        name: 'min',
        desc: 'The short label for "minimal" label for the volume button',
      );

  static String get max => Intl.message(
        'Max',
        name: 'max',
        desc: 'The shortened label for "maximal" label for the volume button',
      );

  static String get music => Intl.message(
        'Music',
        name: 'music',
        desc: 'The label used for the music button',
      );

  static String get back => Intl.message(
        'Back',
        name: 'back',
        desc: 'The label for the "Back" button',
      );

  static String get pause => Intl.message(
        'Pause',
        name: 'pause',
        desc: 'The label for the "Pause" button',
      );

  static String get skip => Intl.message(
        'Skip',
        name: 'skip',
        desc: 'The label for the "Skip" button',
      );

  static String get topProcesses => Intl.message(
        'Top Processes',
        name: 'topProcesses',
        desc: 'The label for the "Top processes" label',
      );

  static String get processes => Intl.message(
        'Processes',
        name: 'processes',
        desc: 'The label for the "Processes" label',
      );

  static String get shutdown => Intl.message(
        'Shutdown',
        name: 'shutdown',
        desc: 'The label for the "System shutdown" label',
      );

  static String get memory => Intl.message(
        'Memory',
        name: 'memory',
        desc: 'The label for the "memory" label',
      );

  static String get cpu => Intl.message(
        'CPU',
        name: 'cpu',
        desc: 'The short name for the "Central Processing Unit" label',
      );

  static String get mem => Intl.message(
        'MEM',
        name: 'mem',
        desc: 'The short name for the "Memory" label',
      );

  static String get ide => Intl.message(
        'IDE',
        name: 'ide',
        desc:
            'The short name for the "Integrated Development Environment" label',
      );

  static String get chrome => Intl.message(
        'Chrome',
        name: 'chrome',
        desc: 'The short name for the "Google Chrome" browser',
      );

  static String get pid => Intl.message(
        'PID',
        name: 'pid',
        desc: 'The short name for the "Process identifier" label',
      );

  static String get tasks => Intl.message(
        'TASKS',
        name: 'tasks',
        desc: 'The short name for the "Tasks" label',
      );

  static String get weather => Intl.message(
        'Weather',
        name: 'weather',
        desc: 'The short name for the "Weather" label',
      );

  static String get unit => Intl.message(
        'Unit',
        name: 'unit',
        desc: 'The short name for the unit of measurement',
      );

  static String get date => Intl.message(
        'Date',
        name: 'date',
        desc: 'The short name for the "date" label',
      );

  static String get dateTime => Intl.message(
        'Date & Time',
        name: 'dateTime',
        desc: 'The short name for the "date & time" label',
      );

  static String get timezone => Intl.message(
        'Timezone',
        name: 'timezone',
        desc: 'The short name for the "timezone" label',
      );

  static String get network => Intl.message(
        'Network',
        name: 'network',
        desc: 'The short name for the "network" label',
      );

  static String get fps => Intl.message(
        'FPS',
        name: 'fps',
        desc: 'The short name for the "Frames per Second" label',
      );

  static String get batt => Intl.message(
        'Batt',
        name: 'batt',
        desc: 'The short name for the "Battery level" label',
      );

  static String get battery => Intl.message(
        'Battery',
        name: 'battery',
        desc: 'The long name for the "Battery level" label',
      );

  static String get power => Intl.message(
        'Power',
        name: 'power',
        desc: 'The short name for the system source of "Power" label',
      );

  static String get wireless => Intl.message(
        'Wireless',
        name: 'wireless',
        desc: 'The short name for the "Wireless network" label',
      );

  static String get wifi => Intl.message(
        'Wi-Fi',
        name: 'wi-fi',
        desc: 'The short name for the "Wi-Fi network" label',
      );

  static String get signalStrong => Intl.message(
        'Strong Signal',
        name: 'signalStrong',
        desc: 'The short name for the "Strong signal" label',
      );

  static String get sunny => Intl.message(
        'Sunny',
        name: 'sunny',
        desc: 'The short name for the "Weather is sunny" label',
      );

  static String runningTasks(int numTasks) => Intl.plural(
        numTasks,
        zero: '$numTasks RUNNING',
        one: '$numTasks RUNNING',
        other: '$numTasks RUNNING',
        name: 'runningTasks',
        args: [numTasks],
        desc: 'How many tasks are currently running',
        examples: const {'numTasks': 42},
      );

  static String totalTasks(int numTasks) => Intl.plural(
        numTasks,
        zero: '$numTasks',
        one: '$numTasks',
        other: '$numTasks',
        name: 'totalTasks',
        args: [numTasks],
        desc: 'How many tasks are currently running',
        examples: const {'numTasks': 42},
      );

  static String numThreads(int numThreads) => Intl.plural(
        numThreads,
        zero: '$numThreads THR',
        one: '$numThreads THR',
        other: '$numThreads THR',
        name: 'numThreads',
        args: [numThreads],
        desc: 'How many threads are currently there, short label',
        examples: const {'numThreads': 42},
      );

  static String openPackage(String packageName) => Intl.message(
        'open $packageName',
        name: 'openPackage',
        desc: 'Open an application with supplied package name',
        args: [packageName],
        examples: const {'packageName': 'simple_browser'},
      );

  static String get name => Intl.message(
        'Name',
        name: 'name',
        desc: 'A generic label for a name, sentence case.',
      );

  static String get ask => Intl.message(
        'ASK',
        name: 'ask',
        desc: 'A generic short label for "query", sentence case.',
      );

  static String get typeToAsk => Intl.message(
        'TYPE TO ASK',
        name: 'typeToAsk',
        desc: 'Shown in the ask text entry box in Ermine shell',
      );

  static String get overview => Intl.message(
        'Overview',
        name: 'overview',
        desc: 'Shown in top bar on the Ermine shell',
      );

  static String get recents => Intl.message(
        'Recents',
        name: 'recents',
        desc: 'A list of recent apps',
      );

  static String get browser => Intl.message(
        'Browser',
        name: 'browser',
        desc: 'A button to invoke a browser with',
      );

  static String get auto => Intl.message(
        'Auto',
        name: 'auto',
        desc: 'A shorthand for "automatic" setting.',
      );

  static String get mockWirelessNetwork => Intl.message(
        'Wireless_Network',
        name: 'mockWirelessNetwork',
        desc:
            'A mock label for a wireless network name that the device is connected to',
      );

  static String get nameThisStory => Intl.message(
        'Name this story',
        name: 'nameThisStory',
        desc:
            'A hint appearing in a window for naming a story. This text should not include padding, but now it does.',
      );

  static String get bluetooth => Intl.message(
        'Bluetooth',
        name: 'bluetooth',
        desc: 'The label for the bluetooth settings widget',
      );

  static String get logout => Intl.message(
        'Logout',
        name: 'logout',
        desc: 'The label for logout button.',
      );

  static String get disconnect => Intl.message(
        'Disconnect',
        name: 'disconnect',
        desc: 'The label for "disconnect" button.',
      );

  static String get channel => Intl.message(
        'Channel',
        name: 'channel',
        desc: 'The short name for the "channel" label',
      );

  static String get build => Intl.message(
        'Build',
        name: 'build',
        desc: 'The short name for the build version label',
      );

  static String get urlNotFoundDesc => Intl.message(
        'Can\'t find the component URL',
        name: 'componentUrlNotFound',
        desc: 'The description for ElementProposeError.NOT_FOUND',
      );

  static String get launchRejectedDesc => Intl.message(
        'The element spec is malformed',
        name: 'malformedElementSpec',
        desc: 'The description for ElementProposeError.REJECTED',
      );

  static String get defaultProposeErrorDesc => Intl.message(
        'Something went wrong while launching',
        name: 'elementProposeErrorDescription',
        desc: 'The description for non-triaged element propose errors',
      );

  static String get invalidViewSpecDesc => Intl.message(
        'A valid ViewHolderToken is missing',
        name: 'invalidViewSpecErrorDescription',
        desc: 'The description for ViewControllerEpitaph.INVALID_VIEW_SPEC',
      );

  static String get viewPresentRejectedDesc => Intl.message(
        'The request to present the view is rejected',
        name: 'viewPresentRejectedErrorDescription',
        desc: 'The description for ViewControllerEpitaph.REJECTED',
      );

  static String get defaultPresentErrorDesc => Intl.message(
        'Something went wrong while presenting the view',
        name: 'presentErrorDescription',
        desc: 'The description for non-triaged view present error.',
      );

  static String get errorWhilePresenting => Intl.message(
        'An error occurred while presenting a component',
        name: 'errorOccurredWhilePresentingView',
        desc: 'The alert message for ViewControllerEpitaph type errors',
      );

  static String get errorType => Intl.message(
        'Error Type',
        name: 'errorType',
        desc: 'The alert subtitle for error type',
      );

  static String get moreErrorInformation => Intl.message(
        'For more information, please visit',
        name: 'forMoreInformationForError',
        desc: 'The alert message for the error reference link',
      );

  static String get fuchsiaWelcome => Intl.message(
        'Welcome to Fuchsia',
        name: 'fuchsiaWelcome',
        desc:
            'A welcome to Fuchsia message shown during the setup of a newly installed system',
      );

  static String get oobeChannelTitle => Intl.message(
        'Select an OTA channel',
        name: 'oobeChannelTitle',
        desc: 'The title displayed for OTA channel selection in the OOBE',
      );

  static String get oobeChannelDesc => Intl.message(
        'Fuchsia will get updates from the channel of your choice. It will not get updated if you skip this step.',
        name: 'oobeChannelDesc',
        desc:
            'The description displayed for selecting an OTA channel in the OOBE',
      );

  static String get oobeBetaChannelDesc => Intl.message(
        'A branch promoted on a weekly basis from dogfood or test as assessed by our manual testers and release management',
        name: 'oobeBetaChannelDesc',
        desc: 'The description displayed for the "Beta" update channel',
      );

  static String get oobeDevhostChannelDesc => Intl.message(
        'Receive updates from your local machine.',
        name: 'oobeDevhostChannelDesc',
        desc: 'The description displayed for the "Devhost" update channel',
      );

  static String get oobeDogfoodChannelDesc => Intl.message(
        'A branch promoted on a daily basis from the most recent test version that passed the entry criteria for dogfood as assessed by our manual testers and release management.',
        name: 'oobeDogfoodChannelDesc',
        desc: 'The description displayed for the "Dogfood" update channel',
      );

  static String get oobeStableChannelDesc => Intl.message(
        'A branch promoted on a six-weekly basis from beta.',
        name: 'oobeStableChannelDesc',
        desc: 'The description displayed for the "Stable" update channel',
      );

  static String get oobeTestChannelDesc => Intl.message(
        'A branch auto-promoted from the latest green build on master and auto-pushed to all test channels 4 times per day, Sunday (to account for non-MTV timezones) through Friday. It is used by the manual testers to validate the state of the tree, and by brave team members who want to test the most raw and cutting-edge builds.',
        name: 'oobeTestChannelDesc',
        desc: 'The description displayed for the "Test" update channel',
      );

  static String get oobeLoadChannelError => Intl.message(
        'ERROR: Could not load channels\nThe OTA channel can be selected later from settings menu.',
        name: 'oobeLoadChannelError',
        desc:
            'The message displayed when the list of update channels cannot be loaded.',
      );

  static String get dataSharingTitle => Intl.message(
        'Send Usage & Diagnostics Data to Google',
        name: 'dataSharingTitle',
        desc:
            'The title displayed for choosing whether to consent to send usage and diagnostics data to Google in the OOBE',
      );

  static String get dataSharingDesc1 => Intl.message(
        'We will use the information you give to us to help address technical issues and to improve our services, subject to our ',
        name: 'dataSharingDesc1',
        desc:
            'The description of what is being agreed to when choosing to send usage and diagnostics data to Google in the OOBE',
      );

  static String get dataSharingDesc2 => Intl.message(
        'privacy policy',
        name: 'dataSharingDesc2',
        desc:
            'The text from the data sharing description that when clicked will display the privacy policy',
      );

  static String get dataSharingDesc3 => Intl.message(
        '. You can change this set-up at anytime later in Settings.',
        name: 'dataSharingDesc3',
        desc:
            'The description of what is being agreed to when choosing to send usage and diagnostics data to Google in the OOBE',
      );

  static String get privacyPolicyTitle => Intl.message(
        'Privacy Policy',
        name: 'privacyPolicyTitle',
        desc: 'The title displayed for the privacy policy',
      );

  static String get oobeSshKeysAddTitle => Intl.message(
        'Add Your SSH Key',
        name: 'oobeSshKeysAddTitle',
        desc: 'The title displayed for adding an SSH key in the OOBE',
      );

  static String get oobeSshKeysConfirmTitle => Intl.message(
        'Confirm Your Key',
        name: 'oobeSshKeysConfirmTitle',
        desc:
            'The title displayed for confirming the SSH key to be added in the OOBE',
      );

  static String get oobeSshKeysErrorTitle => Intl.message(
        'Something Went Wrong',
        name: 'oobeSshKeysErrorTitle',
        desc:
            'The title displayed when an error occurs while adding an SSH key in the OOBE',
      );

  static String get oobeSshKeysSuccessTitle => Intl.message(
        'You Are All Set',
        name: 'oobeSshKeysSuccessTitle',
        desc:
            'The title displayed when an SSH key was successfully added in the OOBE',
      );

  static String get oobeSshKeysAddDesc => Intl.message(
        'Register your SSH key either by pulling from your Gitbub account or by adding manually. The workstation will be still available even if you skip this step. However, it will not be possible to access to the device for development without a valid key. You can also set it up later in Settings.',
        name: 'oobeSshKeysDesc',
        desc: 'The description displayed of adding an SSH key in the OOBE',
      );

  static String oobeSshKeysSelectionDesc(int numKeys) => Intl.message(
        'We found $numKeys key(s) from your github account. Please continue adding a key if the following information is correct.',
        name: 'oobeSshKeysGithubConfirmDesc',
        desc:
            'The description displayed when confirming which SSH key that was pulled from github should be added in the OOBE',
      );

  static String oobeSshKeysGithubErrorDesc(String username) => Intl.message(
        'Unable to pull a key from the github account, $username.\nPlease check if the username is correct and try again.',
        name: 'oobeSshKeysConfirmDesc',
        desc:
            'The description displayed when an error occurs while pulling SSH keys from GitHub',
      );

  static String oobeSshKeysHttpErrorDesc(int status, String statusPhrase) =>
      Intl.message(
        'Unable to pull a key from github.\nHTTP request returned status: $status: $statusPhrase.',
        name: 'oobeSshKeysHttpErrorDesc',
        desc:
            'The description displayed when the API call to GitHub does not return an OK status',
      );

  static String get oobeSshKeysFidlErrorDesc => Intl.message(
        'Unable to add the key you provided.\nPlease check if the key you entered is correct and try again.',
        name: 'oobeSshKeysFidlErrorDesc',
        desc:
            'The description displayed when an error occurs while adding an SSH to the device',
      );

  static String get oobeSshKeysSuccessDesc => Intl.message(
        'Your SSH key has been successfully added.\nYou are now ready to use Fuchsia Workstation. Enjoy!',
        name: 'oobeSshKeysSuccessDesc',
        desc:
            'The description displayed when an SSH key was successfully added in the OOBE',
      );

  static String get oobeSshKeysGithubMethod => Intl.message(
        'via GitHub',
        name: 'oobeSshKeysGithubMethod',
        desc: 'The text displayed for choosing to add a key from github',
      );

  static String get oobeSshKeysManualMethod => Intl.message(
        'Manually',
        name: 'oobeSshKeysManualMethod',
        desc: 'The text displayed for choosing to add a key manually',
      );

  static String get next => Intl.message(
        'Next',
        name: 'next',
        desc: 'The label for the "next" button.',
      );

  static String get agree => Intl.message(
        'Agree',
        name: 'agree',
        desc: 'The label for the "agree" button.',
      );

  static String get disagree => Intl.message(
        'Disagree',
        name: 'disagree',
        desc: 'The label for the "disagree" button.',
      );

  static String get close => Intl.message(
        'Close',
        name: 'close',
        desc: 'The label for the "close" button.',
      );

  static String get add => Intl.message(
        'Add',
        name: 'add',
        desc: 'The label for the "add" button.',
      );

  static String get retry => Intl.message(
        'Retry',
        name: 'retry',
        desc: 'The label for the "retry" button.',
      );

  static String get ok => Intl.message(
        'Ok',
        name: 'ok',
        desc: 'The label for the "ok" button.',
      );

  static String get username => Intl.message(
        'Username',
        name: 'username',
        desc: 'The label for the "username" text field.',
      );

  static String get key => Intl.message(
        'Key',
        name: 'key',
        desc: 'The label for the SSH key text field.',
      );

  static String get sshKeys => Intl.message(
        'SSH Key(s)',
        name: 'sshKeys',
        desc: 'The label for displaying one or more SSH keys.',
      );
  static String get systemInformation => Intl.message(
        'System Information',
        name: 'System Information',
        desc: 'The label for System Information.',
      );
  static String get view => Intl.message(
        'View',
        name: 'view',
        desc: 'The label for the "view" text field.',
      );
  static String get loading => Intl.message(
        'Loading',
        name: 'loading',
        desc: 'The label for the "loading" text field.',
      );
  static String get feedback => Intl.message(
        'Feedback',
        name: 'feedback',
        desc: 'The label for the "feedback" text field.',
      );
  static String get please => Intl.message(
        'Please',
        name: 'please',
        desc: 'The label for the "please" text field.',
      );
  static String get visit => Intl.message(
        'Visit',
        name: 'visit',
        desc: 'The label for the "visit" text field.',
      );
  static String get error => Intl.message(
        'Error',
        name: 'error',
        desc: 'The label for the "error" text field.',
      );
  static String get openSource => Intl.message(
        'Open Source',
        name: 'open source',
        desc: 'The label for the "open source" text field.',
      );
  static String get license => Intl.message(
        'License',
        name: 'license',
        desc: 'The label for the "license" text field.',
      );
  static String get shortcuts => Intl.message(
        'Shortcuts',
        name: 'shortcuts',
        desc: 'The button label for displaying keyboard shortcuts',
      );
  static String get open => Intl.message(
        'Open',
        name: 'open',
        desc: 'The button label for the "open" text field.',
      );
  static String get keyboard => Intl.message(
        'Keyboard',
        name: 'keyboard',
        desc: 'The button label for "Keyboard" text field.',
      );
  static String get language => Intl.message(
        'Language',
        name: 'language',
        desc: 'The button label for "Language" text field.',
      );
  static String get disclaimer => Intl.message(
        'Disclaimer',
        name: 'disclaimer',
        desc: 'The label for "Disclaimer" text field.',
      );
  static String get aboutFuchsia => Intl.message(
        'About Fuchsia',
        name: 'about fuchsia',
        desc: 'The label for "About Fuchsia" text field.',
      );
  static String get updatesAndPrivacy => Intl.message(
        'Updates & Privacy',
        name: 'updates & privacy',
        desc: 'The label for "Updates & Privacy" text field.',
      );
  static String get disclaimerText => Intl.message(
        'Workstation is an open source reference design for Fuchsia. It’s intended as a developer tool to explore Fuchsia, a brand new operating system built from scratch.\n\nThis is a developer tool - not a consumer oriented product. This preview is intended for developers and enthusiasts to explore and experiment with, but does not come with strong security, privacy, or robustness guarantees.\n\nExpect bugs and rapid changes!\n\nPlease file bugs and send feedback to help improve Fuchsia!',
        name: 'disclaimer text',
        desc: 'The disclaimer text for workstation.',
      );
  static String get updatesAndPrivacyText => Intl.message(
        'This system is set to automatically update to improve over time. Use of the update servers are governed by Google APIs Terms of Service and data collected by Google will be handled in accordance with the Google Privacy Policy. To use a Fuchsia system that doesn’t automatically update, visit fuchsia.dev/fuchsia-src/get-started.',
        name: 'updates and privacy text',
        desc: 'The updates and privacy text for workstation.',
      );
  static String get currentChannel => Intl.message(
        'Current Channel',
        name: 'current channel',
        desc: 'The label for "Current Channel" text field.',
      );
  static String get selectAnUpdateChannel => Intl.message(
        'Select an update channel.',
        name: 'select an update channel',
        desc: 'The label for "Select an update channel." text field.',
      );
  static String downloadTargetChannel(String targetChannel) => Intl.message(
        'Download and apply updates of $targetChannel channel.',
        name: 'download target channel text',
        desc:
            'The text displayed for downloading and updating to target channel.',
        args: [targetChannel],
      );
  static String loadingApplication(String title) => Intl.message(
        'Loading $title',
        name: 'loading application with title',
        desc: 'The lable on the circular loading indicator.',
        args: [title],
      );
  static String get applicationNotResponding => Intl.message(
        'This application is not responding',
        name: 'application is not responding message',
        desc: 'The title of dialog to handle unresponsive application.',
      );
  static String promptToWaitOrClose(String title) => Intl.message(
        '$title is taking long to load. Would you like to wait for another 10 seconds or stop loading and close the app?',
        name: 'application taking too long to load. wait or close',
        desc: 'The prompt text to wait or close unresponsive app.',
        args: [title],
      );
  static String applicationFailedToStart(String title) => Intl.message(
        '$title failed to start',
        name: 'application failed to load',
        desc: 'The alert dialog title for an application that failed to start.',
        args: [title],
      );
  static String get wait => Intl.message(
        'Wait',
        name: 'wait',
        desc: 'The button label for the "wait" command.',
      );
  static String get cancelKeyboardShortcut => Intl.message(
        'Dismiss system overlays',
        name: 'dismiss system overlays',
        desc: 'Keyboard shortcut description to dismiss system overlays.',
      );
  static String get closeKeyboardShortcut => Intl.message(
        'Close top view',
        name: 'close top view',
        desc: 'Keyboard shortcut description to close topmost view.',
      );
  static String get launcherKeyboardShortcut => Intl.message(
        'Show Launcher',
        name: 'show launcher',
        desc: 'Keyboard shortcut description to show launcher.',
      );
  static String get settingsKeyboardShortcut => Intl.message(
        'Show Quick Settings',
        name: 'show quick settings',
        desc: 'Keyboard shortcut description to show quick settings.',
      );
  static String get shortcutsKeyboardShortcut => Intl.message(
        'Show Keyboard Shortcuts',
        name: 'show keyboard shortcuts',
        desc: 'Keyboard shortcut description to show keyboard shortcuts.',
      );
  static String get screenSaverKeyboardShortcut => Intl.message(
        'Start screen saver',
        name: 'start screen saver',
        desc: 'Keyboard shortcut description to start screen saver.',
      );
  static String get switchNextKeyboardShortcut => Intl.message(
        'Switch to next app',
        name: 'switch to next app',
        desc: 'Keyboard shortcut description to switch to next app.',
      );
  static String get switchPrevKeyboardShortcut => Intl.message(
        'Switch to previous app',
        name: 'switch to previous app',
        desc: 'Keyboard shortcut description to switch to previous app.',
      );
  static String get update => Intl.message(
        'Update',
        name: 'update',
        desc: 'The label for "Update" text field.',
      );
  static String get continueLabel => Intl.message(
        'Continue',
        name: 'continue',
        desc: 'The label for "Continue" text field.',
      );
  static String get channelUpdateAlertTitle => Intl.message(
        'System will reboot',
        name: 'system will reboot',
        desc: 'The alert dialog title before attempting channel update.',
      );
  static String get channelUpdateAlertBody => Intl.message(
        'This action will download the latest OTA updates and reboot the system. You will lose unsaved data. Do you want to continue?',
        name: 'this action will download the latest OTA updates',
        desc: 'The alert dialog body before attempting channel update.',
      );
  static String get updating => Intl.message(
        'Updating...',
        name: 'updating...',
        desc: 'The label for "Updating..." text field.',
      );
  static String get noUpdateAvailableTitle => Intl.message(
        'No update available.',
        name: 'no update available',
        desc: 'The label for "No update available" text field.',
      );
  static String get errorCheckingForUpdate => Intl.message(
        'Error checking for update.',
        name: 'Error checking for update',
        desc: 'The label for "Error checking for update" text field.',
      );
  static String get checkingForUpdate => Intl.message(
        'Checking for update...',
        name: 'Checking for update...',
        desc: 'The label for "Checking for update..." text field.',
      );
  static String get noUpdateAvailableSubtitle => Intl.message(
        'There is not an update available at this time.',
        name: 'There is not an update available at this time',
        desc: 'The subtitle for "No update available" text field.',
      );
  static String get waitingForReboot => Intl.message(
        'System is about to reboot. Please wait...',
        name: 'System is about to reboot. Please wait...',
        desc: 'The text dialog while waiting for reboot during channel update.',
      );
  static String get installationDeferredByPolicyTitle => Intl.message(
        'Update installation is deferred',
        name: 'Update installation is deferred',
        desc:
            'The alert dialog title for the installation deferred by policy update state.',
      );
  static String get installationDeferredByPolicyBody => Intl.message(
        'Due to policy restrictions the update is not available.',
        name: 'Due to policy restrictions the update is not available.',
        desc:
            'The alert dialog body for the installation deferred by policy update state.',
      );
  static String get installationErrorTitle => Intl.message(
        'Update installation failed',
        name: 'Update installation failed',
        desc: 'The alert dialog title for the installation error update state.',
      );
  static String get installationErrorBody => Intl.message(
        'An error occured while installing the update.',
        name: 'An error occured while installing the update.',
        desc: 'The alert dialog body for the installation error update state.',
      );

  /// Lookup message given it's name.
  static String? lookup(String name) {
    final _messages = <String, String>{
      'cancelKeyboardShortcut': cancelKeyboardShortcut,
      'closeKeyboardShortcut': closeKeyboardShortcut,
      'launcherKeyboardShortcut': launcherKeyboardShortcut,
      'settingsKeyboardShortcut': settingsKeyboardShortcut,
      'shortcutsKeyboardShortcut': shortcutsKeyboardShortcut,
      'screenSaverKeyboardShortcut': screenSaverKeyboardShortcut,
      'switchNextKeyboardShortcut': switchNextKeyboardShortcut,
      'switchPrevKeyboardShortcut': switchPrevKeyboardShortcut,
    };
    return _messages[name];
  }
}
