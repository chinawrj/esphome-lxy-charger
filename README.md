# LXY BLE Charger · ESPHome

基于 **ESPHome 2026.9.0 + ESP-IDF** 的 LXY BLE 充电器控制器。BLE 与事件总线是必选核心，LCD、实体按键、LED、独立网页是四个可选模块。使用网页时不需要 Home Assistant 服务器；不选网页时，BLE 仍可独立连接并读取参数。

项目支持已验证的 `FFF0` 服务、`FFF2` 写入、`FFF1` 通知协议。相同品牌或设备名称不代表协议一定兼容。当前读取与显示的电压、电流都是**充电设定值**，尚未实现实际输出测量、温度解析或充电输出开关。

## 选择配置

| 入口 | 硬件 | 默认模块 |
|---|---|---|
| [esp32-minimal.yaml](esp32-minimal.yaml) | 带 BLE 的经典 ESP32，4 MB Flash | BLE + 事件总线 |
| [esp32-headless.yaml](esp32-headless.yaml) | 带 BLE 的经典 ESP32，4 MB Flash | 核心 + Web |
| [atoms3u.yaml](atoms3u.yaml) | M5Stack ATOMS3U，ESP32-S3，8 MB Flash | 核心 + Web |
| [m5stickc-plus.yaml](m5stickc-plus.yaml) | 原始 M5StickC Plus / v1.1，ESP32-PICO-D4、AXP192 | 核心 + LCD + Button + LED + Web |

M5StickC Plus 的硬件包不适用于 Plus2。ATOMS3U 配置不包含屏幕；普通 ESP32 板也不能直接套用 M5StickC 的引脚。ESP32-S2 没有 BLE，不适用本项目。[M5StickC Plus 官方硬件说明](https://docs.m5stack.com/en/core/m5stickc_plus)、[ATOMS3U 官方硬件说明](https://docs.m5stack.com/en/core/AtomS3U)。

M5StickC Plus 1.1 的 LED、LCD 和串口安装流程也参考同作者的 [m5stickplus1.1 项目](https://github.com/chinawrj/m5stickplus1.1)，并在此适配为 ESPHome 可选模块。

可选模块通过入口 YAML 的 `packages` 组合；始终保留 `common`：

```yaml
packages:
  common: !include packages/common.yaml
  web: !include packages/web.yaml
  lcd: !include packages/m5stickc-plus-display.yaml
  buttons: !include packages/m5stickc-plus-buttons.yaml
  led: !include packages/m5stickc-plus-led.yaml
```

删除对应的一行即可省去该模块及其资源。上面三个 M5StickC 硬件包仅用于对应开发板。LCD、Button、LED、Web 之间没有互相调用；它们只通过事件总线交换请求和状态。详见[架构与事件契约](docs/architecture.md)。

## 编译与首次使用

需要 Python、C++ 构建工具和首次下载 ESPHome/ESP-IDF 依赖的网络连接。在项目目录执行：

```sh
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
cp secrets.example.yaml secrets.yaml
```

编辑 `secrets.yaml`，填入自己的 `charger_mac`。BLE 配置通过 `mac_address: !secret charger_mac` 引用它；启用 Web 时还需填写示例中的 Wi-Fi、热点、网页和 OTA 凭据。`secrets.yaml` 与生成固件不纳入版本管理，生成固件可能包含这些凭据。

选择一个入口文件进行校验、编译和 USB 上传，例如：

```sh
esphome config esp32-headless.yaml
esphome compile esp32-headless.yaml
esphome upload esp32-headless.yaml --device /dev/cu.YOUR_PORT --upload_speed 115200
esphome logs esp32-headless.yaml --device /dev/cu.YOUR_PORT
```

换用其他板时替换 YAML 文件名和实际串口。初次上传会替换开发板原固件；使用 ESPHome `upload` 处理正确的镜像与地址。纯核心配置没有 Wi-Fi/OTA，使用 USB 上传和查看日志。[ESPHome CLI 文档](https://esphome.io/guides/cli/)。

让手机小程序先断开充电器。开发板启动后连接指定设备，发现特征、订阅通知并读取设定值；这些步骤不会发送设置命令。

## 使用可选模块

**Web**：优先连接 `secrets.yaml` 中配置的 Wi-Fi，通过板子的局域网 IP 访问页面。连接失败后可使用设备配网热点，打开 `http://192.168.4.1/` 并用自己的网页凭据登录；`/wifi` 是配网页。页面资源存储在设备内，不依赖外部 CDN；未启用 Home Assistant 原生 API。纯核心配置没有该网页或热点。

| 网页项 | 含义 |
|---|---|
| `Charger ready` | GATT 已就绪，并已取得本次连接的设定值 |
| `Readback set voltage/current` | 设备回读的设定值 |
| `Target voltage/current` | 本地草稿，编辑不会向 BLE 发送设置 |
| `Apply settings` | 显式提交两项草稿值 |
| `Refresh settings` | 只读查询设定值 |
| `BLE connection` | 管理蓝牙连接，不是充电输出开关 |
| `Command status` | 请求接受、回读验证、拒绝或结果未知等状态 |

目前可设置范围是 **58.2–58.4 V / 4.9–5.1 A**，步长 **0.1**，仅覆盖已经验证的协议样本范围。该范围不是对任何电池适用性的判断。

**LCD**：横屏显示 BLE 就绪状态、回读设定值和来自事件总线的 IP 信息。失联时隐藏旧数值，没有网络事件时显示 `Offline / USB`。屏幕不会修改充电设置。

**Button**：M5StickC Plus 的 A 键发布只读刷新请求；B 键仅发布 `INPUT` 事件，目前没有绑定充电操作。

**LED**：M5StickC Plus 的红色 LED 在协议就绪后常亮，未连接或初始化时熄灭。

## 设置确认与断线行为

每次 Apply 只发送一次 `03` 设置帧，等待参数一致的 `83` 回显，再发新的 `02/01` 查询，并用 `82` 回读核对电压、电流。`ACCEPTED` 只表示请求开始处理，`VERIFIED` 才表示此流程中的读回验证完成。`83` 回显本身没有独立成功码。

超时或设置过程中断线会报告失败或结果未知，并清理连接状态；不会自动重发设置。启动、重连、查询、网页加载都不会自动 Apply。网页草稿在 BLE 重连后保留，控制器重启后从首次有效回读初始化。协议没有验证断电保存，因此不把回读匹配解释为永久保存或实际充电输出已改变。

## 测试与验证范围

测试分为三个层次：

| 层次 | 覆盖内容 | 不能证明的事项 |
|---|---|---|
| 原生 C++ 测试 | 抓包帧编解码；队列、关联 ID、快照失效；16 种模块组合的模拟事件路由 | 实际无线通信、引脚和屏幕效果 |
| ESPHome 配置与固件矩阵 | 固定必选 BLE+bus，遍历 LCD/Button/LED/Web 的全部 `2^4 = 16` 种 M5StickC 组合；另编译 ATOMS3U | 已把所有组合逐个刷到硬件 |
| 实机验证 | M5StickC Plus 无 LCD + Web 的读写与重连：**通过**；完整配置已安装，屏幕/按键观察待确认 | ATOMS3U 实机结果：**尚未验证** |

运行原生测试：

```sh
python components/lxy_charger/test_protocol.py
python tests/test_event_core.py
python tests/test_ble_serialization.py
```

运行完整矩阵：

```sh
python tests/matrix.py --mode validate --cases all
python tests/matrix.py --mode generate --cases all
python tests/matrix.py --mode compile --cases all --jobs 2
```

掩码 bit 0/1/2/3 分别表示 LCD/Button/LED/Web；例如 `--cases 0,15` 选择纯核心与全部启用。默认结果和日志在 `.esphome/matrix/`，可用 `--work-dir` 更改。矩阵使用示例凭据，只编译，不上传硬件。

完整编译结果见 [16 组合记录](docs/test-matrix.json)，实机范围见 [验证记录](docs/verification.md)。

BLE 串行回归测试编译实际服务组件，用模拟时钟和 GATT 传输验证后台轮询期间的请求等待、取消及不重复发送。后台 `04` 未收到 `84` 时不会插入新的查询或设置帧。

请以对应提交的测试输出为准；配置通过、代码生成通过和固件编译通过是不同结果。原生测试中的模块替身验证事件边界，不代替 ESPHome 实际适配器的编译或板上测试。

## 目录

- `components/charger_event_bus/`：可原生测试的队列与状态快照，以及 ESPHome 包装。
- `components/lxy_charger/`：BLE 服务、GATT 生命周期、协议解析与事务确认。
- `components/charger_web/`：可选网页实体适配器，草稿与用户请求。
- `packages/common.yaml`：必选核心。
- `packages/web.yaml`：可选网页、Wi-Fi 与网络状态事件。
- `packages/m5stickc-plus-*.yaml`：各自独立的 LCD、Button、LED 硬件模块。
- `assets/`：LCD 字体及其 [OFL 许可证说明](assets/README.md)。
- `tests/`：事件总线测试与配置/编译矩阵。

原始通信日志、设备备份、私人凭据和带凭据的固件不属于公开源码内容。
