# Copy files to and from a component

Use `ffx component copy` to copy files to and from Fuchsia components on a device.

Note: If you need to copy to isolated storage that doesn't currently exist, see [Copy to an isolated storage](#copy-to-non-existent-storage).

## Concepts

Every Fuchsia component has a [namespace][namespace]. A Fuchsia component can
use persistent storage via [storage capabilities][storage-capabilities]. All
the examples in this guide select the `data` directory, which provides
persistent storage on the device.

Before you upload files to (or download files from) a Fuchsia component,
you need to identify the [absolute moniker][absolute-moniker] of your
target component on the device. To find a component's absolute moniker,
use the [`ffx component show`][ffx-component-show] command,
for example:

``` none {:.devsite-disable-click-to-copy}
$ ffx component show stash_secure
               Moniker: /core/stash_secure
                   URL: fuchsia-pkg://fuchsia.com/stash#meta/stash_secure.cm
                  Type: CML static component
       Component State: Resolved
           Instance ID: c1a6d0aebbf7c092c53e8e696636af8ec0629ff39b7f2e548430b0034d809da4
           ...
```

The example above shows the moniker of the `stash_secure` component is
`/core/stash_secure`.

Once you identify your target component's moniker, use this
moniker (as a parameter to the `ffx component copy`) to access
the component's namespace on the device. For the examples below, use
`/core/stash_secure`.

## Download a file from the device {:#download-a-file-from-the-device}

To download a file from a Fuchsia device to your host machine, run the
following command:

```
ffx component copy <MONIKER>::<PATH_TO_FILE> <DESTINATION>
```

Replace the following:

<table class="responsive">
   <tr>
      <th>Argument</th>
      <th>Value</th>
   </tr>
   <tr>
      <td>MONIKER</td>
      <td>The absolute moniker of your target component.
         For example, <code>/core/stash_secure</code>.
      </td>
   </tr>
   <tr>
      <td>PATH_TO_FILE</td>
      <td>The path and filename on the target component where you want
         to save the file. For example, <br> <code>/data/stash_secure.store</code>.
      </td>
   </tr>
   <tr>
      <td>DESTINATION</td>
      <td>The path to a local directory where you want to save the file.</td>
   </tr>
</table>


The following downloads `stash_secure.store` from the target
component to the host machine:

``` none {:.devsite-disable-click-to-copy}
$ ffx component copy /core/stash_secure::/data/stash_secure.store ./stash_secure.store
```

## Upload a file to the device {:#upload-a-file-to-the-device}

To upload a file from your host machine to a Fuchsia device, run the
following command:

```
ffx component copy <SOURCE> <MONIKER>::<PATH_TO_FILE>
```

Replace the following:

<table class="responsive">
   <tr>
      <th>Argument</th>
      <th>Value</th>
   </tr>
   <tr>
      <td>SOURCE</td>
      <td>The path to a file you want to copy to the device.</td>
   </tr>
   <tr>
      <td>MONIKER</td>
      <td>The absolute moniker of your target component. For example,
         <code>/core/stash_secure</code>.
      </td>
   </tr>
   <tr>
      <td>PATH_TO_FILE</td>
      <td>The path and filename on the target component where you want
         to save the file. For example, <br> <code>/data/foo.txt</code>.
      </td>
   </tr>
</table>

The following uploads `foo.txt` from the host machine to the
target component running on the device:

``` none {:.devsite-disable-click-to-copy}
$ cd $FUCHSIA_DIR
$ LOCAL_RESOURCE_DIR=src/developer/ffx/plugins/component/copy/test-driver/resources
$ ffx component copy $LOCAL_RESOURCE_DIR/foo.txt /core/stash_secure::/data/foo.txt
```

## Copying to an isolated storage that currently does not exist {:#copy-to-non-existent-storage}

If you're trying to copy to an isolated storage that doesn't currently exist,
you need to use <br> `ffx component storage copy`. Instead of an absolute
moniker, this command uses a component's [instance id][component-id-index].<br>
By default, `ffx component storage` connects to the persistent
isolated storage `data`.

Upload a file to device:
```
ffx component storage copy <SOURCE> <INSTANCE_ID>::<PATH_TO_FILE>
```

Download a file from device:
```
ffx component storage copy <INSTANCE_ID>::<PATH_TO_FILE> <DESTINATION>
```

For the desired command, replace the following:

<table class="responsive">
   <tr>
      <th>Argument</th>
      <th>Value</th>
   </tr>
   <tr>
      <td>INSTANCE_ID</td>
      <td>An instance ID of a component to be created later. For example,
         <code>c1a6d0aebbf7c092c53e8e696636af8ec0629ff39b7f2e548430b0034d809da4</code>.
      </td>
   </tr>
   <tr>
      <td>PATH_TO_FILE</td>
      <td>The path and filename on the target component where you want
         to save the file. For example, <br> <code>/my-example.file</code>.
      </td>
   </tr>
   <tr>
      <td>SOURCE</td>
      <td>The path to a file you want to copy to the device.</td>
   </tr>
   <tr>
      <td>DESTINATION</td>
      <td>The path to a local directory where you want to save the
         file.
      </td>
   </tr>
</table>

## List all directories and files {:#list-all-directories-and-files}

To list all directories and files in a component's storage, run
the following command:

```
ffx component storage list <INSTANCE_ID>::<PATH>
```


Replace the following:

<table class="responsive">
   <tr>
      <th>Argument</th>
      <th>Value</th>
   </tr>
   <tr>
      <td>INSTANCE_ID</td>
      <td>An instance ID of a component to be created later. For example,
         <code>c1a6d0aebbf7c092c53e8e696636af8ec0629ff39b7f2e548430b0034d809da4</code>.
      </td>
   </tr>
   <tr>
      <td>PATH</td>
      <td>A path on the target component.For example, <code>/</code> or <code>/my/path/</code>.
      </td>
   </tr>
</table>

The following shows all directories and files in the root (`/`)
directory of the target component:

``` none {:.devsite-disable-click-to-copy}
$ ffx component storage list c1a6d0aebbf7c092c53e8e696636af8ec0629ff39b7f2e548430b0034d809da4::/
my-example.file
stash_secure.store
```

## Create a new directory {:#create-a-new-directory}

To create a new directory in a component's storage, run the
following command:

```
ffx component storage make-directory <INSTANCE_ID>::<NEW_PATH>
```

Replace the following:

<table class="responsive">
   <tr>
      <th>Argument</th>
      <th>Value</th>
   </tr>
   <tr>
      <td>INSTANCE_ID</td>
      <td>An instance ID of a component to be created later. For example,
         <code>c1a6d0aebbf7c092c53e8e696636af8ec0629ff39b7f2e548430b0034d809da4</code>.
      </td>
   </tr>
   <tr>
      <td>PATH</td>
      <td>The name of a new directory on the target component. For example, <code>/my-new-path</code>.
      </td>
   </tr>
</table>

The following shows creates a new directory named `my-new-path` on
the target component:

``` none {:.devsite-disable-click-to-copy}
$ ffx component storage make-directory c1a6d0aebbf7c092c53e8e696636af8ec0629ff39b7f2e548430b0034d809da4::/my-new-path
```

<!-- Reference links -->
[storage-capabilities]: /docs/concepts/components/v2/capabilities/storage.md
[ffx-component-storage]: https://fuchsia.dev/reference/tools/sdk/ffx#storage
[ffx-component-show]: ./view-component-information.md#get-detailed-information-from-a-component
[component-id-index]: /docs/development/components/component_id_index.md
[absolute-moniker]: /docs/reference/components/moniker.md#absolute
[namespace]: /docs/concepts/process/namespaces.md