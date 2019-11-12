
## **XUNIONS**

### DisplaySettings {#DisplaySettings}
*Defined in [fuchsia.diagnostics.inspect/inspect.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/sdk/fidl/fuchsia.diagnostics.inspect/inspect.fidl#22)*

<p>Criteria for how to format
the selected Inspect data.</p>

<table>
    <tr><th>Name</th><th>Type</th><th>Description</th></tr><tr>
            <td><code>json</code></td>
            <td>
                <code><a class='link' href='#JsonSettings'>JsonSettings</a></code>
            </td>
            <td></td>
        </tr><tr>
            <td><code>text</code></td>
            <td>
                <code><a class='link' href='#TextSettings'>TextSettings</a></code>
            </td>
            <td></td>
        </tr></table>

### ReaderSelector {#ReaderSelector}
*Defined in [fuchsia.diagnostics.inspect/inspect.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/sdk/fidl/fuchsia.diagnostics.inspect/inspect.fidl#44)*

<p>Selection criteria for data returned by the Reader service.</p>

<table>
    <tr><th>Name</th><th>Type</th><th>Description</th></tr><tr>
            <td><code>structured_selector</code></td>
            <td>
                <code><a class='link' href='#Selector'>Selector</a></code>
            </td>
            <td><p>The reader applies the selection defined
by structured_selector to all possible inspect data that it
has access to, returning a potential subset, but not superset,
of what would be returned by selection using only the system
configuration.</p>
</td>
        </tr><tr>
            <td><code>string_selector</code></td>
            <td>
                <code>string[1024]</code>
            </td>
            <td><p>The reader parses the string-based selector
string_selector into a structured selector and then will apply
the selection defined by structured_selector to all possible inspect
data that it has access to, returning a potential subset, but
not a superset of what would be returned by selection using only the
system configuration.</p>
</td>
        </tr></table>

### PropertySelector {#PropertySelector}
*Defined in [fuchsia.diagnostics.inspect/selector.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/sdk/fidl/fuchsia.diagnostics.inspect/selector.fidl#36)*


<table>
    <tr><th>Name</th><th>Type</th><th>Description</th></tr><tr>
            <td><code>string_pattern</code></td>
            <td>
                <code>string[1024]</code>
            </td>
            <td></td>
        </tr><tr>
            <td><code>wildcard</code></td>
            <td>
                <code>bool</code>
            </td>
            <td></td>
        </tr></table>
