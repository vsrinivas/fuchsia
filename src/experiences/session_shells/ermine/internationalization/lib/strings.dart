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

  static String get apply => Intl.message(
        'Apply',
        name: 'apply',
        desc: 'Button label to apply changes.',
      );

  static String get scale => Intl.message(
        'Scale',
        name: 'scale',
        desc: 'Text label for scaling the UI.',
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

  static String get login => Intl.message(
        'Login',
        name: 'login',
        desc: 'The text for login prompt.',
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
        'Welcome to Fuchsia Workstation',
        name: 'fuchsiaWelcome',
        desc:
            'A welcome to Fuchsia message shown during the setup of a newly installed system',
      );

  static String get startWorkstation => Intl.message(
        'Start Workstation',
        name: 'startWorkstation',
        desc: 'The button label to launch the workstation application',
      );

  static String get accountPasswordTitle => Intl.message(
        'Set your login password',
        name: 'accountPasswordTitle',
        desc: 'The title for the creation of account password during OOBE',
      );

  static String get accountPasswordInvalid => Intl.message(
        'The password does not meet the requirements.',
        name: 'accountPasswordInvalid',
        desc: 'The error text when password does not meet requirements',
      );

  static String get accountPasswordFailedAuthentication => Intl.message(
        'The password you entered was incorrect.',
        name: 'accountPasswordFailedAuthentication',
        desc: 'The error text when password does fails authentication',
      );

  static String get accountPartitionNotFound => Intl.message(
        'Account partition not found. Please re-pave the device.',
        name: 'accountPartitionNotFound',
        desc: 'The error text when account partition is not found',
      );

  static String get accountNotFound => Intl.message(
        'Account not found. Please reboot the device.',
        name: 'accountNotFound',
        desc: 'The error text when an account is not found',
      );

  static String get accountPasswordMismatch => Intl.message(
        'The passwords do not match.',
        name: 'accountPasswordMismatch',
        desc: 'The error text when password and confirm password do not match',
      );

  static String accountPasswordDesc(int passwordLength) => Intl.message(
        'Enter a password to protect your data on Workstation.\n'
        'The length should be at least $passwordLength characters.\n'
        '\n'
        'This can be done only once and you cannot change it until you factory-reset the device.',
        name: 'accountPasswordDesc',
        desc:
            'The description for the creation of account password during OOBE',
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
        'Share usage & diagnostics information with Google',
        name: 'dataSharingTitle',
        desc:
            'The title displayed for choosing whether to consent to send usage and diagnostics data to Google in the OOBE',
      );

  static String get dataSharingDesc => Intl.message(
        'You can later change the preference in Settings > Usage & Diagnostics.',
        name: 'dataSharingDesc1',
        desc:
            'The description of where the user can mange the data sharing consent status',
      );

  static String get dataSharingCheckboxLabel => Intl.message(
        'Help improve Fuchsia Workstation by automatically sending usage and diagnostic data to Google.',
        name: 'data sharing checkbox label',
        desc:
            'The text label for the checkbox that the user can opt in/out the automated telemetry data sharing service with',
      );

  static String dataSharingPrivacyTerms(String url) => Intl.message(
        'Google\'s Privacy & Terms is available at $url',
        name: 'data sharing privacy terms',
        desc:
            'The description of where the user can find Google\'s privacy terms',
        args: [url],
        examples: {'url': 'policies.google.com/privacy'},
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
        'Your SSH key has been successfully added.',
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

  static String get setPassword => Intl.message(
        'Set Password',
        name: 'setPassword',
        desc: 'The label for the "set password" button.',
      );

  static String get passwordHint => Intl.message(
        'Password',
        name: 'passwordHint',
        desc: 'The label for the entering password in a text edit field.',
      );

  static String get passwordIsSet => Intl.message(
        'Your password has been set',
        name: 'passwordIsSet',
        desc: 'The label for when the password was successfully set.',
      );

  static String get readyToUse => Intl.message(
        'Fuchsia Workstation is ready to use.',
        name: 'readyToUse',
        desc: 'The label for when oobe is complete and workstation is ready.',
      );

  static String get confirmPasswordHint => Intl.message(
        'Re-enter password',
        name: 'confirmPasswordHint',
        desc: 'The label for the re-entering password in a text edit field.',
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
  static String get reportAnIssue => Intl.message(
        'Report an Issue (internal)',
        name: 'user feedback',
        desc: 'The label for the "user feedback" text field.',
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
  static String get navigateBackKeyboardShortcut => Intl.message(
        'Navigate back',
        name: 'navigate back',
        desc: 'Keyboard shortcut description to go back to previous state.',
      );
  static String get refreshKeyboardShortcut => Intl.message(
        'Refresh',
        name: 'refresh',
        desc: 'Keyboard shortcut description to refresh current view.',
      );
  static String get fullscreenToggleKeyboardShortcut => Intl.message(
        'Toggle Fullscreen',
        name: 'toggle fullscreen',
        desc: 'Keyboard shortcut description to toggle fullscreen view mode.',
      );
  static String get arrangeWindowsKeyboardShortcut => Intl.message(
        'Arrange Windows',
        name: 'arrange windows',
        desc: 'Keyboard shortcut description to arrange windows on screen.',
      );
  static String get decreaseBrightnessKeyboardShortcut => Intl.message(
        'Decrease Brightness',
        name: 'decrease brighness',
        desc: 'Keyboard shortcut description to decrease screen brightness.',
      );
  static String get increaseBrightnessKeyboardShortcut => Intl.message(
        'Increase Brightness',
        name: 'increase brighness',
        desc: 'Keyboard shortcut description to increase screen brightness.',
      );
  static String get playPauseMediaKeyboardShortcut => Intl.message(
        'Play/Pause Media',
        name: 'play/pause media',
        desc: 'Keyboard shortcut description to play or pause media playback.',
      );
  static String get muteVolumeKeyboardShortcut => Intl.message(
        'Mute Volume',
        name: 'mute volume',
        desc: 'Keyboard shortcut description to mute sound volume.',
      );
  static String get decreaseVolumeKeyboardShortcut => Intl.message(
        'Decrease Volume',
        name: 'decrease volume',
        desc: 'Keyboard shortcut description to decrease sound volume.',
      );
  static String get increaseVolumeKeyboardShortcut => Intl.message(
        'Increase Volume',
        name: 'increase volume',
        desc: 'Keyboard shortcut description to increase sound volume.',
      );
  static String get logoutKeyboardShortcut => Intl.message(
        'Logout',
        name: 'logout',
        desc: 'Keyboard shortcut description to logout.',
      );
  static String get zoomInShortcut => Intl.message(
        'Zoom In',
        name: 'zoomIn',
        desc: 'Keyboard shortcut description to zoom in the shell UI.',
      );
  static String get zoomOutShortcut => Intl.message(
        'Zoom Out',
        name: 'zoomOut',
        desc: 'Keyboard shortcut description to zoom out the shell UI.',
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
  static String get confirmLogoutAlertTitle => Intl.message(
        'Are you sure you want to log out?',
        name: 'confirmLogoutAlertTitle',
        desc: 'The alert dialog title to confirm logout.',
      );
  static String get confirmToSaveWorkAlertBody => Intl.message(
        'This will close all running applications and you could lose unsaved work.',
        name: 'confirmToSaveWorkAlertBody',
        desc: 'The alert dialog body to continue logout/restart/shutdown.',
      );
  static String get confirmRestartAlertTitle => Intl.message(
        'Are you sure you want restart?',
        name: 'confirmRestartAlertTitle',
        desc: 'The alert dialog title to confirm restart.',
      );
  static String get confirmShutdownAlertTitle => Intl.message(
        'Are you sure you want shutdown?',
        name: 'confirmShutdownAlertTitle',
        desc: 'The alert dialog title to confirm shutdown.',
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
  static String updating(int percent) => Intl.message(
        'Updating... $percent%',
        name: 'updating...',
        desc: 'The label for "Updating..." text field.',
        args: [percent],
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
  static String get mute => Intl.message(
        'Mute',
        name: 'mute',
        desc: 'The label for "Mute" text field.',
      );
  static String get unmute => Intl.message(
        'Unmute',
        name: 'unmute',
        desc: 'The label for "Unmute" text field.',
      );
  static String get connect => Intl.message(
        'Connect',
        name: 'connect',
        desc: 'The label for "connect" text field.',
      );
  static String enterPasswordForNetwork(String network) => Intl.message(
        'Enter password for $network',
        name: 'Enter password for network',
        desc: 'The prompt text to enter password for selected network.',
        args: [network],
      );
  static String get selectNetwork => Intl.message(
        'Select a network.',
        name: 'Select a network.',
        desc: 'The label for "select a network" text field.',
      );

  static String get showPassword => Intl.message(
        'Show password',
        name: 'Show password',
        desc: 'The label for "Show password" checkbox.',
      );
  static String get savedNetworks => Intl.message(
        'Saved Networks',
        name: 'Saved networks',
        desc: 'The label for "Saved Networks" checkbox.',
      );
  static String get otherNetworks => Intl.message(
        'Other Networks',
        name: 'Other networks',
        desc: 'The label for "Other Networks" checkbox.',
      );

  static String get factoryDataReset => Intl.message(
        'Erase all user data and settings and reset password',
        name: 'factoryDataReset',
        desc: 'The label of button to factory data reset a device.',
      );

  static String get factoryDataResetTitle => Intl.message(
        'Are you sure you want to erase all user data and settings and reset '
        'the password?',
        name: 'eraseAndReset',
        desc: 'The title of the factory data reset alert dialog.',
      );

  static String get factoryDataResetPrompt => Intl.message(
        'This will also remove the SSH keys. If you want to connect to the '
        'device again for debugging, you\'ll have to repave the device or '
        'set up the SSH keys.',
        name: 'factoryDataReset',
        desc: 'The body text of the factory data reset alert dialog.',
      );

  static String get eraseAndReset => Intl.message(
        'Erase & Reset',
        name: 'eraseAndReset',
        desc: 'The label of button to factory data reset a device.',
      );

  static String get forget => Intl.message(
        'Forget',
        name: 'forget',
        desc: 'The label for "Forget" text field.',
      );

  static String connectToNetwork(String network) => Intl.message(
        'Connect to $network',
        name: 'Connect to network',
        desc: 'The prompt text to connected to selected network.',
        args: [network],
      );
  static String get connected => Intl.message(
        'Connected',
        name: 'connected',
        desc: 'The label for "Connected" text field.',
      );
  static String get incorrectPassword => Intl.message(
        'Incorrect Password',
        name: 'incorrect password',
        desc: 'The label for "Incorrect Password" text field.',
      );
  static String get failedToTurnOnWifi => Intl.message(
        'Failed to turn on Wi-Fi.',
        name: 'failedToTurnOnWifi',
        desc:
            'Tooltip text for the wifi toggle warning icon when turning on wifi fails.',
      );
  static String get failedToTurnOffWifi => Intl.message(
        'Failed to turn off Wi-Fi.',
        name: 'failedToTurnOffWifi',
        desc:
            'Tooltip text for the wifi toggle warning icon when turning off wifi fails.',
      );

  static String get usageAndDiagnostics => Intl.message(
        'Usage & Diagnostics',
        name: 'usage and diagnostics',
        desc:
            'The title for "Usage & Diagnostics Sharing" menu in Quick Settings',
      );

  static String get sharingOn => Intl.message(
        'Sharing on',
        name: 'sharing on',
        desc: 'A text indicator of the data sharing opt-in status',
      );

  static String get sharingOff => Intl.message(
        'Sharing off',
        name: 'sharing off',
        desc: 'A text indicator of the data sharing opt-out status',
      );

  static String get helpFuchsia => Intl.message(
        'Help improve Fuchsia by automatically sending diagnostic '
        'and usage data to Google.',
        name: 'help improve fuchsia',
        desc: 'The description for "Usage & Diagnostics"',
      );

  static String get privacyTerms => Intl.message(
        'Google’s privacy & terms',
        name: 'google privacy and terms',
        desc: 'The text link to the Google\'s privacy and terms webpage.',
      );

  static String get turnOnDataSharingTitle => Intl.message(
        'Turn on sharing to send feedback',
        name: 'the title of the alert dialog to turn on data sharing',
        desc: 'The title of the alert that pops up when the user tries to '
            'open User Feedback when the data sharing is disabled',
      );

  static String get turnOnDataSharingBody => Intl.message(
        'Report an Issue is available only when you have consented to sharing '
        'usage and diagnostics data. You can update the preference in Quick '
        'Settings > Usage & Diagnostics.',
        name: 'the body text of the alert dialog to turn on data sharing',
        desc: 'The body text of the alert that pops up when the user tries to '
            'open User Feedback when the data sharing is disabled',
      );

  static String get firstTimeUserFeedback1 => Intl.message(
        'All data submitted through this form will be uploaded to the crash '
        'server and a corresponding bug will be created in Monorail. You may '
        'later be cc\'d on the bug with the username you provide in the form.',
        name: 'first time user feedback message 1',
        desc: 'The first paragraph of the first-time user feedback message',
      );

  static String firstTimeUserFeedback2(String url) => Intl.message(
        'If you want to directly create a Monorail bug by yourself, '
        'please visit $url',
        name: 'first time user feedback message 2',
        desc: 'The second paragraph of the first-time user feedback message.',
        args: [url],
        examples: const {'url': 'https://bugs.fuchsia.dev'},
      );

  static String get doNotShowAgain => Intl.message(
        'Don\'t show this message again',
        name: 'do not show this message again',
        desc: 'The checkbox label for the option to supress the user feedback '
            'first-time dialog',
      );

  static String get okGotIt => Intl.message(
        'OK, got it',
        name: 'ok got it',
        desc: 'The label of the button on the user feedback first-time dialog',
      );

  static String get sendFeedback => Intl.message(
        'Send feedback to Google',
        name: 'send feedback',
        desc: 'The title for the user feedback form',
      );

  static String get noPII => Intl.message(
        'Please do not include personal information in the description unless '
        'it is necessary to describe the issue.',
        name: 'noPII',
        desc:
            'A warning message not to include personal data in the user feedback form',
      );

  static String get issueTitle => Intl.message(
        'Issue Title',
        name: 'issue title',
        desc: 'The label for the issue title input field',
      );

  static String get description => Intl.message(
        'Description',
        name: 'issue description',
        desc: 'The label for the issue description input field',
      );

  static String get needDescription => Intl.message(
        'Please write what you are reporting about.',
        name: 'issue description is needed',
        desc:
            'The error message displayed when the user tries to submit the feedback form without issue description',
      );

  static String get needUsername => Intl.message(
        'Please enter your corp username',
        name: 'reporter\'s username is required.',
        desc:
            'The error message displayed when the user tries to submit the feedback form without their username',
      );

  static String get submit => Intl.message(
        'Submit',
        name: 'submit',
        desc: 'The lable of the Submit button',
      );

  static String get submittedTitle => Intl.message(
        'Feedback has been sent.',
        name: 'feedback submitted title',
        desc: 'The title for the user feedback submission complete page',
      );

  static String submittedDesc(String id) => Intl.message(
        'Thanks for submitting your report. It has been filed successfully and '
        'will appear on the crash server when the upload is complete.\n\n'
        'This report will also create a Monorail bug. You\'ll be notified by '
        'email when the bug is ready.\n\n'
        'Report ID: $id',
        name: 'user feedback submission description',
        desc: 'The description on the user feedback submission complete page',
        examples: const {'id': '271649084504292-5ts4-8ew5'},
        args: [id],
      );

  static String get failedToFileTitle => Intl.message(
        'Failed to file your report',
        name: 'failed to file your report',
        desc: 'The title of the user feedback filing error page',
      );

  static String failedToFileDesc(String url) => Intl.message(
        'Please try again later. Alternatively, you can create a report at $url.',
        name: 'try to file the report later',
        desc: 'The description of the user feedback filing error page',
        examples: const {'url': 'go/workstation-feedback'},
        args: [url],
      );

  static String dataSharingLegalStatement(String legalHelpUrl,
          String privacyPolicyUrl, String termsOfServiceUrl,
          [String prefix = '[', String midfix = '](', String suffix = ')']) =>
      Intl.message(
        'Device logs are sent to Google along with this report. '
        'Go to the ${prefix}Legal Help$midfix$legalHelpUrl$suffix page to request '
        'content changes for legal reasons. We will use the information you '
        'give us to help address technical issues and to improve our services, '
        'subject to our ${prefix}Privacy Policy$midfix$privacyPolicyUrl$suffix '
        'and ${prefix}Terms of Service$midfix$termsOfServiceUrl$suffix.',
        name: 'legal statement for data sharing consent',
        desc: 'Statement including text links in the markdown format that opens'
            ' web pages that show legal information regarding data sharing '
            'with data sharing relevant information.',
        examples: const {
          'legalHelpUrl': 'https://support.google.com/legal/answer/3110420',
          'privacyPolicyUrl': 'https://policies.google.com/privacy',
          'termsOfServiceUrl': 'https://policies.google.com/terms',
          'prefix': '[',
          'midfix': '](',
          'suffix': ')'
        },
        args: [
          legalHelpUrl,
          privacyPolicyUrl,
          termsOfServiceUrl,
          prefix,
          midfix,
          suffix
        ],
      );

  static String get restartOrShutDown => Intl.message(
        'Do you want to restart or shut down the device?',
        name: 'do you want to restart or shut down',
        desc: 'The title of the dialog for power button pressed',
      );

  static String get powerBtnPressedDesc => Intl.message(
        'These actions will close all running apps and you could '
        'lose unsaved work. The device will shut down automatically '
        'if you press and hold the power button for 8 seconds.',
        name: 'restart / shutdown warning',
        desc: 'The description of the dialog for power button pressed.',
      );

  static String get lowBattery => Intl.message(
        'Low battery',
        name: 'low battery',
        desc: 'The title of the low battery alert dialog.',
      );

  static String percentRemaining(String level) => Intl.message(
        '$level% remaining',
        name: 'remaining battery level',
        desc: 'The body text of the low battery alert dialog.',
        examples: const {'level': '2'},
        args: [level],
      );

  static String get unlock => Intl.message(
        'Unlock',
        name: 'unlock',
        desc: 'The label for "Unlock" text field.',
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
      'navigateBackKeyboardShortcut': navigateBackKeyboardShortcut,
      'refreshKeyboardShortcut': refreshKeyboardShortcut,
      'fullscreenToggleKeyboardShortcut': fullscreenToggleKeyboardShortcut,
      'arrangeWindowsKeyboardShortcut': arrangeWindowsKeyboardShortcut,
      'decreaseBrightnessKeyboardShortcut': decreaseBrightnessKeyboardShortcut,
      'increaseBrightnessKeyboardShortcut': increaseBrightnessKeyboardShortcut,
      'playPauseMediaKeyboardShortcut': playPauseMediaKeyboardShortcut,
      'muteVolumeKeyboardShortcut': muteVolumeKeyboardShortcut,
      'decreaseVolumeKeyboardShortcut': decreaseVolumeKeyboardShortcut,
      'increaseVolumeKeyboardShortcut': increaseVolumeKeyboardShortcut,
      'zoomInKeyboardShortcut': zoomInShortcut,
      'zoomOutKeyboardShortcut': zoomOutShortcut,
      'reportAnIssueKeyboardShortcut': reportAnIssue,
    };
    return _messages[name];
  }
}
