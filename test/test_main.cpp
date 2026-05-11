/*
 * Unit test for HFSM
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

#include <gtest/gtest.h>

#include <unistd.h>
#include <gtest/gtest.h>
#include <hfsm.c>
#include <log.c>

hfsm_handle ghfsm = NULL;
hub_t *ghub = NULL;

enum {
    TEST_STATE_0,
    TEST_STATE_1,
    TEST_STATE_2,
};

enum {
    TEST_EVENT_AT_STATE1_STATE0,
    TEST_EVENT_TRANS_FROM_STATE1_TO_STATE2,
    TEST_EVENT_AT_STATE2_ONLY,
    TEST_EVENT_AT_STATE2_STATE0,
};

static struct evt_info_t{
    uint8_t evt_id;
    const char *info;
} test_evt_info[] = {
    {TEST_EVENT_AT_STATE1_STATE0, "TEST_EVENT_AT_STATE1_STATE0"},
    {TEST_EVENT_TRANS_FROM_STATE1_TO_STATE2, "TEST_EVENT_TRANS_FROM_STATE1_TO_STATE2"},
    {TEST_EVENT_AT_STATE2_ONLY, "TEST_EVENT_AT_STATE2_ONLY"},
    {TEST_EVENT_AT_STATE2_STATE0, "TEST_EVENT_AT_STATE2_STATE0"}
};

class GlobalTest : public testing::Environment
{
  public:
    virtual void SetUp()
    {
        hfsm_param param = {
            .max_states = 4,
            .userdata = NULL
        };
        ghub = hub_create(10);
        hfsm_create(&ghfsm, &param, ghub);
    }

    virtual void TearDown()
    {
        hfsm_destroy(&ghfsm);
        hub_destroy(ghub);
    }

};

GTEST_API_ int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    testing::Environment *env = new GlobalTest();
    testing::AddGlobalTestEnvironment(env); 
    return RUN_ALL_TESTS();
}

TEST(hfsm, hfsm_create)
{
    ASSERT_NE(ghfsm, nullptr);
}

TEST(hfsm, hfsm_new_state)
{
    state_t *s0, *s1, *s2;
    s0 = hfsm_new_state(ghfsm);
    EXPECT_NE(s0, nullptr);
    s1 = hfsm_new_state(ghfsm);
    EXPECT_NE(s1, nullptr);
    s2 = hfsm_new_state(ghfsm);
    EXPECT_NE(s2, nullptr);

    int s;
    struct state_info_t *info;
    struct hfsm_t *handle = (struct hfsm_t*)ghfsm;

    info = container_of(s0, struct state_info_t, state);
    s = ALLOCATOR_FREE(state, &handle->pool, info);
    EXPECT_EQ(s, UTILS_SUCC);

    info = container_of(s1, struct state_info_t, state);
    s = ALLOCATOR_FREE(state, &handle->pool, info);
    EXPECT_EQ(s, UTILS_SUCC);

    info = container_of(s2, struct state_info_t, state);
    s = ALLOCATOR_FREE(state, &handle->pool, info);
    EXPECT_EQ(s, UTILS_SUCC);
}

void s0_entry(void *userdata)
{
    LOGD("%s()", __FUNCTION__);
}
void s0_exit(void *userdata)
{
    LOGD("%s()", __FUNCTION__);
}
bool s0_process(msg_id_t msg_id, mq_param_t param, state_id_t* pstate)
{
    LOGD("%s() msg = %s state = %u", __FUNCTION__, test_evt_info[msg_id].info, *pstate);
    return true;
}
void s1_entry(void *userdata)
{
    LOGD("%s()", __FUNCTION__);
}
void s1_exit(void *userdata)
{
    LOGD("%s()", __FUNCTION__);
}
bool s1_process(msg_id_t msg_id, mq_param_t param, state_id_t* pstate)
{
    LOGD("%s() msg = %s state = %u", __FUNCTION__, test_evt_info[msg_id].info, *pstate);
    if (msg_id == TEST_EVENT_TRANS_FROM_STATE1_TO_STATE2) {
        LOGD("%s() try to transit to state[%lu]", __FUNCTION__, TEST_STATE_2);
        *pstate = TEST_STATE_2;
        return true;
    }

    return false;
}
void s2_entry(void *userdata)
{
    LOGD("%s()", __FUNCTION__);
}
void s2_exit(void *userdata)
{
    LOGD("%s()", __FUNCTION__);
}
bool s2_process(msg_id_t msg_id, mq_param_t param, state_id_t* pstate)
{
    LOGD("%s() msg = %s state = %u", __FUNCTION__, test_evt_info[msg_id].info, *pstate);
    if (param.ptr) {
        return false;   //need parent processing
    } else {
        return true;
    }
}

TEST(hfsm, hfsm_add_state)
{
    int s;
    state_t *s0, *s1, *s2;
    s0 = hfsm_new_state(ghfsm);
    ASSERT_NE(s0, nullptr);
    s1 = hfsm_new_state(ghfsm);
    ASSERT_NE(s1, nullptr);
    s2 = hfsm_new_state(ghfsm);
    ASSERT_NE(s2, nullptr);

    s0->id = TEST_STATE_0;
    s0->parent = NULL;
    s0->action.entry = s0_entry;
    s0->action.exit = s0_exit;
    s0->action.process = s0_process;
    s = hfsm_add_state(ghfsm, s0);
    EXPECT_EQ(s, HFSM_SUCC);

    s1->id = TEST_STATE_1;
    s1->parent = s0;
    s1->action.entry = s1_entry;
    s1->action.exit = s1_exit;
    s1->action.process = s1_process;
    s = hfsm_add_state(ghfsm, s1);
    EXPECT_EQ(s, HFSM_SUCC);

    s2->id = TEST_STATE_2;
    s2->parent = s0;
    s2->action.entry = s2_entry;
    s2->action.exit = s2_exit;
    s2->action.process = s2_process;
    s = hfsm_add_state(ghfsm, s2);
    EXPECT_EQ(s, HFSM_SUCC);
}

TEST(hfsm, hfsm_start)
{
    int s = hfsm_start(ghfsm, TEST_STATE_1);
    EXPECT_EQ(s, HFSM_SUCC);
}

TEST(hfsm, hub_publish)
{
    mq_param_t param = {};
    mq_err_t s = hub_publish(ghub, TEST_EVENT_AT_STATE1_STATE0, param, MQ_PRIO_HIGH);
    EXPECT_EQ(s, MQ_OK);

    s = hub_publish(ghub, TEST_EVENT_TRANS_FROM_STATE1_TO_STATE2, param, MQ_PRIO_HIGH);
    EXPECT_EQ(s, MQ_OK);

    s = hub_publish(ghub, TEST_EVENT_AT_STATE2_ONLY, param, MQ_PRIO_HIGH);
    EXPECT_EQ(s, MQ_OK);

    param.ptr = (void*)1;
    s = hub_publish(ghub, TEST_EVENT_AT_STATE2_STATE0, param, MQ_PRIO_HIGH);
    EXPECT_EQ(s, MQ_OK);
    
}

TEST(hfsm, hfsm_destroy)
{
    usleep(1000); // wait for other cases compeleted
    int s = hfsm_destroy(&ghfsm);
    EXPECT_EQ(s, HFSM_SUCC);
    EXPECT_EQ(ghfsm, nullptr);
}
