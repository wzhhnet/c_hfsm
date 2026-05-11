# HFSM - Hierarchical Finite State Machine

HFSM（层次有限状态机）是一个用C语言实现的轻量级库，用于管理复杂的状态机逻辑。它支持层次状态结构，允许子状态继承父状态的行为，并通过消息队列和发布/订阅机制实现事件驱动的状态转换。

## 特性

- **层次状态支持**: 状态可以有父子关系，子状态可以继承父状态的事件处理
- **事件驱动**: 基于消息队列的事件处理，支持优先级消息
- **线程安全**: 使用内部调度线程处理事件，避免阻塞主线程
- **轻量级**: 无外部依赖，纯C实现，支持C++包装
- **可扩展**: 支持自定义用户数据和回调函数

## 架构

HFSM由以下核心组件组成：

### 消息队列 (msg_queue)
- 基于最大堆实现的优先级队列
- 支持阻塞/超时/非阻塞出队
- 线程安全，使用pthread mutex和条件变量

### 消息Hub (msg_hub)
- 发布/订阅模式的消息分发器
- 内部单调度线程消费消息队列并分发到订阅者
- 支持精确订阅和通配符订阅
- 保证订阅确认消息的有序性

### 状态机 (hfsm)
- 管理状态层次结构
- 处理状态转换逻辑
- 订阅所有消息并按层次查找事件处理器

## 使用方式

### 1. 初始化

```c
#include "hfsm.h"
#include "msg_hub.h"

// 创建消息Hub
hub_t *hub = hub_create(100); // 队列容量

// 配置HFSM参数
hfsm_param param = {
    .max_states = 10,      // 最大状态数
    .userdata = my_data    // 用户数据
};

// 创建HFSM实例
hfsm_handle hfsm;
hfsm_create(&hfsm, &param, hub);
```

### 2. 定义状态

```c
// 定义状态回调函数
void state_entry(void *userdata) {
    printf("Entering state\n");
}

void state_exit(void *userdata) {
    printf("Exiting state\n");
}

bool state_process(msg_id_t msg_id, mq_param_t param, state_id_t *next_state) {
    switch (msg_id) {
        case EVENT_TRANSIT:
            *next_state = NEXT_STATE_ID;
            return true; // 处理完成
        default:
            return false; // 让父状态处理
    }
}

// 创建状态
state_t *state = hfsm_new_state(hfsm);
state->id = STATE_ID;
state->parent = parent_state; // 可为NULL
state->action.entry = state_entry;
state->action.exit = state_exit;
state->action.process = state_process;

// 添加状态到HFSM
hfsm_add_state(hfsm, state);
```

### 3. 启动状态机

```c
// 启动HFSM，指定初始状态
hfsm_start(hfsm, INITIAL_STATE_ID);
```

### 4. 发送事件

```c
// 发布消息触发状态转换
mq_param_t param = {.value = 123};
hub_publish(hub, EVENT_ID, param, MQ_PRIO_HIGH);
```

### 5. 清理

```c
// 销毁HFSM和Hub
hfsm_destroy(&hfsm);
hub_destroy(hub);
```

## 事件处理流程

1. 事件通过`hub_publish()`发布到消息队列
2. Hub的调度线程消费消息并分发给订阅者
3. HFSM收到消息后，从当前状态开始向上遍历状态层次
4. 每个状态的`process`回调被调用处理事件
5. 如果`process`返回`true`，表示事件已处理
6. 如果`next_state`与当前状态不同，执行状态转换
7. 状态转换时，先执行退出动作（从当前状态到共同祖先），再执行进入动作（从共同祖先到目标状态）

## API 参考

### HFSM 接口

- `hfsm_create()`: 创建HFSM实例
- `hfsm_destroy()`: 销毁HFSM实例
- `hfsm_start()`: 启动HFSM并设置初始状态
- `hfsm_add_state()`: 添加状态到HFSM
- `hfsm_new_state()`: 分配新的状态对象

### 消息Hub接口

- `hub_create()`: 创建消息Hub
- `hub_destroy()`: 销毁消息Hub
- `hub_subscribe()`: 订阅消息
- `hub_unsubscribe()`: 取消订阅
- `hub_publish()`: 发布消息
- `hub_pending()`: 获取待处理消息数量

### 消息队列接口

- `mq_create()`: 创建消息队列
- `mq_destroy()`: 销毁消息队列
- `mq_push()`: 入队消息
- `mq_pop()`: 出队消息

## 构建和测试

### 依赖

- CMake >= 3.28
- GCC 或 Clang 编译器
- pthread 库

### 构建

```bash
mkdir build
cd build
cmake ..
make
```

### 运行测试

```bash
# 构建测试可执行文件
make hfsm-unit

# 运行单元测试
./hfsm-unit
```

### 安装

```bash
make install
```

## 示例

完整的使用示例请参考 `test/test_main.cpp` 中的单元测试代码。

## 许可证

本项目采用 Apache License 2.0 许可证。详见 [LICENSE](LICENSE) 文件。
