// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <zircon/compiler.h>

__BEGIN_CDECLS

/*
 * Global Platform TEE Client API
 *
 * https://globalplatform.org/specs-library/tee-client-api-specification/
 *
 * This header contains Fuchsia's implementation of the constants, data structures and functions
 * that are defined by the Global Platform TEE Client API V1.0_c and it's associated Errata (V2.0).
 *
 * NOTE: The file name for this file uses underscores rather than Fuchsia's conventional dashes due
 * to the requirement from the Global Platform TEE Client API Spec that the header must be named
 * exactly 'tee_client_api.h'.
 */

/*************
 * Constants *
 *************/

/* Configuration Settings
 *
 * Shared Memory Maximum Size
 *      The maximum size of a single Shared Memory block, in bytes, of both API allocated and API
 *      registered memory. This version of the standard requires that this maximum size is greater
 *      than or equal to 512kB. In systems where there is no limit imposed by the Implementation
 *      then this definition should be defined to be the size of the address space.
 */
#define TEEC_CONFIG_SHAREDMEM_MAX_SIZE UINT64_MAX

/* Return Codes
 *
 * TEEC_SUCCESS                 The operation was successful.
 * TEEC_ERROR_GENERIC           Non-specific cause.
 * TEEC_ERROR_ACCESS_DENIED     Access privileges are not sufficient.
 * TEEC_ERROR_CANCEL            The operation was cancelled.
 * TEEC_ERROR_ACCESS_CONFLICT   Concurrent accesses caused conflict.
 * TEEC_ERROR_EXCESS_DATA       Too much data for the requested operation was passed.
 * TEEC_ERROR_BAD_FORMAT        Input data was of invalid format.
 * TEEC_ERROR_BAD_PARAMETERS    Input parameters were invalid.
 * TEEC_ERROR_BAD_STATE         Operation is not valid in the current state.
 * TEEC_ERROR_ITEM_NOT_FOUND    The requested data item is not found.
 * TEEC_ERROR_NOT_IMPLEMENTED   The requested operation should exist but is not yet implemented.
 * TEEC_ERROR_NOT_SUPPORTED     The requested operation is valid but is not supported in this
 *                              Implementation.
 * TEEC_ERROR_NO_DATA           Expected data was missing.
 * TEEC_ERROR_OUT_OF_MEMORY     System ran out of resources.
 * TEEC_ERROR_BUSY              The system is busy working on something else.
 * TEEC_ERROR_COMMUNICATION     Communication with a remote party failed.
 * TEEC_ERROR_SECURITY          A security fault was detected.
 * TEEC_ERROR_SHORT_BUFFER      The supplied buffer is too short for the generated output.
 * TEE_ERROR_EXTERNAL_CANCEL    An external event has caused a User Interface operation to be
 *                              aborted.
 * TEE_ERROR_OVERFLOW           Internal TEE error.
 * TEE_ERROR_TARGET_DEAD        The Trusted Application has terminated.
 * TEEC_ERROR_TARGET_DEAD       The Trusted Application has terminated.
 * TEE_ERROR_STORAGE_NO_SPACE   Internal TEE error.
 */
#define TEEC_SUCCESS 0x00000000
#define TEEC_ERROR_GENERIC 0xFFFF0000
#define TEEC_ERROR_ACCESS_DENIED 0xFFFF0001
#define TEEC_ERROR_CANCEL 0xFFFF0002
#define TEEC_ERROR_ACCESS_CONFLICT 0xFFFF0003
#define TEEC_ERROR_EXCESS_DATA 0xFFFF0004
#define TEEC_ERROR_BAD_FORMAT 0xFFFF0005
#define TEEC_ERROR_BAD_PARAMETERS 0xFFFF0006
#define TEEC_ERROR_BAD_STATE 0xFFFF0007
#define TEEC_ERROR_ITEM_NOT_FOUND 0xFFFF0008
#define TEEC_ERROR_NOT_IMPLEMENTED 0xFFFF0009
#define TEEC_ERROR_NOT_SUPPORTED 0xFFFF000A
#define TEEC_ERROR_NO_DATA 0xFFFF000B
#define TEEC_ERROR_OUT_OF_MEMORY 0xFFFF000C
#define TEEC_ERROR_BUSY 0xFFFF000D
#define TEEC_ERROR_COMMUNICATION 0xFFFF000E
#define TEEC_ERROR_SECURITY 0xFFFF000F
#define TEEC_ERROR_SHORT_BUFFER 0xFFFF0010
#define TEE_ERROR_EXTERNAL_CANCEL 0xFFFF0011
#define TEE_ERROR_OVERFLOW 0xFFFF300F
#define TEE_ERROR_TARGET_DEAD 0xFFFF3024
#define TEEC_ERROR_TARGET_DEAD 0xFFFF3024
#define TEE_ERROR_STORAGE_NO_SPACE 0xFFFF3041

/* Return Code Origins
 *
 * These indicate where in the software stack the return code was generated for an open session
 * operation or an invoke-command operation.
 *
 * TEEC_ORIGIN_API          The return code is an error that originated within the TEE Client API
 *                          implementation.
 * TEEC_ORIGIN_COMMS        The return code is an error that originated within the underlying
 *                          communications stack linking the rich OS with the TEE.
 * TEEC_ORIGIN_TEE          The return code is an error that originated within the common TEE code.
 * TEEC_ORIGIN_TRUSTED_APP  The return code originated within the Trusted Application code. This
 *                          includes the case where the return code is a success.
 */
#define TEEC_ORIGIN_API 0x00000001
#define TEEC_ORIGIN_COMMS 0x00000002
#define TEEC_ORIGIN_TEE 0x00000003
#define TEEC_ORIGIN_TRUSTED_APP 0x00000004

/* Shared Memory Control - TEEC_MEM_*
 *
 * These are used to indicate the current status and synchronization requirements of Shared Memory
 * blocks.
 *
 * TEEC_MEM_INPUT   The Shared Memory can carry data from the Client Application to the Trusted
 *                  Application.
 * TEEC_MEM_OUTPUT  The Shared Memory can carry data from the Trusted Application to the Client
 *                  Application.
 */
#define TEEC_MEM_INPUT 0x00000001
#define TEEC_MEM_OUTPUT 0x00000002

/* Shared Memory Control - TEEC_VALUE_*
 *
 * These are used to indicate the type of Parameter encoded inside the operation structure.
 *
 * TEEC_NONE                    The Parameter is not used.
 * TEEC_VALUE_INPUT             The Parameter is a TEEC_Value tagged as input.
 * TEEC_VALUE_OUTPUT            The Parameter is a TEEC_Value tagged as output.
 * TEEC_VALUE_INOUT             The Parameter is a TEEC_Value tagged as both as input and output,
 *                              i.e., for which both the behaviors of TEEC_VALUE_INPUT and
 *                              TEEC_VALUE_OUTPUT apply.
 * TEEC_MEMREF_TEMP_INPUT       The Parameter is a TEEC_TemporaryMemoryReference describing a region
 *                              of memory which needs to be temporarily registered for the duration
 *                              of the Operation and is tagged as input.
 * TEEC_MEMREF_TEMP_OUTPUT      Same as TEEC_MEMREF_TEMP_INPUT, but the Memory Reference is tagged
 *                              as output. The Implementation may update the size field to reflect
 *                              the required output size in some use cases.
 * TEEC_MEMREF_TEMP_INOUT       A Temporary Memory Reference tagged as both input and output, i.e.,
 *                              for which both the behaviors of TEEC_MEMREF_TEMP_INPUT and
 *                              TEEC_MEMREF_TEMP_OUTPUT apply.
 * TEEC_MEMREF_WHOLE            The Parameter is a Registered Memory Reference that refers to the
 *                              entirety of its parent Shared Memory block. The parameter structure
 *                              is a TEEC_RegisteredMemoryReference. In this structure, the
 *                              Implemetnation must read only the parent field and may update the
 *                              size field when the operation completes.
 * TEEC_MEMREF_PARTIAL_INPUT    A Registered Memory Reference structure that refers to a partial
 *                              region of its parent Shared Memory block and is tagged as input.
 * TEEC_MEMREF_PARTIAL_OUTPUT   A Registered Memory Reference structure that refers to a partial
 *                              region of its parent Shared Memory block and is tagged as output.
 * TEEC_MEMREF_PARTIAL_INOUT    The Registered Memory Reference structure that refers to a partial
 *                              region of its parent Shared Memory block and is tagged as both
 *                              input and output, i.e., for which both the behaviors of
 *                              TEEC_MEMREF_PARTIAL_INPUT and TEEC_MEMREF_PARTIAL_OUTPUT apply.
 */
#define TEEC_NONE 0x00000000
#define TEEC_VALUE_INPUT 0x00000001
#define TEEC_VALUE_OUTPUT 0x00000002
#define TEEC_VALUE_INOUT 0x00000003
#define TEEC_MEMREF_TEMP_INPUT 0x00000005
#define TEEC_MEMREF_TEMP_OUTPUT 0x00000006
#define TEEC_MEMREF_TEMP_INOUT 0x00000007
#define TEEC_MEMREF_WHOLE 0x0000000C
#define TEEC_MEMREF_PARTIAL_INPUT 0x0000000D
#define TEEC_MEMREF_PARTIAL_OUTPUT 0x0000000E
#define TEEC_MEMREF_PARTIAL_INOUT 0x0000000F

/* Session Login Methods
 *
 * These are used to indicate what identity credentials about the Client Application are used by
 * the Implementation to determine access control permissions to functionality provided by, or data
 * stored by, the Trusted Application.
 *
 * Login types are designed to be orthogonal from reach other, in accordance with the identity
 * token(s) defined for each constant. For example, the credentials generated for
 * TEEC_LOGIN_APPLICATION must only depend on the identity of the application program, and not the
 * user running it. If two users use the same program, the Implementation must assign the same
 * login identity to both users so that they can access the same assets held inside the TEE. These
 * identity tokens must also be persistent within one Implementation, across multiple invocations of
 * the application and across power cycles, enabling them to be used to disambiguate persistent
 * storage. Note that this specification does not guarantee separation based on use of different
 * login types - in many embedded platforms there is no notion of "group" or "user" so these login
 * types may fall back to TEEC_LOGIN_PUBLIC - these details of generating the credential for each
 * login type are implementation-defined.
 *
 * TEEC_LOGIN_PUBLIC            No login data is provided.
 * TEEC_LOGIN_USER              Login data about the user running the Client Application process is
 *                              provided.
 * TEEC_LOGIN_GROUP             Login data about the group running the Client Application process is
 *                              provided.
 * TEEC_LOGIN_APPLICATION       Login data about the running Client Application itself is provided.
 * TEEC_LOGIN_USER_APPLICATION  Login data about the user running the Client Application and about
 *                              the Client Application itself is provided.
 * TEEC_LOGIN_GROUP_APPLICATION Login data about the group running the Client Application and about
 *                              the Client Application itself is provided.
 */
#define TEEC_LOGIN_PUBLIC 0x00000000
#define TEEC_LOGIN_USER 0x00000001
#define TEEC_LOGIN_GROUP 0x00000002
#define TEEC_LOGIN_APPLICATION 0x00000004
#define TEEC_LOGIN_USER_APPLICATION 0x00000005
#define TEEC_LOGIN_GROUP_APPLICATION 0x00000006

/**********
 * Macros *
 **********/

/* TEEC_PARAM_TYPES
 *
 * This function-like macro builds a constant containing four Parameter types for use in the
 * paramTypes field of a TEEC_Operation structure.
 */
#define TEEC_PARAM_TYPES(param0Type, param1Type, param2Type, param3Type) \
    (((param0Type & 0xF) << 0) | ((param1Type & 0xF) << 4) |             \
     ((param2Type & 0xF) << 8) | ((param3Type & 0xF) << 12))

/**************
 * Data Types *
 **************/

/* TEEC_Result
 *
 * This type is used to contain return codes which are the results of invoking TEE Client API
 * functions.
 */
typedef uint32_t TEEC_Result;

/* TEEC_UUID
 *
 * This type contains a Universally Unique Resource Identifier (UUID) type as defined in RFC4122.
 * These UUID values are used to identify Trusted Applications.
 */
typedef struct {
    uint32_t timeLow;
    uint16_t timeMid;
    uint16_t timeHiAndVersion;
    uint8_t clockSeqAndNode[8];
} TEEC_UUID;

/* TEEC_Context
 *
 * This type denotes a TEE Context, the main logical container linking a Client Application with a
 * particular TEE. Its content is entirely implementation-defined.
 */
typedef struct {
    void* imp;
} TEEC_Context;

/* TEEC_Session
 *
 * This type denotes a TEE Session, the logical container linking a Client Application with a
 * particular Trusted Application. Its content is entirely implementation-defined.
 */
typedef struct {
    void* imp;
} TEEC_Session;

/* TEEC_SharedMemory
 *
 * This type denotes a Shared Memory block which has either been registered with the Implementation
 * or allocated by it.
 *
 * Fields:
 * buffer   A pointer to the memory buffer shared with the TEE.
 *
 * size     The size of the memory buffer, in bytes.
 *
 * flags    A bit-vector which can contain the following flags:
 *              TEEC_MEM_INPUT: The memory can be used to transfer data from the Client Application
 *                              to the TEE.
 *              TEEC_MEM_OUTPUT: The memory can be used to transfer data from the TEE to the Client
 *                               Application.
 *              All other bits in this field should be set to zero, and are reserved for future use.
 *
 * imp      Contains any additional implementation-defined data attached to the Shared Memory
 *          structure.
 */
typedef struct {
    void* buffer;
    size_t size;
    uint32_t flags;
    void* imp;
} TEEC_SharedMemory;

/* TEEC_TempMemoryReference
 *
 * This type defines a Temporary Memory Reference. It is used as a TEEC_Operation parameter when
 * the corresponding parameter type is one of TEEC_MEMREF_TEMP_INPUT, TEEC_MEMREF_TEMP_OUTPUT, or
 * TEEC_MEMREF_TEMP_INOUT.
 *
 * Fields:
 * buffer   A pointer to the first byte of a region of memory which needs to be temporarily
 *          registered for the duration of the Operation. This field can be NULL to specify a null
 *          Memory Reference.
 *
 * size     The size of the referenced memory region, in bytes.
 */
typedef struct {
    void* buffer;
    size_t size;
} TEEC_TempMemoryReference;

/* TEEC_RegisteredMemoryReference
 *
 * This type defines a Registered Memory Reference, i.e., that uses a pre-registered or
 * pre-allocated Shared Memory block. It is used as a TEEC_Operation parameter when the
 * corresponding parameter type is one of TEEC_MEMREF_WHOLE, TEEC_MEMREF_PARTIAL_INPUT,
 * TEEC_MEMREF_PARTIAL_OUTPUT, or TEEC_MEMREF_PARTIAL_INOUT.
 *
 * Fields:
 * parent   A pointer to a TEEC_SharedMemory structure. The memory reference refers either to the
 *          whole Shared Memory or to a partial region within the Shared Memory block, depending on
 *          the parameter type. The data flow direction of the memory reference must be consistent
 *          with the flags defined in the parent Shared Memory Block. Note that the parent field
 *          must not be NULL. To encode a null Memory Reference, the Client Application must use a
 *          Temporary Memory Reference with the buffer field set to NULL.
 *
 * size     The size of the referenced memory region, in bytes.
 *
 * offset   The offset, in bytes, of the referenced memory region from the start of the Shared
 *          Memory block.
 */
typedef struct {
    TEEC_SharedMemory* parent;
    size_t size;
    size_t offset;
} TEEC_RegisteredMemoryReference;

/* TEEC_Value
 *
 * This type defines a parameter that is not referencing shared memory, but carries instead small
 * raw data passed by value. It is used as a TEEC_Operation parameter when the corresponding
 * parameter type is one of TEEC_VALUE_INPUT, TEEC_VALUE_OUTPUT, or TEEC_VALUE_INOUT.
 *
 * The two fields of this structure do not have a particular meaning. It is up to the protocol
 * between the Client Application and the Trusted Application to assign a semantic to those two
 * integers.
 */
typedef struct {
    uint32_t a;
    uint32_t b;
} TEEC_Value;

/* TEEC_Parameter
 *
 * This type defines a Parameter of a TEEC_Operation. It can be a Temporary Memory Reference, a
 * Registered Memory Reference or a Value Parameter. The field to select in this union depends on
 * the type of the parameter specified in the paramTypes field of the TEEC_Operation structure.
 */
typedef union {
    TEEC_TempMemoryReference tmpref;
    TEEC_RegisteredMemoryReference memref;
    TEEC_Value value;
} TEEC_Parameter;

/* TEEC_Operation
 *
 * This type defines the payload of either an open Session operation or an invoke Command operation.
 * It is also used for cancellation of operations, which may be desirable even if no payload is
 * passed.
 *
 * Fields:
 * started      A field which must be initialized to zero by the Client Application before each use
 *              in an operation if the Client Application may need to cancel the operation about to
 *              be performed.
 *
 * paramTypes   Encodes the type of each of the Parameters in the operation. The layout of these
 *              types within a 32-bit integer is implementation-defined and the Client Application
 *              must use the macro TEEC_PARAM_TYPES to construct a constant value for this field. As
 *              a special case, if the Client Application sets paramTypes to 0, then the
 *              Implementation must interpret it as a meaning that the type of each Parameter is set
 *              to TEEC_NONE.
 *
 * params       An array of four parameters. For each parameter, one of the memref, tmpref or value
 *              fields must be used depending on the corresponding parameter type passed in
 *              paramTypes as described in the specification of TEEC_Parameter.
 *
 * imp          Contains any additional implementation-defined data attached to the operation
 *              structure.
 */
typedef struct {
    uint32_t started;
    uint32_t paramTypes;
    TEEC_Parameter params[4];
    void* imp;
} TEEC_Operation;

/*************
 * Functions *
 *************/

/* TEEC_InitializeContext
 *
 * This function initializes a new TEE Context, forming a connection between this Client Application
 * and the TEE identified by the string identifier name.
 *
 * Parameters:
 * name     A zero-terminated string that describes the TEE to connect to. If this parameter is set
 *          to NULL the Implementation must select a default TEE.
 *
 * context  A TEEC_Context structre that must be initialized by the Implementation.
 *
 * Return:
 * TEEC_Result  TEEC_SUCCESS: Initialization was successful.
 *              Another error code: Initialization was not successful.
 */
TEEC_Result TEEC_InitializeContext(const char* name, TEEC_Context* context);

/* TEEC_FinalizeContext
 *
 * This function finalizes an initialized TEE Context, closing the connection between the Client
 * Application and the TEE. The Client Application MUST only call this function when all Sessions
 * inside this TEE Context have been closed and all Shared Memory blocks have been released.
 *
 * Parameters:
 * context  An initialized TEEC_Context structure which is to be finalized.
 */
void TEEC_FinalizeContext(TEEC_Context* context);

/* TEEC_RegisterSharedMemory
 *
 * This function registers a block of existing Client Application memory as a block of Shared Memory
 * within the scope of the specified TEE Context, in accordance with the parameters which have been
 * set by the Client Application inside the sharedMem structure.
 *
 * Parameters:
 * context      A pointer to an initialized TEE Context.
 *
 * sharedMem    A pointer to a Shared Memory structure to register:
 *                  The buffer, size, and flags fields of the sharedMem structure MUST be set in
 *                  accordance with the specification described above.
 *
 * Return:
 * TEEC_Result  TEEC_SUCCESS: The registration was successful.
 *              TEEC_ERROR_OUT_OF_MEMORY: The registration could not be completed because of a lack
 *                  of resources.
 *              Another error code: Registration was not successful for another reason.
 */
TEEC_Result TEEC_RegisterSharedMemory(TEEC_Context* context, TEEC_SharedMemory* sharedMem);

/* TEEC_AllocateSharedMemory
 *
 * This function allocates a new block of memory as a block of Shared Memory within the scope of the
 * specified TEE Context, in accordance with the parameters which have been set by the Client
 * Application inside the sharedMem structure.
 *
 * Parameters:
 * context      A pointer to an initialized TEE Context.
 *
 * sharedMem    A pointer to a Shared Memory structure to allocate:
 *                  * Before calling this function, the Client Application MUST have set the size,
 *                    and flags fields.
 *                  * On return, for a successful allocation the Implementation MUST have set the
 *                    pointer buffer to the address of the allocated block, otherwise it MUST set
 *                    buffer to NULL.
 *
 * Return:
 * TEEC_Result  TEEC_SUCCESS: The allocation was successful.
 *              TEEC_ERROR_OUT_OF_MEMORY: The allocation could not be completed due to resource
 *                  constraints.
 *              Another error code: Allocation was not successful for another reason.
 */
TEEC_Result TEEC_AllocateSharedMemory(TEEC_Context* context, TEEC_SharedMemory* sharedMem);

/* TEEC_ReleaseSharedMemory
 *
 * This function deregisters or deallocates a previously initialized block of Shared Memory.
 *
 * Parameters:
 * sharedMem    A pointer to a valid Shared Memory structure.
 */
void TEEC_ReleaseSharedMemory(TEEC_SharedMemory* sharedMem);

/* TEEC_OpenSession
 *
 * This function opens a new Session between the Client Application and the specified Trusted
 * Application.
 *
 * Parameters:
 * context          A pointer to an initialized TEE Context.
 *
 * session          A pointer to a Session structure to open.
 *
 * destination      A pointer to a structure containing the UUID of the destination Trusted
 *                  Application.
 *
 * connectionMethod The method of connection to use.
 *
 * connectionData   Any necessary data required to support the connection method chosen.
 *
 * operation        A pointer to an Operation containing a set of Parameters to exchange with the
 *                  Trusted Application, or NULL if no parameters are to be exchanged or if the
 *                  operation cannot be cancelled.
 *
 * returnOrigin     A pointer to a variable which will contain the return origin. This field may be
 *                  NULL if the return origin is not needed.
 *
 * Return:
 * TEEC_Result  * If the returnOrigin is different from TEEC_ORIGIN_TRUSTED_APP, a TEEC Return Code.
 *              * If the returnOrigin is equal to TEEC_ORIGIN_TRUSTED_APP, a return code defined by
 *                the protocol between the Client Application and the Trusted Application. In any
 *                case, a return code set to TEEC_SUCCESS means that the session was successfully
 *                opened and a return code different from TEEC_SUCCESS means that the session
 *                opening failed.
 */
TEEC_Result TEEC_OpenSession(TEEC_Context* context,
                             TEEC_Session* session,
                             const TEEC_UUID* destination,
                             uint32_t connectionMethod,
                             const void* connectionData,
                             TEEC_Operation* operation,
                             uint32_t* returnOrigin);

/* TEEC_CloseSession
 *
 * This function closes a Session which has been opened with a Trusted Application.
 *
 * Parameters:
 * session  The session to close.
 */
void TEEC_CloseSession(TEEC_Session* session);

/* TEEC_InvokeCommand
 *
 * This function invokes a Command within the specified Session.
 *
 * Parameters:
 * session      The open Session in which the command will be invoked.
 * commandID    The identifier of the Command within the Trusted Application to invoke. The meaning
 *              of each Command Identifier must be defined in the protocol exposed by the Trusted
 *              Application.
 * operation    A pointer to a Client Application initialized TEEC_Operation structure, or NULL if
 *              there is no payload to send or if the Command does not need to support cancellation.
 * returnOrigin A pointer to a variable which will contain the return origin. This field may be NULL
 *              if the return origin is not needed.
 *
 * Return:
 * TEEC_Result  * If the returnOrigin is different from TEEC_ORIGIN_TRUSTED_APP, a TEEC Return Code.
 *              * If the returnOrigin is TEEC_ORIGIN_TRUSTED_APP, a return code defined by the
 *                Trusted Application protocol.
 */
TEEC_Result TEEC_InvokeCommand(TEEC_Session* session,
                               uint32_t commandID,
                               TEEC_Operation* operation,
                               uint32_t* returnOrigin);

/* TEEC_RequestCancellation
 *
 * This function requests the cancellation of a pending open Session operation or a Command
 * invocation operation. As this is a synchronous API, this function must be called from a thread
 * other than the one executing the TEEC_OpenSession or TEEC_InvokeCommand function.
 *
 * Parameters:
 * operation    A pointer to a Client Application instantiated Operation structure.
 */
void TEEC_RequestCancellation(TEEC_Operation* operation);

__END_CDECLS
