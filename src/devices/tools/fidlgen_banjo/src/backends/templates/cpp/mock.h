
// This class mocks a device by providing a {protocol_name_snake}_protocol_t.
// Users can set expectations on how the protocol ops are called and what values they return. After
// the test, use VerifyAndClear to reset the object and verify that all expectations were satisfied.
// See the following example test:
//
// ddk::Mock{protocol_name} {protocol_name_snake};
//
// /* Set some expectations on the device by calling {protocol_name_snake}.Expect... methods. */
//
// SomeDriver dut({protocol_name_snake}.GetProto());
//
// EXPECT_OK(dut.SomeMethod());
// ASSERT_NO_FATAL_FAILURES({protocol_name_snake}.VerifyAndClear());
//
// Note that users must provide the equality operator for struct types, for example:
// bool operator==(const a_struct_type& lhs, const a_struct_type& rhs)

class Mock{protocol_name} : ddk::{protocol_name}Protocol<Mock{protocol_name}> {{
public:
    Mock{protocol_name}() : proto_{{&{protocol_name_snake}_protocol_ops_, this}} {{}}

    virtual ~Mock{protocol_name}() {{}}

    const {protocol_name_snake}_protocol_t* GetProto() const {{ return &proto_; }}
{mock_expects}
    void VerifyAndClear() {{
{mock_verify}
    }}
{protocol_definitions}
{mock_accessors}

protected:
{mock_definitions}

private:
    const {protocol_name_snake}_protocol_t proto_;
}};
