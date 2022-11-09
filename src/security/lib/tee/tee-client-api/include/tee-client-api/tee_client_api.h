// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SECURITY_LIB_TEE_TEE_CLIENT_API_INCLUDE_TEE_CLIENT_API_TEE_CLIENT_API_H_
#define SRC_SECURITY_LIB_TEE_TEE_CLIENT_API_INCLUDE_TEE_CLIENT_API_TEE_CLIENT_API_H_

#include <stddef.h>
#include <stdint.h>
#include <zircon/compiler.h>

#include <tee-client-api/tee-client-types.h>

__BEGIN_CDECLS

/*
 * Global Platform TEE Client API
 *
 * https://globalplatform.org/specs-library/tee-client-api-specification/
 *
 * This header contains Fuchsia's implementation of the functions, and by inclusion (#include), the
 * constants and data structures, that are defined by the Global Platform TEE Client API V1.0_c and
 * its associated Errata (V2.0).
 *
 * NOTE: The file name for this file uses underscores rather than Fuchsia's conventional dashes due
 * to the requirement from the Global Platform TEE Client API Spec that the header must be named
 * exactly 'tee_client_api.h'.
 */

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
TEEC_Result TEEC_OpenSession(TEEC_Context* context, TEEC_Session* session,
                             const TEEC_UUID* destination, uint32_t connectionMethod,
                             const void* connectionData, TEEC_Operation* operation,
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
TEEC_Result TEEC_InvokeCommand(TEEC_Session* session, uint32_t commandID, TEEC_Operation* operation,
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

#endif  // SRC_SECURITY_LIB_TEE_TEE_CLIENT_API_INCLUDE_TEE_CLIENT_API_TEE_CLIENT_API_H_
