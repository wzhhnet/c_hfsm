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

#include "allocator.h"
#include "msg_hub.h"
#include "log.h"
#include "hfsm.h"

#define HFSM_INIT_STATE_ID 0xFF
struct hfsm_sys_t {
    unsigned int what;
    int arg1;
    int arg2;
};

struct state_info_t {
    struct listnode node;
    struct state_t state;
};

ALLOCATOR_DECLARE(state, struct state_info_t);
ALLOCATOR_IMPLEMENT(state, struct state_info_t);

struct hfsm_t {
    hub_t *hub;
    hub_sub_id_t sub_id; // subscribe id for event processing
    state_id_t init_state_id;
    void *user_data;
    struct state_t *cur_state;
    struct listnode state_list;
    ALLOCATOR_DEFINE(state, pool);
};

static state_t* hfsm_find_state(struct hfsm_t *handle, state_id_t id)
{
    state_t* s = NULL;
    struct listnode *n;
    struct state_info_t *info;
    list_for_each(n, &handle->state_list) {
        info = list_entry(n, struct state_info_t, node);
        if (info->state.id == id) {
            s = &info->state;
            break;
        }
    }

    return s;
}

static void hfsm_state_transit(struct hfsm_t *handle, state_id_t id)
{
    const int MAX_LEVEL = 64;
    state_t *from[MAX_LEVEL], *to[MAX_LEVEL], *tmp, *target = NULL;
    int from_sum = 0, to_sum = 0;

    /*! find target state information */
    target = hfsm_find_state(handle, id);
    RETURN_IF_NULL(target, VOID);

    /*! fetch all parents of current state */
    tmp = handle->cur_state;
    while (tmp) {
        from[from_sum++] = tmp;
        tmp = tmp->parent;
    }

    /*! fetch all parents of target state */
    tmp = target;
    while (tmp) {
        to[to_sum++] = tmp;
        tmp = tmp->parent;
    }

    /*! remove the common tail */
    while (from_sum > 0 && to_sum > 0
        && from[from_sum-1] == to[to_sum-1]) {
        --from_sum;
        --to_sum;
    }

    /*! invoke exit action */
    for (int i=0; i<from_sum; ++i) {
        if (from[i]->action.exit) {
            from[i]->action.exit(handle->user_data);
        }
    }

    /*! invoke entry action */
    for (int i=to_sum; i>0; --i) {
        if (to[i-1]->action.entry) {
            to[i-1]->action.entry(handle->user_data);
        }
    }

    /*! state transition */
    handle->cur_state = target;
}

static void hfsm_event_invoke(const mq_msg_t *msg, void *user_data)
{
    struct hfsm_t *handle = (struct hfsm_t*)user_data;
    RETURN_IF_NULL(msg, VOID);
    RETURN_IF_NULL(user_data, VOID);
    state_t *s = handle->cur_state;
    if (msg->msg_id == HUB_MSG_ID_SUB_ACK) {
        // Start HFSM with initial state
        hfsm_state_transit(handle, handle->init_state_id);
        return;
    }
    while (s) {
        if (s->action.process) {
            state_id_t id = handle->cur_state->id;
            bool done = s->action.process(msg->msg_id, msg->param, &id);
            if (done) {
                if (id != handle->cur_state->id) {
                    hfsm_state_transit(handle, id);
                }
                break;
            }
        }
        s = s->parent;
    }
}

state_t* hfsm_new_state(hfsm_handle hfsm)
{
    struct hfsm_t *handle;
    struct state_info_t *info;

    RETURN_IF_NULL(hfsm, NULL);
    handle = (struct hfsm_t*)hfsm;

    info = ALLOCATOR_ALLOC(state, &handle->pool);
    RETURN_IF_NULL(info, NULL);
    return &info->state;
}

int hfsm_create(hfsm_handle *hfsm, hfsm_param *param, hub_t *hub)
{
    int s;
    struct hfsm_t *handle;
    RETURN_IF_NULL(hub, HFSM_ERR_NULLPTR);
    RETURN_IF_NULL(hfsm, HFSM_ERR_NULLPTR);
    RETURN_IF_NULL(param, HFSM_ERR_NULLPTR);
    handle = (struct hfsm_t*)malloc(sizeof(struct hfsm_t));
    RETURN_IF_NULL(handle, HFSM_ERR_MALLOC);

    s = ALLOCATOR_CREATE(state, &handle->pool, param->max_states);
    RETURN_IF_FAIL(s, HFSM_ERR_ALLOCATOR);

    handle->init_state_id = HFSM_INIT_STATE_ID;
    handle->user_data = param->userdata;
    handle->cur_state = NULL;
    handle->hub = hub;
    list_init(&handle->state_list);
    *hfsm = (hfsm_handle)handle;
    return HFSM_SUCC;
}

int hfsm_destroy(hfsm_handle *hfsm)
{
    struct hfsm_t *handle;
    struct listnode *c, *n;
    struct state_info_t *info;
    RETURN_IF_NULL(hfsm, HFSM_ERR_NULLPTR);
    handle = (struct hfsm_t*)(*hfsm);
    RETURN_IF_NULL(handle, HFSM_ERR_NULLPTR);

    /*! Recycled all state into pool (not necessary) */
    list_for_each_safe(c, n, &handle->state_list) {
        info = list_entry(c, struct state_info_t, node);
        list_remove(c);
        ALLOCATOR_FREE(state, &handle->pool, info);
    }

    /*! Destory allocator of state */
    ALLOCATOR_DESTORY(state, &handle->pool);
    /*! Destory evthub */
    if (handle->hub) {
        hub_unsubscribe(handle->hub, handle->sub_id);
    }
    /*! Destory hfsm */
    free(*hfsm);
    *hfsm = NULL;

    return HFSM_SUCC;
}

int hfsm_start(hfsm_handle hfsm, state_id_t id)
{
    struct hfsm_t *handle;
    RETURN_IF_NULL(hfsm, HFSM_ERR_NULLPTR);
    handle = (struct hfsm_t*)hfsm;
    handle->init_state_id = id;
    handle->sub_id = hub_subscribe(handle->hub, HUB_MSG_ID_ALL, hfsm_event_invoke, hfsm);
    RETURN_IF_TRUE(!handle->sub_id, HFSM_ERR_EVTHUB);
    return HFSM_SUCC;
}

int hfsm_add_state(hfsm_handle hfsm, state_t *s)
{
    struct hfsm_t *handle;
    struct state_info_t *info;
    RETURN_IF_NULL(s, HFSM_ERR_NULLPTR);
    RETURN_IF_NULL(hfsm, HFSM_ERR_NULLPTR);
    handle = (struct hfsm_t*)hfsm;

    info = container_of(s, struct state_info_t, state);
    RETURN_IF_NULL(info, HFSM_ERR_ALLOCATOR);
    list_add_tail(&handle->state_list, &info->node);
    return HFSM_SUCC;
}


