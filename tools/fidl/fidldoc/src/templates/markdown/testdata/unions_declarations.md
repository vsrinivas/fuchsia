
## **UNIONS**

### StoreAccessor_Flush_Result {#StoreAccessor_Flush_Result}
*generated*


<table>
    <tr><th>Name</th><th>Type</th><th>Description</th></tr><tr>
            <td><code>response</code></td>
            <td>
                <code><a class='link' href='#StoreAccessor_Flush_Response'>StoreAccessor_Flush_Response</a></code>
            </td>
            <td></td>
        </tr><tr>
            <td><code>err</code></td>
            <td>
                <code><a class='link' href='#FlushError'>FlushError</a></code>
            </td>
            <td></td>
        </tr></table>

### Value {#Value}
*Defined in [fuchsia.stash/stash.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/sdk/fidl/fuchsia.stash/stash.fidl#35)*

<p>Value holds a value for a given key.</p>

<table>
    <tr><th>Name</th><th>Type</th><th>Description</th></tr><tr>
            <td><code>intval</code></td>
            <td>
                <code>int64</code>
            </td>
            <td></td>
        </tr><tr>
            <td><code>floatval</code></td>
            <td>
                <code>float64</code>
            </td>
            <td></td>
        </tr><tr>
            <td><code>boolval</code></td>
            <td>
                <code>bool</code>
            </td>
            <td></td>
        </tr><tr>
            <td><code>stringval</code></td>
            <td>
                <code>string[12000]</code>
            </td>
            <td></td>
        </tr><tr>
            <td><code>bytesval</code></td>
            <td>
                <code><a class='link' href='../fuchsia.mem/'>fuchsia.mem</a>/<a class='link' href='../fuchsia.mem/#Buffer'>Buffer</a></code>
            </td>
            <td></td>
        </tr></table>
