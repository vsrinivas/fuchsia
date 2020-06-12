[TOC]

# test.name




## **STRUCTS**

### HandlesInTypes {#HandlesInTypes}
*Defined in [test.name/handles_in_types.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/handles_in_types.test.fidl#25)*



<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr>
            <td><code>normal_handle</code></td>
            <td>
                <code>handle&lt;vmo&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>handle_in_vec</code></td>
            <td>
                <code>vector&lt;vmo&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>handle_in_array</code></td>
            <td>
                <code>vmo[5]</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>handle_in_mixed_vec_array</code></td>
            <td>
                <code>vector&lt;array&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>table_with_handle</code></td>
            <td>
                <code><a class='link' href='#TableWithHandle'>TableWithHandle</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>union_with_handle</code></td>
            <td>
                <code><a class='link' href='#UnionWithHandle'>UnionWithHandle</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr>
</table>



## **ENUMS**

### obj_type {#obj_type}
Type: <code>uint32</code>

*Defined in [test.name/handles_in_types.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/handles_in_types.test.fidl#6)*



<table>
    <tr><th>Name</th><th>Value</th><th>Description</th></tr><tr>
            <td><code>NONE</code></td>
            <td><code>0</code></td>
            <td></td>
        </tr><tr>
            <td><code>VMO</code></td>
            <td><code>3</code></td>
            <td></td>
        </tr></table>



## **TABLES**

### TableWithHandle {#TableWithHandle}


*Defined in [test.name/handles_in_types.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/handles_in_types.test.fidl#17)*



<table>
    <tr><th>Ordinal</th><th>Name</th><th>Type</th><th>Description</th></tr>
    <tr>
            <td>1</td>
            <td><code>h</code></td>
            <td>
                <code>handle&lt;vmo&gt;</code>
            </td>
            <td></td>
        </tr></table>



## **UNIONS**

### UnionWithHandle {#UnionWithHandle}
*Defined in [test.name/handles_in_types.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/handles_in_types.test.fidl#21)*


<table>
    <tr><th>Name</th><th>Type</th><th>Description</th></tr><tr>
            <td><code>h</code></td>
            <td>
                <code>handle&lt;vmo&gt;</code>
            </td>
            <td></td>
        </tr></table>







