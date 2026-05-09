# ESP32-C3 + ML307R 稳定短信转发固件重构大纲

## 目标

重写一个新的短信转发固件项目，硬件继续使用 ESP32-C3 + ML307R-DC。核心目标是长期稳定运行，优先保证短信接收、排队、转发、日志可见和自恢复。先不考虑商业化，先做自己可长期使用的稳定版本。

## 技术选型

- 开发框架：PlatformIO + Arduino-ESP32
- 语言：C 风格 C++
- 芯片：ESP32-C3
- 4G 模块：ML307R-DC，AT 固件
- 通信方式：ESP32-C3 `Serial1` UART 连接 ML307R
- 配置存储：NVS / Preferences
- Web 静态资源：优先 PROGMEM，后续可迁移 LittleFS
- 核心转发方式：HTTP Webhook
- 可选功能：SMTP、Telegram、Gotify、PushPlus、钉钉、飞书、Server酱

## 硬件约定

- ESP32 TX：GPIO3，接 ML307R RX
- ESP32 RX：GPIO4，接 ML307R TX
- 模组 EN 控制：旧代码假设为 GPIO5，但当前集成板实测无效；GPIO5 拉低期间 ML307R 仍响应 `AT -> OK`
- 串口波特率：115200
- LED：当前集成板 `PWR D5` 为电源指示，`NET D1` 为 ML307R 网络状态指示，不是 ESP32 应用可控 LED
- 供电要求：4G 模块必须使用稳定 5V 供电，避免短信/联网瞬间电压跌落

## 项目结构

```text
sms-forwarder-firmware/
  platformio.ini
  partitions.csv
  README.md
  TODO.md

  include/
    app_config.h
    logger.h
    wifi_manager.h
    modem_at.h
    sms_receiver.h
    sms_queue.h
    forwarder_http.h
    web_server.h
    config_store.h
    health_monitor.h
    scheduler.h

  src/
    main.cpp
    logger.cpp
    wifi_manager.cpp
    modem_at.cpp
    sms_receiver.cpp
    sms_queue.cpp
    forwarder_http.cpp
    web_server.cpp
    config_store.cpp
    health_monitor.cpp
    scheduler.cpp

  data/
    index.html
    style.css
    app.js
```

## PlatformIO 安装与构建

### 安装方式

推荐使用 VS Code + PlatformIO 插件：

1. 安装 VS Code
2. 在 VS Code 插件市场安装 `PlatformIO IDE`
3. 打开项目目录
4. 等待 PlatformIO 自动安装 toolchain 和 ESP32 平台

也可以使用命令行：

```bash
python3 -m pip install platformio
pio --version
```

### 初始 `platformio.ini`

```ini
[env:esp32c3]
platform = espressif32
framework = arduino
board = esp32-c3-devkitm-1
monitor_speed = 115200
upload_speed = 921600

build_flags =
  -DCORE_DEBUG_LEVEL=0
  -DARDUINO_USB_CDC_ON_BOOT=1

board_build.filesystem = littlefs
board_build.partitions = partitions.csv

lib_deps =
  pdulib
```

### 常用命令

```bash
pio run
pio run -t upload
pio device monitor
pio run -t clean
pio run -t uploadfs
```

## 架构原则

- `Serial1` 只能由 `modem_at` 模块读取。
- Web、Ping、主动发短信不能直接读写 `Serial1`。
- 收到短信后只做解析和入队，不同步执行 HTTP/SMTP。
- 所有 HTTP/SMTP 请求必须有超时。
- 所有失败必须写日志。
- `loop()` 中所有模块都用 `poll()` 推进，不做长时间阻塞。
- 辅助功能必须让路给短信接收。
- 定时重启和看门狗只是兜底，不能替代正确架构。

## 模块划分

### `logger`

职责：

- 保存 RAM 环形日志。
- 支持不同级别：INFO、WARN、ERROR、DEBUG。
- Web 页面可查看最近日志。
- 关键事件必须写日志：启动、WiFi、AT、短信、推送、失败、自恢复、重启。

TODO：

- [ ] 实现 `logInfo()` / `logWarn()` / `logError()`
- [ ] 实现环形日志缓冲区
- [ ] 实现 `/api/logs`
- [ ] 支持清空日志

### `config_store`

职责：

- 保存和读取配置。
- 使用 NVS / Preferences。
- 避免频繁写 Flash。

配置项：

- Web 管理账号密码
- WiFi SSID / 密码
- HTTP 推送通道
- 黑名单
- 管理员手机号
- 定时重启开关和时间
- 自恢复策略开关

TODO：

- [ ] 定义 `DeviceConfig`
- [ ] 实现默认配置
- [ ] 实现加载配置
- [ ] 实现保存配置
- [ ] 实现恢复出厂配置

### `wifi_manager`

职责：

- 连接 WiFi。
- 断线自动重连。
- 首次无配置时进入 AP 配网模式。
- 提供状态给 Web。

TODO：

- [ ] 从 NVS 读取 WiFi 配置
- [x] 正常 STA 模式连接 WiFi
- [ ] 连接失败后开启 SoftAP 配网
- [x] 每 10 秒检查连接状态
- [x] 记录断线和重连日志
- [ ] Web 页面支持修改 WiFi

### `modem_at`

职责：

- 独占 `Serial1`。
- 负责 AT 命令队列。
- 负责 AT 响应匹配。
- 负责识别 URC。
- 向 `sms_receiver` 投递短信 URC/PDU。

TODO：

- [ ] 初始化 UART
- [ ] 实现串口行读取器
- [ ] 实现 AT 命令队列
- [ ] 实现 AT 超时处理
- [ ] 实现 URC 识别
- [ ] 禁止其他模块直接访问 `Serial1`
- [ ] 提供 `modemSubmitCommand()`
- [ ] 提供模组重启函数 `modemPowerCycle()`

### `sms_receiver`

职责：

- 处理 `+CMT` 和 PDU。
- 解码短信。
- 合并长短信。
- 生成统一 `SmsMessage`。
- 投递到短信队列。

TODO：

- [ ] 接入 `pdulib`
- [ ] 定义 `SmsMessage`
- [ ] 实现普通短信解析
- [ ] 实现长短信缓存
- [ ] 实现长短信超时处理
- [ ] 实现黑名单过滤
- [ ] 写入接收日志

### `sms_queue`

职责：

- 保存待转发短信。
- 管理短信状态。
- 管理重试次数和下次重试时间。
- 后续可支持 LittleFS 持久化。

TODO：

- [x] 实现 RAM 队列
- [x] 设置最大队列长度
- [x] 队列满时按固定策略处理
- [x] 每条短信记录转发状态
- [x] 每条短信记录失败次数
- [ ] 提供 Web 查询队列状态

### `forwarder_http`

职责：

- 从队列取短信。
- 按配置发送到 HTTP/Webhook 通道。
- 每个通道独立超时和失败记录。
- 失败后重试，不阻塞短信接收。

TODO：

- [x] 实现 HTTP POST JSON
- [ ] 实现 GET URL 参数模式
- [x] 实现每通道超时
- [x] 实现失败重试
- [x] 实现指数退避
- [x] 记录 HTTP code 和错误原因
- [ ] 一个通道失败不影响其他通道

### `web_server`

职责：

- 提供 LuCI 风格轻量 Web 后台。
- 页面以状态、配置、日志、队列为主。
- Web 请求不直接执行长阻塞动作。
- API 返回 JSON。

页面：

- `/` 状态页
- `/config` 配置页
- `/wifi` WiFi 配网页
- `/queue` 短信队列
- `/logs` 日志页
- `/system` 系统页

API：

- `/api/status`
- `/api/config`
- `/api/config/save`
- `/api/wifi/save`
- `/api/logs`
- `/api/queue`
- `/api/reboot`
- `/api/push/test`

TODO：

- [ ] 实现基础 WebServer
- [ ] 实现 Basic Auth
- [ ] 实现状态 API
- [ ] 实现配置 API
- [ ] 实现日志 API
- [ ] 实现轻量 CSS
- [ ] 避免 C++ 中大量拼接 HTML

### `health_monitor`

职责：

- 监控系统健康。
- 执行分级自恢复。
- 提供状态给 Web。

监控项：

- WiFi 连接状态
- AT 是否响应
- 短信模式是否正确
- 蜂窝网络注册状态
- 剩余 heap
- 最近成功收短信时间
- 最近成功推送时间
- 连续失败次数

恢复顺序：

1. 重新设置 `AT+CMGF=0`
2. 重新设置 `AT+CNMI=2,2,0,0,0`
3. WiFi 重连
4. ML307R 软件恢复或模块软件复位 AT 指令
5. ESP32 软件重启兜底

TODO：

- [ ] 实现周期性健康检查
- [ ] 实现失败计数
- [ ] 实现模组重新初始化
- [ ] 实现模组软件恢复；当前集成板不支持已验证的 GPIO 断电重启
- [ ] 实现 ESP32 重启兜底
- [ ] 所有动作写日志

### `scheduler`

职责：

- 管理定时任务。
- 实现定时整机重启。
- 避免在短信处理或推送中重启。

TODO：

- [ ] 实现每日定时重启
- [ ] 实现最大运行时长重启
- [ ] 重启前检查队列是否为空
- [ ] 最近 10 分钟有短信时延迟重启
- [ ] 写入重启原因日志

## 开发阶段 TODO

### Phase 1：最小硬件验证

- [ ] 创建 PlatformIO 项目
- [ ] 跑通 ESP32-C3 串口日志
- [x] 验证 `PWR D5` / `NET D1` 是模块板状态灯，不受 ESP32 hello world 固件控制
- [ ] 初始化 `Serial1`
- [ ] 发送 `AT`，收到 `OK`
- [x] 验证 GPIO5 不能控制当前集成板 ML307R EN；拉低期间 `AT` 仍返回 `OK`
- [ ] 查询 `AT+CEREG?`

### Phase 2：短信接收内核

- [ ] 设置 `AT+CMGF=0`
- [ ] 设置 `AT+CNMI=2,2,0,0,0`
- [ ] 接收 `+CMT`
- [ ] 读取 PDU 行
- [ ] 解码短信
- [ ] 打印发送者、时间、内容
- [ ] 支持长短信合并
- [ ] 接入日志

### Phase 3：HTTP 转发

- [x] 实现短信 RAM 队列
- [x] 实现 HTTP POST JSON
- [x] 设置 HTTP 超时
- [x] 转发成功后标记状态
- [x] 转发失败后重试
- [ ] Web 查看队列状态

### Phase 4：Web 后台

- [ ] 实现状态页
- [ ] 实现日志页
- [ ] 实现配置页
- [ ] 实现 WiFi 配网页
- [ ] 实现 HTTP 推送配置
- [ ] 配置保存到 NVS
- [ ] 页面采用 LuCI-like 轻量 CSS

### Phase 5：长期稳定性

- [ ] WiFi 自动重连
- [ ] AT 健康检查
- [ ] 模组短信模式检查
- [ ] 模组自动软件恢复
- [ ] ESP32 看门狗
- [ ] 定时整机重启
- [ ] heap 监控
- [ ] 72 小时连续测试

### Phase 6：可选增强

- [ ] Web 上传 OTA
- [ ] HTTP 在线 OTA
- [ ] Telegram 推送
- [ ] Gotify 推送
- [ ] 钉钉/飞书/PushPlus/Server酱
- [ ] SMTP 邮件
- [ ] 主动发短信
- [ ] Ping 测试
- [ ] 管理员短信命令

## 验收标准

- [ ] 连续运行 72 小时不中断
- [ ] WiFi 断开后恢复，短信可继续转发
- [ ] webhook 不可达时不丢短信
- [ ] Web 页面可看到日志和队列
- [ ] Web 查询状态时不影响短信接收
- [ ] 普通短信、中文短信、长短信都能转发
- [ ] HTTP 推送失败不会阻塞主循环
- [ ] 模组 AT 异常后能自动恢复
- [ ] 定时重启不会打断正在处理的短信
- [ ] 可通过 PlatformIO 一键构建

## 暂不做

- [ ] 不做云端短信存储
- [ ] 不做多用户云平台
- [ ] 不做复杂前端框架
- [ ] 不做 React/Vue/Tailwind
- [ ] 不做真正的 LuCI 移植
- [ ] 不优先做 SMTP
- [ ] 不优先做商业授权
