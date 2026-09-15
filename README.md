# ESP8266-NodeMCU WiFi SoftAP LED 演示程序

基于 **ESP8266 RTOS SDK**（ESP12F 模组）的 C 演示程序：上电后进入 WiFi **SoftAP（AP）** 模式，通过**板载 LED** 直观反映系统运行状态与客户端连接状态。

## 功能行为

| 系统状态 | LED 行为 |
|---|---|
| 上电初始化完成，AP 已启动，等待设备连接 | 每 3 秒闪烁一次（亮 100ms / 灭 2900ms） |
| 有设备正在连接（探测到关联请求） | 快速闪烁（亮 100ms / 灭 100ms） |
| 至少一个设备连接成功（已关联） | LED 常亮 |
| 所有已连接设备断开 | 恢复为每 3 秒闪烁一次 |

> 说明：ESP8266 的 WiFi 驱动未暴露"正在连接"的完整中间态回调。本程序通过 **`WIFI_EVENT_AP_PROBEREQRECVED`（探针请求）** 作为"有设备正在连接"的近似信号，进入快闪；若宽限时间内未完成关联则回退到"等待"状态；一旦 `AP_STACONNECTED` 触发即进入常亮。因此实际观感为：连接瞬间 LED 由"慢闪"短暂经过"快闪"后变"常亮"。

## 硬件要求

- 开发板：NodeMCU 系列（内置 **ESP8266-ESP12F** 模组）
- 板载 LED：接至 **GPIO2 (D4)**，**低电平点亮（active-low）**
  - 若你的板子 LED 引脚不同，请修改配置项 `AP_LED_GPIO`（见下文）。
- USB 转串口芯片（板上已集成）用于烧录

> 无需额外接线，板载 LED 即可工作。

## 开发环境

- ESP8266 RTOS SDK（本仓库默认安装于 `/root/esp/ESP8266_RTOS_SDK`）
- 加载环境：

```bash
source /root/esp/ESP8266_RTOS_SDK/export.sh
```

## 构建

支持 **CMake（`idf.py`）** 与 **Make（`make`）** 两种方式。

### 方式一：CMake / idf.py（推荐）

```bash
source /root/esp/ESP8266_RTOS_SDK/export.sh
cd <本仓库根目录>
idf.py build
```

### 方式二：Make

```bash
source /root/esp/ESP8266_RTOS_SDK/export.sh
cd <本仓库根目录>
make defconfig   # 生成默认配置（纯 Python，无需 ncurses）
make             # 编译
```

> 注：
> - `make defconfig` 生成默认 `sdkconfig`；如需交互修改配置可用 `make menuconfig`（需要系统安装 ncurses 库）。
> - 在 Docker overlay 文件系统环境下，make 的子进程递归可能报权限错误，此时以绝对路径 make 调用即可规避：`make MAKE=/usr/bin/make`。

构建产物位于 `build/`：
- `build/wifi_softAP.bin` — 应用固件
- `build/wifi_softAP.elf` — 含调试符号
- `build/bootloader/bootloader.bin` — 引导加载器
- `build/partitions_singleapp.bin` — 分区表

## 烧录

```bash
# idf.py 方式
idf.py flash monitor

# 或 make 方式
make flash monitor
```

串口默认为 `/dev/ttyUSB0`，可用 `--port`（idf.py）或 `make flash PORT=/dev/ttyUSB1` 指定。

## 默认 AP 配置

| 配置项 | 默认值 |
|---|---|
| SSID | `ESP8266-Demo` |
| 密码 | `12345678` |
| 最大连接数 | `4` |
| LED 引脚 | `GPIO2` |

手机/电脑搜索到 SSID `ESP8266-Demo` 并输入密码即可连接。

## 配置项（Kconfig）

配置项定义在 `main/Kconfig.projbuild`，可通过 `make menuconfig` 或编辑 `sdkconfig` 修改：

| 宏 | 说明 | 默认 |
|---|---|---|
| `CONFIG_AP_WIFI_SSID` | AP 名称（SSID） | `ESP8266-Demo` |
| `CONFIG_AP_WIFI_PASSWORD` | AP 密码（8~32 位，WPA2） | `12345678` |
| `CONFIG_AP_MAX_CONN` | 最大同时连接设备数 | `4` |
| `CONFIG_AP_LED_GPIO` | LED 引脚号 | `2` |

修改后重新执行 `make defconfig`（或 `idf.py build`）即可生效。

## 项目结构

```
.
├── CMakeLists.txt            # 顶层 CMake（idf.py build 入口）
├── Makefile                  # 顶层 Make（make 入口）
├── main/
│   ├── CMakeLists.txt        # main 组件（CMake 注册）
│   ├── component.mk          # main 组件（Make 注册）
│   ├── Kconfig               # main 组件 Kconfig 占位
│   ├── Kconfig.projbuild     # 可配置项（SSID/密码/最大连接数/LED 引脚）
│   ├── main.c                # 入口：NVS → LED → SoftAP 初始化
│   ├── led_fsm.h / .c        # LED 闪烁时序状态机（纯逻辑，可 host 测试）
│   ├── ap_logic.h / .c       # 连接计数 + LED 状态决策（纯逻辑，可 host 测试）
│   ├── led_control.h / .c    # GPIO 初始化 + 自驱闪烁任务（固件薄层）
│   └── wifi_ap.h / .c        # SoftAP 初始化 + WiFi 事件处理（固件薄层）
└── tests/host/
    ├── test_util.h           # 轻量自包含测试框架
    ├── test_led_fsm.c        # led_fsm 单元测试（8 用例）
    ├── test_ap_logic.c       # ap_logic 单元测试（8 用例）
    └── run_tests.sh          # 一键编译运行测试 + 覆盖率统计
```

## 设计说明

采用 **HAL 分离 + 纯逻辑可测** 架构，将"业务决策"与"硬件操作"解耦：

```
┌─────────────────────────────────────────────────────────┐
│  固件薄层（依赖 ESP / FreeRTOS）                          │
│   wifi_ap.c   led_control.c      main.c                 │
│   (esp_wifi+事件) (GPIO+任务)        (入口/装配)          │
└──────────────┬──────────────────────────┬────────────────┘
               │ 调用                      │ 调用
┌──────────────▼──────────────────────────▼────────────────┐
│  纯逻辑层（无 ESP/RTOS 依赖，可 host 单元测试）             │
│   ap_logic.c              led_fsm.c                      │
│   (连接计数+状态决策)       (三态闪烁时序)                 │
└──────────────────────────────────────────────────────────┘
```

- **`ap_logic`**：维护"已连接 STA 计数"，根据事件返回目标 LED 状态（启动→IDLE，连接→CONNECTED，全部断开→IDLE，探测且无连接→CONNECTING）。
- **`led_fsm`**：封装三态闪烁时序，通过 `led_hal_t` 回调注入 GPIO 操作，`advance()` 返回本阶段持续时长。
- **`led_control`**：FreeRTOS 任务驱动 `led_fsm`，通过 `EventGroup` 实现线程安全的"状态变更即时唤醒"（切换响应 < 500ms）。
- **`wifi_ap`**：`esp_wifi` SoftAP 初始化与事件处理；探针请求事件触发"连接中"快闪，并以软定时器宽限回退。

## 单元测试

纯逻辑层（`led_fsm`、`ap_logic`）在 host 端用 gcc + gcov 测试，无需 ESP 工具链：

```bash
bash tests/host/run_tests.sh
```

- 用例覆盖：三态闪烁时序、状态切换相位重置、单/多设备连接断开、探测快闪、宽限超时回退、完整生命周期、NULL 安全等。
- 行覆盖率：`led_fsm.c` ≈ 97.5%，`ap_logic.c` = 100%（均远超 70% 要求）。

## 验收自测

1. `make`（或 `idf.py build`）编译，C 代码 **0 warning**。
2. 烧录后上电，搜索到 SSID `ESP8266-Demo`，LED **每 3 秒闪一次**。
3. 连接该热点，**连接瞬间 LED 变常亮**。
4. 断开连接，**LED 恢复每 3 秒闪烁**。
5. 多设备同时连接，全部断开后 LED 恢复闪烁。

## 备注

- 任务范围外的功能（TCP/IP 服务、HTTP/MQTT、OTA、传感器、低功耗）不在本程序实现。
- 仅生成并保留**源码与可复现构建脚本**，不提交编译二进制与测试产物（见 `.gitignore`）。
