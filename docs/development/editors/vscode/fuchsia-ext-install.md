# Installing the Fuchsia developer extension

The [Fuchsia developer extension][extension-link]{: .external} provides support for debugging
Fuchsia devices, logging, and syntax highlighting. This extension is Fuchsia’s official extension 
and can be used with the source tree and the SDK. 


### Prerequisites

Note: For more information on configuring see [Using VS Code with Fuchsia][vscode-fuchsia].

Before you begin:

* Download [Visual Studio Code][vscode]{: .external}.


### Installation

<img class="vscode-image vscode-image-center"
     alt="This figure shows the image of the fuchsia extension logo "
     src="images/extensions/extension-logo.png"
     width = "10%"/>

* Download the [Fuchsia extension][extension-link]{: .external} off of Visual Studio Marketplace.

Note: The extension automatically detects the appropriate settings for each workspace that you use,
including the location of relevant tools such as ffx.  If these settings are incorrect or not set automatically,
follow the section below.

* {SDK}

    Note: For more information about the Fuchsia SDK and how to configure your environment, 
    see [SDK fundamentals][sdk-fundamentals].

    1. Open your desired workspace. For example, open the [getting-started repository][sdk-fundamentals] in your VS Code workspace.
    1. The extension should automatically detect the path to ffx. If not detected, follow these steps:
        1. In VS Code navigate to the main menu, click **Code**, then **Preferences**, then **Settings**.
        1. Under **Extensions** navigate to **Fuchsia SDK** then **Ffx Path**.
        1. Enter the path to `ffx` directory (for example, `~/fuchsia/getting-started/tools/ffx`).
        1. Verify the extension is working via the the button in the bottom right corner. Click said button, which lists a Fuchsia target device and ensure that your device is connected.
 
    If there is no Fuchsia device that is running, including the emulator, you will see the following in the **Output** tab:

    ```none {:.devsite-disable-click-to-copy}
    Running: ffxPath target,list,--format,json
    exit: 2: null
    ```

* {Source Tree}

    Note: For more information about the Fuchsia source tree and how to configure your environment, 
    see [source tree fundamentals][sourcetree-fundamentals].

    1. Open your desired workspace. For example, open the [sample repository][sourcetree-fundamentals]
    in your VS Code workspace.
    1. The extension should automatically detect the path to ffx. If not detected follow the following steps:
        1. In VS Code navigate to the main menu, click **Code**, then **Preferences**, then **Settings**.
        1. Under **Extensions** navigate to **Fuchsia SDK** then **Ffx Path**.
        1. Enter the path to `ffx` directory (for example, ` ~/fuchsia/tools/ffx`).
        1. Verify the extension is working via the the button in the bottom right corner. Click said button, which lists a Fuchsia target device and ensure that your device is connected.

    If there is no Fuchsia device that is running, including the emulator, you will see the following in the **Output** tab:

    ```none {:.devsite-disable-click-to-copy}
    Running: ffxPath target,list,--format,json
    exit: 2: null
    ```

You have successfully configured the Fuchsia developer extension!

<!-- Reference links -->
[sdk-fundamentals]: /docs/get-started/sdk/learn
[sourcetree-fundamentals]: /docs/get-started/learn
[vscode-fuchsia]: /docs/development/editors/vscode/README.md
[vscode]: https://code.visualstudio.com/
[extension-link]: https://code.visualstudio.com/
