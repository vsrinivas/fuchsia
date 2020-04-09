[TOC]

# test.fidl.unionsandwich




## **STRUCTS**

### SandwichUnionSize8Alignment4 {#SandwichUnionSize8Alignment4}
*Defined in [test.fidl.unionsandwich/union_sandwich.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/union_sandwich.test.fidl#14)*



<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr>
            <td><code>before</code></td>
            <td>
                <code>uint32</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>union</code></td>
            <td>
                <code><a class='link' href='#UnionSize8Alignment4'>UnionSize8Alignment4</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>after</code></td>
            <td>
                <code>uint32</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr>
</table>

### SandwichUnionSize12Alignment4 {#SandwichUnionSize12Alignment4}
*Defined in [test.fidl.unionsandwich/union_sandwich.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/union_sandwich.test.fidl#24)*



<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr>
            <td><code>before</code></td>
            <td>
                <code>uint32</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>union</code></td>
            <td>
                <code><a class='link' href='#UnionSize12Alignment4'>UnionSize12Alignment4</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>after</code></td>
            <td>
                <code>int32</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr>
</table>

### StructSize16Alignment8 {#StructSize16Alignment8}
*Defined in [test.fidl.unionsandwich/union_sandwich.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/union_sandwich.test.fidl#30)*



<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr>
            <td><code>f1</code></td>
            <td>
                <code>uint64</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>f2</code></td>
            <td>
                <code>uint64</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr>
</table>

### SandwichUnionSize24Alignment8 {#SandwichUnionSize24Alignment8}
*Defined in [test.fidl.unionsandwich/union_sandwich.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/union_sandwich.test.fidl#39)*



<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr>
            <td><code>before</code></td>
            <td>
                <code>uint32</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>union</code></td>
            <td>
                <code><a class='link' href='#UnionSize24Alignment8'>UnionSize24Alignment8</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>after</code></td>
            <td>
                <code>uint32</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr>
</table>

### SandwichUnionSize36Alignment4 {#SandwichUnionSize36Alignment4}
*Defined in [test.fidl.unionsandwich/union_sandwich.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/union_sandwich.test.fidl#49)*



<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr>
            <td><code>before</code></td>
            <td>
                <code>uint32</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>union</code></td>
            <td>
                <code><a class='link' href='#UnionSize36Alignment4'>UnionSize36Alignment4</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>after</code></td>
            <td>
                <code>uint32</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr>
</table>







## **UNIONS**

### UnionSize8Alignment4 {#UnionSize8Alignment4}
*Defined in [test.fidl.unionsandwich/union_sandwich.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/union_sandwich.test.fidl#10)*


<table>
    <tr><th>Name</th><th>Type</th><th>Description</th></tr><tr>
            <td><code>variant</code></td>
            <td>
                <code>uint32</code>
            </td>
            <td></td>
        </tr></table>

### UnionSize12Alignment4 {#UnionSize12Alignment4}
*Defined in [test.fidl.unionsandwich/union_sandwich.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/union_sandwich.test.fidl#20)*


<table>
    <tr><th>Name</th><th>Type</th><th>Description</th></tr><tr>
            <td><code>variant</code></td>
            <td>
                <code>uint8[6]</code>
            </td>
            <td></td>
        </tr></table>

### UnionSize24Alignment8 {#UnionSize24Alignment8}
*Defined in [test.fidl.unionsandwich/union_sandwich.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/union_sandwich.test.fidl#35)*


<table>
    <tr><th>Name</th><th>Type</th><th>Description</th></tr><tr>
            <td><code>variant</code></td>
            <td>
                <code><a class='link' href='#StructSize16Alignment8'>StructSize16Alignment8</a></code>
            </td>
            <td></td>
        </tr></table>

### UnionSize36Alignment4 {#UnionSize36Alignment4}
*Defined in [test.fidl.unionsandwich/union_sandwich.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/union_sandwich.test.fidl#45)*


<table>
    <tr><th>Name</th><th>Type</th><th>Description</th></tr><tr>
            <td><code>variant</code></td>
            <td>
                <code>uint8[32]</code>
            </td>
            <td></td>
        </tr></table>







