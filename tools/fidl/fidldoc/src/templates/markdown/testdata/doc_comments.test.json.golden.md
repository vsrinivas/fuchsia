[TOC]

# test.name

<p>library comment #1</p>
<p>library comment #2</p>

## **PROTOCOLS**

## Interface {#Interface}
*Defined in [test.name/doc_comments.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/doc_comments.test.fidl#112)*

<p>interface comment #1</p>
<p>interface comment #3</p>

### Method {#test.name/Interface.Method}

<p>method comment #1</p>
<p>method comment #3</p>

#### Request
<table>
    <tr><th>Name</th><th>Type</th></tr>
    </table>



### OnEvent {#test.name/Interface.OnEvent}

<p>event comment #1</p>
<p>event comment #3</p>



#### Response
<table>
    <tr><th>Name</th><th>Type</th></tr>
    </table>



## **STRUCTS**

### Struct {#Struct}
*Defined in [test.name/doc_comments.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/doc_comments.test.fidl#72)*

<p>struct comment #1</p>
<p>struct comment #3</p>


<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr id="Struct.Field">
            <td><code>Field</code></td>
            <td>
                <code>int32</code>
            </td>
            <td><p>struct member comment #1</p>
<p>struct member comment #3</p>
</td>
            <td>No default</td>
        </tr>
</table>



## **ENUMS**

### MyStrictEnum {#MyStrictEnum}
Type: <code>uint32</code>

*Defined in [test.name/doc_comments.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/doc_comments.test.fidl#42)*

<p>strict enum comment #1.</p>
<p>strict enum comment #2.</p>


<table>
    <tr><th>Name</th><th>Value</th><th>Description</th></tr><tr id="MyStrictEnum.FOO">
            <td><code>FOO</code></td>
            <td><code>1</code></td>
            <td><p>FOO member comment #1</p>
<p>FOO member comment #3</p>
</td>
        </tr><tr id="MyStrictEnum.BAR">
            <td><code>BAR</code></td>
            <td><code>2</code></td>
            <td><p>BAR member comment #1</p>
<p>BAR member comment #3</p>
</td>
        </tr></table>

### MyFlexibleEnum {#MyFlexibleEnum}
Type: <code>uint32</code>

*Defined in [test.name/doc_comments.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/doc_comments.test.fidl#57)*

<p>flexible enum comment #1.</p>
<p>flexible enum comment #2.</p>


<table>
    <tr><th>Name</th><th>Value</th><th>Description</th></tr><tr id="MyFlexibleEnum.FOO">
            <td><code>FOO</code></td>
            <td><code>1</code></td>
            <td><p>FOO member comment #1</p>
<p>FOO member comment #3</p>
</td>
        </tr><tr id="MyFlexibleEnum.BAR">
            <td><code>BAR</code></td>
            <td><code>2</code></td>
            <td><p>BAR member comment #1</p>
<p>BAR member comment #3</p>
</td>
        </tr></table>



## **TABLES**

### Table {#Table}


*Defined in [test.name/doc_comments.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/doc_comments.test.fidl#102)*

<p>table comment #1</p>
<p>table comment #3</p>


<table>
    <tr><th>Ordinal</th><th>Name</th><th>Type</th><th>Description</th></tr>
    <tr id="Table.Field">
            <td>1</td>
            <td><code>Field</code></td>
            <td>
                <code>int32</code>
            </td>
            <td><p>table field comment #1</p>
<p>table field comment #3</p>
</td>
        </tr></table>



## **UNIONS**

### StrictUnion {#StrictUnion}
*Defined in [test.name/doc_comments.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/doc_comments.test.fidl#82)*

<p>strict union comment #1</p>
<p>strict union comment #3</p>

<table>
    <tr><th>Name</th><th>Type</th><th>Description</th></tr><tr id="StrictUnion.Field">
            <td><code>Field</code></td>
            <td>
                <code>int32</code>
            </td>
            <td><p>union member comment #1</p>
<p>union member comment #3</p>
</td>
        </tr></table>

### FlexibleUnion {#FlexibleUnion}
*Defined in [test.name/doc_comments.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/doc_comments.test.fidl#92)*

<p>flexible union comment #1</p>
<p>flexible union comment #3</p>

<table>
    <tr><th>Name</th><th>Type</th><th>Description</th></tr><tr id="FlexibleUnion.Field">
            <td><code>Field</code></td>
            <td>
                <code>int32</code>
            </td>
            <td><p>union member comment #1</p>
<p>union member comment #3</p>
</td>
        </tr></table>



## **BITS**

### MyStrictBits {#MyStrictBits}
Type: <code>uint32</code>

*Defined in [test.name/doc_comments.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/doc_comments.test.fidl#9)*

<p>strict bits comment #1</p>
<p>strict bits comment #2</p>


<table>
    <tr><th>Name</th><th>Value</th><th>Description</th></tr><tr id="MyStrictBits.MY_FIRST_BIT">
            <td>MY_FIRST_BIT</td>
            <td>1</td>
            <td><p>MY_FIRST_BIT member comment #1</p>
<p>MY_FIRST_BIT member comment #3</p>
</td>
        </tr><tr id="MyStrictBits.MY_OTHER_BIT">
            <td>MY_OTHER_BIT</td>
            <td>2</td>
            <td><p>MY_OTHER_BIT member comment #1</p>
<p>MY_OTHER_BIT member comment #3</p>
</td>
        </tr></table>

### MyFlexibleBits {#MyFlexibleBits}
Type: <code>uint32</code>

*Defined in [test.name/doc_comments.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/doc_comments.test.fidl#23)*

<p>flexible bits comment #1</p>
<p>flexible bits comment #2</p>


<table>
    <tr><th>Name</th><th>Value</th><th>Description</th></tr><tr id="MyFlexibleBits.MY_FIRST_BIT">
            <td>MY_FIRST_BIT</td>
            <td>1</td>
            <td><p>MY_FIRST_BIT member comment #1</p>
<p>MY_FIRST_BIT member comment #3</p>
</td>
        </tr><tr id="MyFlexibleBits.MY_OTHER_BIT">
            <td>MY_OTHER_BIT</td>
            <td>2</td>
            <td><p>MY_OTHER_BIT member comment #1</p>
<p>MY_OTHER_BIT member comment #3</p>
</td>
        </tr></table>



## **CONSTANTS**

<table>
    <tr><th>Name</th><th>Value</th><th>Type</th><th>Description</th></tr><tr id="C">
            <td><a href="https://fuchsia.googlesource.com/fuchsia/+/master/doc_comments.test.fidl#37">C</a></td>
            <td>
                    <code>4</code>
                </td>
                <td><code>int32</code></td>
            <td><p>const comment #1</p>
<p>const comment #3</p>
</td>
        </tr>
    
</table>



