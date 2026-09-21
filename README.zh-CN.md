# LXY BLE Charger · ESPHome

[English](README.md) | **简体中文**

基于 **ESPHome 2026.9.0 + ESP-IDF** 的 LXY BLE 充电器控制器。BLE 与事件总线是必选核心，LCD、实体按键、LED、独立网页是四个可选模块。使用网页时不需要 Home Assistant 服务器；不选网页时，BLE 仍可独立连接并读取参数。

项目支持已验证的 `FFF0` 服务、`FFF2` 写入、`FFF1` 通知协议。相同品牌或设备名称不代表协议一定兼容。LCD 主区域用于显示**输出电压、电流**，**充电设定值**以较小字体单独显示。输出数据缺失或过期时显示 `--.-`，绝不用设定值代替测量值。**当前尚未启用实时输出解码**：字节映射与缩放仍需非零充电器样本验证；尚未实现温度解析或充电输出开关。

## 界面截图

### LCD 布局预览

下列预览由**与固件相同的 C++ 视图模型**生成，**不是硬件照片**。[渲染工具](tests/render_lcd.py) 使用 Pillow 绘制字体，个别像素可能与设备不同。

<p>
  <img src="docs/images/lcd-output-preview.png" width="320" alt="LCD输出页预览：无有效实时数据时显示横线">
  <img src="docs/images/lcd-edit-preview.png" width="320" alt="LCD编辑页预览：58.3 V和5.1 A示例草稿">
  <img src="docs/images/lcd-confirm-preview.png" width="320" alt="LCD确认页预览：需要再次长按A才提交">
</p>

从左到右为输出页、编辑页、确认页。输出页特意不提供有效实时样本，因此显示 `--.-`；较小的设定值属于示例界面数据。编辑与确认采用 **58.3 V / 5.1 A 示例草稿**，不是实际输出测量截图。

### 实机 Web 界面

<img src="docs/images/web-ui.png" width="702" alt="实机ESPHome网页：回读设定58.4 V和5.1 A，输出测量不可用，实时输出有效状态关闭">

截图来自 OTA 后运行 v2.1 开发固件的 M5StickC Plus。只读本地代理保留设备原始页面与实时事件数据，未模拟或替换读数；图片仅裁去 IP 等诊断信息。设定回读为 **58.4 V / 5.1 A**，输出字段为 **NA**，`Live output valid` 为 **OFF**，与当前输出解码尚未启用的状态一致。

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

### Web

优先连接 `secrets.yaml` 中配置的 Wi-Fi，通过板子的局域网 IP 访问页面。连接失败后可使用设备配网热点，打开 `http://192.168.4.1/` 并用自己的网页凭据登录；`/wifi` 是配网页。页面资源存储在设备内，不依赖外部 CDN；未启用 Home Assistant 原生 API。纯核心配置没有该网页或热点。

| 网页项 | 含义 |
|---|---|
| `Charger ready` | GATT 已就绪，并已取得本次连接的设定值 |
| `Output voltage/current` | 实时输出字段；BLE 输出解码尚未启用，当前不可用 |
| `Live output valid` | 是否存在未超过 6 秒的有效输出样本 |
| `Readback set voltage/current` | 设备回读的设定值 |
| `Target voltage/current` | 本地草稿，编辑不会向 BLE 发送设置 |
| `Apply settings` | 显式提交两项草稿值 |
| `Refresh settings` | 只读查询设定值 |
| `BLE connection` | 管理蓝牙连接，不是充电输出开关 |
| `Command status` | 请求接受、回读验证、拒绝或结果未知等状态 |

目前可设置范围是 **58.2–58.4 V / 4.9–5.1 A**，步长 **0.1**，仅覆盖已经验证的协议样本范围。该范围不是对任何电池适用性的判断。

### LCD

横屏以大数字突出输出电压、电流，较小的 `Set` 数值表示充电设定值。输出数据超过 **6 秒**没有有效更新，或设备断连后，显示为 `--.-`。设定电压不能当作实际输出电压读取。

紧凑界面还显示当前选中的设定项、编辑/确认状态，以及连接或事务状态；不再放置 IP、品牌、温度或长状态文本。布局和编辑状态由共享的 `charger_display` 视图模型提供。

当前 BLE 服务只保留原始 `84` 帧，不发布已确认的输出测量，因此实际运行的常规页面会显示 `--.-`。输出解码仍需非零充电器样本验证。界面具有输出字段、或能够生成布局预览，不代表已证明测量准确性；具体以对应版本的[验证记录](docs/verification.md)为准。

### 按键

只有启用且可用的 LCD 才能通过按键编辑设定值。长按指**按住 800 ms 至 5 秒后释放**。操作在释放时判定；超过 5 秒的按住会作为异常长按忽略。

| 模式 | A 键 | B 键 |
|---|---|---|
| 查看 | 短按：选择电压/电流。长按：进入编辑。 | 短按：刷新设定值。 |
| 编辑 | 短按：当前草稿减 0.1。长按：进入确认。 | 短按：当前草稿加 0.1。长按：取消。 |
| 确认 | 再次长按，才将电压/电流草稿成对提交一次。 | 任意按下均取消。 |

进入编辑和进入确认都不会发送设置命令，数值始终受上述已验证范围限制。**30 秒**无操作、断连、其他操作使设备忙碌、基准配置变化或显示不可用时，编辑自动取消。取消的草稿不会提交，也不会在稍后重放。

不启用 LCD 时，Button 包仅提供选择和只读刷新，不能进入编辑或 Apply。按键通过事件通信，不直接调用 BLE 服务。

### LED

红色 LED 独立于 LCD 和 Web 工作：

| 状态 | 指示 |
|---|---|
| 未连接 | 每 3 秒亮 80 ms |
| 已连接、协议尚未就绪 | 亮 500 ms、灭 500 ms |
| 已就绪且空闲 | 常亮 |
| 事务忙碌 | 亮 125 ms、灭 125 ms |
| `VERIFIED` | 两次亮 100 ms，中间灭 100 ms；每 1.5 秒重复，持续 3 秒 |
| `FAILED` / `UNKNOWN` | 三次亮 100 ms，相邻两次之间灭 150 ms；每 1.5 秒重复，持续 6 秒 |
| `REJECTED` | 两次亮 300 ms，中间灭 200 ms；每 1.5 秒重复，持续 3 秒 |

这些指示表示连接与请求结果，不表示充电输出已开启。优先级依次是失败/未知、忙碌、成功/拒绝提示、当前连接状态。快速重连后的成功读回不会提前清除 6 秒故障提示。所有模式均非阻塞。

## 设置确认与断线行为

每次 Apply 只发送一次 `03` 设置帧，等待参数一致的 `83` 回显，再发新的 `02/01` 查询，并用 `82` 回读核对电压、电流。`ACCEPTED` 只表示请求开始处理，`VERIFIED` 才表示此流程中的读回验证完成。`83` 回显本身没有独立成功码。

后台状态查询也使用同一串行通道：`04` 等待 `84` 时，可保留一笔带请求 ID 和固定参数快照的查询或 Apply，收到状态回复后才发送。后台轮询本身不会阻止编辑本地草稿。

超时或设置过程中断线会报告失败或结果未知，并清理连接状态；不会自动重发设置。启动、重连、查询、网页加载都不会自动 Apply。网页草稿在 BLE 重连后保留，控制器重启后从首次有效回读初始化。协议没有验证断电保存，因此不把回读匹配解释为永久保存或实际充电输出已改变。尚未实现充电输出开关。详见[协议参考与边界](docs/protocol.md)。

## 测试与验证范围

测试分为三个层次：

| 层次 | 覆盖内容 | 不能证明的事项 |
|---|---|---|
| 原生 C++ 测试 | 抓包帧编解码；队列、关联 ID、快照失效；16 种模块组合的模拟事件路由 | 实际无线通信、引脚和屏幕效果 |
| ESPHome 配置与固件矩阵 | 固定必选 BLE+bus，遍历 LCD/Button/LED/Web 的全部 `2^4 = 16` 种 M5StickC 组合；另编译 ATOMS3U | 已把所有组合逐个刷到硬件 |
| 实机验证 | M5StickC Plus 无 LCD + Web 和完整配置的 BLE 读写与重连：**通过**；此前发布版本的 LCD 显示：**用户现场验收通过** | ATOMS3U 实机结果：**尚未验证**；红灯和实体按键尚未独立现场确认 |

运行原生测试：

```sh
python components/lxy_charger/test_protocol.py
python tests/test_event_core.py
python tests/test_ble_serialization.py
python tests/test_local_controls.py
python tests/test_telemetry_view.py
python tests/test_web_telemetry.py
```

运行完整矩阵：

```sh
python tests/matrix.py --mode validate --cases all
python tests/matrix.py --mode generate --cases all
python tests/matrix.py --mode compile --cases all --jobs 2
```

掩码 bit 0/1/2/3 分别表示 LCD/Button/LED/Web；例如 `--cases 0,15` 选择纯核心与全部启用。默认结果和日志在 `.esphome/matrix/`，可用 `--work-dir` 更改。矩阵使用示例凭据，只编译，不上传硬件。

本地操作测试编译真实按键和 LED 适配器，检查确认、取消、输入丢失与指示时序。遥测界面和 Web 测试通过合成的类型化事件验证新鲜度、失效与显示，不用于证明充电器的输出字节映射。Web 测试覆盖真实适配器及三项可选遥测实体的全部 8 种组合。

完整编译结果见 [16 组合记录](docs/test-matrix.json)，对应的源码版本与实机范围见 [验证记录](docs/verification.md)。请以所用版本的记录为准；旧版界面的验收不自动覆盖新界面。

BLE 串行回归测试编译实际服务组件，用模拟时钟和 GATT 传输验证后台轮询期间的请求等待、取消及不重复发送。后台 `04` 未收到 `84` 时不会插入新的查询或设置帧。

请以对应提交的测试输出为准；配置通过、代码生成通过和固件编译通过是不同结果。原生测试中的模块替身验证事件边界，不代替 ESPHome 实际适配器的编译或板上测试。

## 目录

- `components/charger_event_bus/`：可原生测试的队列与状态快照，以及 ESPHome 包装。
- `components/lxy_charger/`：BLE 服务、GATT 生命周期、协议解析与事务确认。
- `components/charger_web/`：可选网页实体适配器，草稿与用户请求。
- `components/charger_display/`：共享 LCD 布局与视图模型。
- `components/charger_buttons/`：本地按键编辑、显式确认与取消。
- `components/charger_indicator/`：独立、非阻塞的 LED 模式。
- `packages/common.yaml`：必选核心。
- `packages/web.yaml`：可选网页、Wi-Fi 与网络状态事件。
- `packages/m5stickc-plus-*.yaml`：各自独立的 LCD、Button、LED 硬件模块。
- `assets/`：LCD 字体及其 [OFL 许可证说明](assets/README.md)。
- `tests/`：事件总线测试与配置/编译矩阵。

原始通信日志、设备备份、私人凭据和带凭据的固件不属于公开源码内容。
