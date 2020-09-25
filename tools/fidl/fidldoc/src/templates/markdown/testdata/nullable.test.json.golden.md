[TOC]

# fidl.test.nullable


## **PROTOCOLS**

## SimpleProtocol {#SimpleProtocol}
*Defined in [fidl.test.nullable/nullable.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/nullable.test.fidl#17)*


### Add {#fidl.test.nullable/SimpleProtocol.Add}


#### Request
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>a</code></td>
            <td>
                <code>int32</code>
            </td>
        </tr><tr>
            <td><code>b</code></td>
            <td>
                <code>int32</code>
            </td>
        </tr></table>


#### Response
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>sum</code></td>
            <td>
                <code>int32</code>
            </td>
        </tr></table>



## **STRUCTS**

### StructWithNullableString {#StructWithNullableString}
*Defined in [fidl.test.nullable/nullable.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/nullable.test.fidl#5)*



<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr id="StructWithNullableString.val">
            <td><code>val</code></td>
            <td>
                <code>string?</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr>
</table>

### StructWithNullableVector {#StructWithNullableVector}
*Defined in [fidl.test.nullable/nullable.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/nullable.test.fidl#9)*



<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr id="StructWithNullableVector.val">
            <td><code>val</code></td>
            <td>
                <code>vector&lt;int32&gt;?</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr>
</table>

### StructWithNullableHandle {#StructWithNullableHandle}
*Defined in [fidl.test.nullable/nullable.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/nullable.test.fidl#13)*



<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr id="StructWithNullableHandle.val">
            <td><code>val</code></td>
            <td>
                <code>handle&lt;vmo&gt;?</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr>
</table>

### StructWithNullableProtocol {#StructWithNullableProtocol}
*Defined in [fidl.test.nullable/nullable.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/nullable.test.fidl#21)*



<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr id="StructWithNullableProtocol.val">
            <td><code>val</code></td>
            <td>
                <code><a class='link' href='#SimpleProtocol'>SimpleProtocol</a>?</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr>
</table>

### StructWithNullableRequest {#StructWithNullableRequest}
*Defined in [fidl.test.nullable/nullable.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/nullable.test.fidl#25)*



<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr id="StructWithNullableRequest.val">
            <td><code>val</code></td>
            <td>
                <code>request&lt;<a class='link' href='#SimpleProtocol'>SimpleProtocol</a>&gt;?</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr>
</table>

### Int32Wrapper {#Int32Wrapper}
*Defined in [fidl.test.nullable/nullable.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/nullable.test.fidl#29)*



<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr id="Int32Wrapper.val">
            <td><code>val</code></td>
            <td>
                <code>int32</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr>
</table>

### StructWithNullableStruct {#StructWithNullableStruct}
*Defined in [fidl.test.nullable/nullable.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/nullable.test.fidl#33)*



<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr id="StructWithNullableStruct.val">
            <td><code>val</code></td>
            <td>
                <code><a class='link' href='#Int32Wrapper'>Int32Wrapper</a>?</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr>
</table>

### StructWithNullableUnion {#StructWithNullableUnion}
*Defined in [fidl.test.nullable/nullable.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/nullable.test.fidl#42)*



<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr id="StructWithNullableUnion.val">
            <td><code>val</code></td>
            <td>
                <code><a class='link' href='#SimpleUnion'>SimpleUnion</a>?</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr>
</table>







## **UNIONS**

### SimpleUnion {#SimpleUnion}
*Defined in [fidl.test.nullable/nullable.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/nullable.test.fidl#37)*


<table>
    <tr><th>Name</th><th>Type</th><th>Description</th></tr><tr id="SimpleUnion.a">
            <td><code>a</code></td>
            <td>
                <code>int32</code>
            </td>
            <td></td>
        </tr><tr id="SimpleUnion.b">
            <td><code>b</code></td>
            <td>
                <code>float32</code>
            </td>
            <td></td>
        </tr></table>







