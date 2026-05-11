/*
 * State interface for C users
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

#ifndef _HFSM_STATE_H
#define _HFSM_STATE_H

//#include "event_hub.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char state_id_t;
typedef unsigned long msg_id_t;
typedef struct action_t {
    /**
     *    @brief entering state callback
     *    @param[in]  userdata: user data
     *    @return     none
     */
    void (*entry)(void* /*!< userdata */);
    /**
     *    @brief exiting state callback
     *    @param[in]  userdata: user data
     *    @return     none
     */
    void (*exit)(void* /*!< userdata */);
    /**
     *    @brief event processing callback in the state
     *    @param[in]  msg_id: message identifier
     *    @param[in]  userdata: user data
     *    @param[out] next_state: transiting target state id
     *    @return     true if processing completed
                      false if event need parent state processing yet.
     */
    bool (*process)(msg_id_t /*!< msg id */,
        mq_param_t /*!< msg parameter */, state_id_t* /*!< next state id */);
} action_t;

typedef struct state_t {
    state_id_t id;                /*!< State identifier */
    struct state_t *parent;     /*!< Parent state */
    struct action_t action;     /*!< Parent action callback */
} state_t;


#ifdef __cplusplus
}
#endif

#endif /*! _HFSM_STATE_H */
