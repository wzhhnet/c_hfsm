/*
 * Hierarchical finite state machine
 *
 * Author wanch
 * Date 2021/12/23
 * Email wzhhnet@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef HIERARCHICAL_FINITE_STATE_MACHINE_H
#define HIERARCHICAL_FINITE_STATE_MACHINE_H

#include "state.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define HFSM_EVENT_USR_BASE EVENT_ID_USER_BASE

enum error_code {
    HFSM_SUCC           = UTILS_SUCC,
    HFSM_ERR_MALLOC     = UTILS_ERR_MALLOC,
    HFSM_ERR_NULLPTR    = UTILS_ERR_PTR,
    HFSM_ERR_ALLOCATOR,
    HFSM_ERR_NO_STATE,
    HFSM_ERR_EVTHUB,
};

typedef void* hfsm_handle;

typedef struct {
    unsigned char max_states;
    void *userdata;
} hfsm_param;

/**
  *    @brief create HFSM
  *
  *    create HFSM
  *    @param[in]  param: attribute of HFSM
  *    @param[in]  hub: msg hub for HFSM event processiing
  *    @param[out] hfsm: point of FHSM handle
  *    @return     0 success, non-zero error code
  */
int hfsm_create(hfsm_handle *hfsm, hfsm_param *param, hub_t *hub);

/**
  *    @brief destroy HFSM
  *           unprocessed events will be discarded.
  *    destroy HFSM
  *    @param[in]  hfsm: point of FHSM handle
  *    @return     0 success, non-zero error code
  */
int hfsm_destroy(hfsm_handle *hfsm);

/**
  *    @brief start HFSM
  *
  *    start HFSM
  *    @param[in]  hfsm handle
  *    @param[in]  id initial state identifier
  *    @return     0 success, non-zero error code
  */
int hfsm_start(hfsm_handle hfsm, state_id_t id);

/**
  *    @brief add state
  *
  *    add state for HFSM
  *    @param[in]  hfsm: point of FHSM handle
  *    @param[in]  state: state to add
  *    @param[in]  parent: parent of state to add
  *    @return     0 success, non-zero error code
  */
int hfsm_add_state(hfsm_handle hfsm, state_t *s);

/**
  *    @brief allocate a new state by HFSM
  *
  *    allocate a new state by HFSM
  *    @param[in]  hfsm: point of FHSM handle
  *    @return     pointer of state_t, NULL if failed
  */
state_t* hfsm_new_state(hfsm_handle hfsm);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif

