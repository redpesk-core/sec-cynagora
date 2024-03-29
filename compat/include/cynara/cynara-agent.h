/*
 *  Copyright (c) 2014-2017 Samsung Electronics Co., Ltd All Rights Reserved
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License
 */
/**
 * @file        src/include/cynara-agent.h
 * @author      Adam Malinowski <a.malinowsk2@partner.samsung.com>
 * @author      Oskar Switalski <o.switalski@samsung.com>
 * @version     1.0
 * @brief       This file contains agent APIs available with libcynara-agent.
 */

#ifndef CYNARA_AGENT_H
#define CYNARA_AGENT_H

#include <stdint.h>

#include <cynara/cynara-error.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t cynara_agent_req_id;
typedef struct cynara_agent cynara_agent;

/**
 * \enum cynara_agent_msg_type
 * Values specifying type of message.
 *
 * \var cynara_agent_msg_type::CYNARA_MSG_TYPE_ACTION
 * Message of this type indicates its content is a request for performing an action or response to
 * such request.
 *
 * \var cynara_agent_msg_type::CYNARA_MSG_TYPE_CANCEL
 * Message of this type indicates its content is a request for canceling action or response to such
 * request.
 */
typedef enum {
    CYNARA_MSG_TYPE_ACTION,
    CYNARA_MSG_TYPE_CANCEL
} cynara_agent_msg_type;

/**
 * \par Description:
 * Initialize cynara-agent structure.
 * Create structure used in following API calls.
 *
 * \par Purpose:
 * This API must be used prior to calling other agent API functions.
 *
 * \par Typical use case:
 * Once before other agent API functions are called.
 *
 * \par Method of function operation:
 * This API initializes inner library structures and in case of success returns cynara_agent
 * structure.
 *
 * \par Sync (or) Async:
 * This is a synchronous API.
 *
 * \par Thread safety:
 * This function is NOT thread safe. If functions from described API are called by multithreaded
 * application from different threads, they must be put into protected critical section.
 *
 * \par Important notes:
 * Structure cynara_agent created by this function should be released with cynara_agent_finish().
 *
 * \param[out] pp_cynara_agent Place holder for created cynara_agent structure.
 * \param[in]  p_agent_type Type (name) of agent used by cynara for communication agent<->plugin.
 *
 * \return CYNARA_API_SUCCESS on success, or error code on error.
 */
int cynara_agent_initialize(cynara_agent **pp_cynara_agent, const char *p_agent_type);

/**
 * \par Description:
 * Destroy structure created with cynara_agent_initialize().
 *
 * \par Purpose:
 * This API should be used to clean up after usage of cynara-agent library.
 *
 * \par Typical use case:
 * Once after connection to cynara is not needed.
 *
 * \par Method of function operation:
 * This API releases inner library structure and destroys cynara_agent structure. Connection to
 * cynara service is closed.
 *
 * \par Sync (or) Async:
 * This is a synchronous API.
 *
 * \par Thread safety:
 * This function is NOT thread-safe. If functions from described API are called by multithreaded
 * application from different threads, they must be put into protected critical section.
 *
 * \par Important notes:
 * No other call to cynara-agent library should be made after call to cynara_agent_finish() except
 * cynara_agent_initialize().
 *
 * \param[in] p_cynara_agent cynara_agent structure. If NULL, then the call has no effect.
 */
int cynara_agent_finish(cynara_agent *p_cynara_agent);

/**
 * \par Description:
 * Get request from cynara service.
 *
 * \par Purpose:
 * This API should be used to get request from cynara service. Request is generated by corresponding
 * plugin loaded into cynara service.
 *
 * \par Typical use case:
 * Agent waits for request from cynara service. Request may be either ask for performing agent
 * specific action or ask for canceling such action. Agent calls this function when is ready to
 * perform or cancel an action.
 *
 * \par Method of function operation:
 * \parblock
 * Function reads data incoming from cynara service and if at least one complete request is ready
 * then returns with CYNARA_API_SUCCESS code. Request type, request id and specific
 * plugin data are stored in given arguments. Function returns exactly one request. If there are
 * more then one requests ready to get then one must call this function multiple times.
 *
 * This function is blocking which means that if there is no request from cynara service it will not
 * return. On success, buffer for plugin specific data is allocated and size is set. Developer is
 * responsible for freeing this memory using free() function.
 * \endparblock
 *
 * \par Sync (or) Async:
 * This is a synchronous API.
 *
 * \par Thread safety:
 * This function is NOT thread safe. If functions from described API are called by multithreaded
 * application from different threads, they must be put into protected critical section.
 *
 * \par Important notes:
 * \parblock
 * Call to cynara_agent_get_request() needs cynara_agent structure to be created first.
 * Use cynara_agent_initialize() before calling this function.
 *
 * After CYNARA_API_ACCESS_DENIED error is returned agent should be terminated or at least should
 * not invoke neither cynara_agent_get_request() nor cynara_agent_put_response() functions.
 * \endparblock
 *
 * \param[in]  p_cynara_agent cynara_agent structure.
 * \param[out] req_type Request type, demand an action or cancel this action.
 * \param[out] req_id Request identifier used to pair with answer #cynara_agent_put_response() and
 *                    cancel request.
 * \param[out] data Plugin specific data. Buffer is allocated in this function and developer is
 *                  responsible for freeing it using free() function. Buffer is filled with data
 *                  from corresponding plugin. If there is no enough memory for data
 *                  CYNARA_API_OUT_OF_MEMORY is returned and all arguments remain untouched.
 * \param[out] data_size Size of plugin data (bytes count). In case of out of memory this value
 *                  stays untouched.
 *
 * \return CYNARA_API_SUCCESS on successfully read request,
 *         CYNARA_API_INTERRUPTED when cynara_agent_cancel_waiting() is called during waiting,
 *         or negative error code otherwise.
 */
int cynara_agent_get_request(cynara_agent *p_cynara_agent, cynara_agent_msg_type *req_type,
                             cynara_agent_req_id *req_id, void **data, size_t *data_size);

/**
 * \par Description:
 * Send response to cynara service.
 *
 * \par Purpose:
 * This API should be used to send response to cynara service.
 *
 * \par Typical use case:
 * Agent calls this function when is ready to answer request for action or cancel request.
 *
 * \par Method of function operation:
 * Function sends data to cynara service. Data contains answer for previously got question.
 * Answer may be of type CYNARA_MSG_TYPE_ACTION or CYNARA_MSG_TYPE_CANCEL. Type is
 * CYNARA_MSG_TYPE_ACTION when request for an action was processed and answer is ready, or
 * CYNARA_MSG_TYPE_CANCEL when processing request for an action was interrupted by cancel request.
 * Agent must send exactly one response per one request and cancel. If request is processed before
 * cancel message arrives the agent sends action response. If cancel arrives before action request
 * is processed then agent sends cancel response and drops processing related action.
 * Request id in response must be the same as request id in corresponding request.
 *
 * \par Sync (or) Async:
 * This is a synchronous API.
 *
 * \par Thread safety:
 * This function is NOT thread safe. If functions from described API are called by multithreaded
 * application from different threads, they must be put into protected critical section.
 *
 * \par Important notes:
 * Call to cynara_agent_get_request() needs cynara_agent structure to be created first.
 * Use cynara_agent_initialize() before calling this function. Also successful call to
 * cynara_agent_get_request() is needed before calling this function.
 *
 * \param[in] p_cynara_agent cynara_agent structure.
 * \param[in] resp_type Response type - see Method of operation for details.
 * \param[in] req_id Request identifier obtained from request.
 * \param[in] data Plugin specific data. If necessary agent should fill this buffer with data
 *                 directed to plugin.
 * \param[in] data_size Size of plugin data (bytes count).
 *
 * \return CYNARA_API_SUCCESS on successfully read request, or negative error code otherwise.
 */
int cynara_agent_put_response(cynara_agent *p_cynara_agent, const cynara_agent_msg_type resp_type,
                              const cynara_agent_req_id req_id, const void *data,
                              const size_t data_size);

/**
 * \par Description:
 * Break from waiting for cynara service request using cynara_agent_get_request().
 *
 * \par Purpose:
 * This API should be used when cynara_agent_get_request() is blocked and before calling
 * cynara_agent_finish().
 *
 * \par Typical use case:
 * Agent calls this API, when it wants to gracefully quit.
 *
 * \par Method of function operation:
 * Function notifies cynara_agent_get_request() to stop waiting for request from cynara.
 * Then cynara_agent_get_request() returns CYNARA_API_INTERRUPTED and no request is fetched.
 *
 * \par Sync (or) Async:
 * This is a synchronous API.
 *
 * \par Thread safety:
 * This function can be used only together with cynara_agent_get_request(), otherwise should
 * be treaded as NOT thread safe.
 *
 * \par Important notes:
 * Call to cynara_agent_cancel_waiting() needs cynara_agent structure to be created first and
 * cynara_agent_get_request() running.
 * Use cynara_agent_initialize() before calling this function.
 *
 * \param[in] p_cynara_agent cynara_agent structure.
 * \return CYNARA_API_SUCCESS on successful waiting cancel, or negative error code otherwise.
 */
int cynara_agent_cancel_waiting(cynara_agent *p_cynara_agent);

#ifdef __cplusplus
}
#endif

#endif /* CYNARA_AGENT_H */
