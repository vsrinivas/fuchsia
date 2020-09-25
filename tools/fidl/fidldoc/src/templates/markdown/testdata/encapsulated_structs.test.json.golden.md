[TOC]

# fidl.test.encapsulatedstructs




## **STRUCTS**

### Int8Int32 {#Int8Int32}
*Defined in [fidl.test.encapsulatedstructs/encapsulated_structs.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/encapsulated_structs.test.fidl#3)*



<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr id="Int8Int32.a">
            <td><code>a</code></td>
            <td>
                <code>int8</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Int8Int32.b">
            <td><code>b</code></td>
            <td>
                <code>int32</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr>
</table>

### Int16Int8 {#Int16Int8}
*Defined in [fidl.test.encapsulatedstructs/encapsulated_structs.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/encapsulated_structs.test.fidl#9)*



<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr id="Int16Int8.a">
            <td><code>a</code></td>
            <td>
                <code>int16</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Int16Int8.b">
            <td><code>b</code></td>
            <td>
                <code>int8</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr>
</table>

### ArrayInt16Int8 {#ArrayInt16Int8}
*Defined in [fidl.test.encapsulatedstructs/encapsulated_structs.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/encapsulated_structs.test.fidl#15)*



<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr id="ArrayInt16Int8.arr">
            <td><code>arr</code></td>
            <td>
                <code>[3]</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr>
</table>

### StructPaddingTestStruct {#StructPaddingTestStruct}
*Defined in [fidl.test.encapsulatedstructs/encapsulated_structs.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/encapsulated_structs.test.fidl#20)*



<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr id="StructPaddingTestStruct.trailing">
            <td><code>trailing</code></td>
            <td>
                <code><a class='link' href='#Int16Int8'>Int16Int8</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="StructPaddingTestStruct.inner">
            <td><code>inner</code></td>
            <td>
                <code><a class='link' href='#Int8Int32'>Int8Int32</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="StructPaddingTestStruct.array">
            <td><code>array</code></td>
            <td>
                <code><a class='link' href='#ArrayInt16Int8'>ArrayInt16Int8</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr>
</table>

### NonInlineStructTestStruct {#NonInlineStructTestStruct}
*Defined in [fidl.test.encapsulatedstructs/encapsulated_structs.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/encapsulated_structs.test.fidl#31)*



<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr id="NonInlineStructTestStruct.element">
            <td><code>element</code></td>
            <td>
                <code><a class='link' href='#Int16Int8'>Int16Int8</a>?</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="NonInlineStructTestStruct.h">
            <td><code>h</code></td>
            <td>
                <code>handle&lt;handle&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr>
</table>

### TopLevelStruct {#TopLevelStruct}
*Defined in [fidl.test.encapsulatedstructs/encapsulated_structs.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/encapsulated_structs.test.fidl#37)*



<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr id="TopLevelStruct.a">
            <td><code>a</code></td>
            <td>
                <code><a class='link' href='#StructPaddingTestStruct'>StructPaddingTestStruct</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="TopLevelStruct.b">
            <td><code>b</code></td>
            <td>
                <code><a class='link' href='#NonInlineStructTestStruct'>NonInlineStructTestStruct</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr>
</table>













