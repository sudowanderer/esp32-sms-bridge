# ESP32C3 与 ML307R-DC 4G 模块交互说明

本文档基于当前仓库现有固件实现整理，用于后续重构时快速理解 ESP32C3 如何控制、初始化、读写和管理 ML307R-DC 4G 通讯模块。

## 硬件连接

当前硬件使用 ESP32C3 通过 UART 与 ML307R-DC 4G 模块通信。旧代码中保留了 `GPIO5` 控制 4G 模块 EN 的假设，但在当前集成板上实测无效。

代码中的引脚定义位于 `code/code.ino`：

```cpp
#define TXD 3
#define RXD 4
#define MODEM_EN_PIN 5
```

连接关系：

| ESP32C3 | ML307R-DC | 作用 |
|---|---|---|
| GPIO3 / TX | RX | ESP32 向 4G 模块发送 AT 指令 |
| GPIO4 / RX | TX | ESP32 接收 4G 模块响应和短信上报 |
| GPIO5 | 未确认 / 旧 EN 假设 | 当前集成板实测不能控制 ML307R 上下电 |
| GND | GND | 共地 |
| 5V | VCC | 给 4G 模块供电 |

注意事项：

- ESP32C3 和 ML307R-DC 必须共地。
- ML307R-DC 需要稳定 5V 供电，短信、联网和发射瞬间电流较大，供电不稳会导致模组异常、AT 无响应或短信上报丢失。
- 当前集成板上，`PWR D5` 红灯是电源指示，`NET D1` 蓝灯是 ML307R 网络状态指示。纯串口固件不控制任何 GPIO 时，这两个 LED 仍按原逻辑工作，因此它们不应被当作 ESP32 应用 LED。
- 当前集成板上，`GPIO5` 拉高/拉低期间 ML307R 仍能持续响应 `AT -> OK`，因此不能把 `GPIO5` 当作可靠的 ML307R 断电重启控制脚。

## 通信方式

ESP32C3 与 ML307R-DC 的主通信链路是 `Serial1` UART。

当前串口初始化：

```cpp
Serial1.begin(115200, SERIAL_8N1, RXD, TXD);
Serial1.setRxBufferSize(SERIAL_BUFFER_SIZE);
```

参数：

- 波特率：`115200`
- 数据格式：`8N1`
- RX：`GPIO4`
- TX：`GPIO3`

交互模型：

- ESP32C3 通过 `Serial1.println()` 向 ML307R 发送 AT 指令。
- ML307R 通过同一条 UART 返回 AT 响应，例如 `OK`、`ERROR`、`+CEREG:`。
- ML307R 收到短信时，会主动通过 UART 上报 URC，例如 `+CMT:`，后面跟随一行 PDU 数据。

因此，短信接收、Web AT 调试、模组查询、主动发短信、Ping 测试都共享同一个 UART 通道。

## 4G 模块电源控制

旧代码通过 `MODEM_EN_PIN` 控制 ML307R-DC 的 EN 引脚。这是旧硬件/飞线方案的假设，不适用于当前已测试的 ESP32-C3 + 4G 集成板。

相关函数：

```cpp
void modemPowerCycle() {
  pinMode(MODEM_EN_PIN, OUTPUT);

  Serial.println("EN 拉低：关闭模组");
  digitalWrite(MODEM_EN_PIN, LOW);
  delay(1200);

  Serial.println("EN 拉高：开启模组");
  digitalWrite(MODEM_EN_PIN, HIGH);
  delay(6000);
}
```

旧代码预期作用：

- 启动时让模组从干净状态开始。
- 管理员短信 `RESET` 命令会触发模组重启。
- 后续重构中可以作为自恢复手段，例如 AT 长时间无响应时重启 ML307R。

当前 `resetModule()` 流程：

1. 调用 `modemPowerCycle()`。
2. 清空串口残留数据。
3. 最多等待约 10 秒，反复发送 `AT`。
4. 收到 `OK` 认为模组恢复。

当前集成板实测结果：

```text
MODEM_EN HIGH -> AT 返回 OK
MODEM_EN LOW  -> AT 仍返回 OK
MODEM_EN HIGH -> AT 返回 OK
```

结论：

- `GPIO5` 不能让 ML307R 掉电、失联或重启。
- `PWR D5` / `NET D1` 是模块板状态灯，不是 ESP32 应用可控 LED。
- 固件不能依赖 `modemPowerCycle()` 做硬件级恢复。
- 真正的整板断电重启也不能由 ESP32 自身代码完成，因为整板由 USB 供电；ESP32 一旦切断自身供电，就无法再执行恢复上电动作。
- 可用的恢复手段应优先设计为 AT 软件恢复，例如重新设置短信模式、重新设置短信上报、查询/恢复功能模式，最后才使用 `ESP.restart()` 重启 ESP32 自身。

## 启动初始化流程

当前 `setup()` 中与 ML307R 相关的初始化流程如下：

1. 初始化 ESP32 USB 日志串口：

   ```cpp
   Serial.begin(115200);
   ```

2. 初始化 ML307R UART：

   ```cpp
   Serial1.begin(115200, SERIAL_8N1, RXD, TXD);
   Serial1.setRxBufferSize(SERIAL_BUFFER_SIZE);
   ```

3. 清空串口残留数据。

4. 旧代码调用 `modemPowerCycle()`，试图通过 EN 引脚重启 ML307R；当前集成板上该动作实测无效，重构时不应依赖。

5. 再次清空串口残留数据。

6. 发送 `AT`，等待 `OK`，确认模组可通信：

   ```text
   AT
   ```

7. ML307R 可能上报模块业务命令可用：

   ```text
   +MATREADY
   ```

   当前固件不会在 `setup()` 中一次性塞满 AT 队列，而是在主循环中用启动状态机逐条提交命令。`AT`
   返回 `OK` 只表示 UART 基础通信可用；`+MATREADY` 只作为 URC 记录，不再作为短信初始化的前置条件。
   实测 ML307R 可能很晚上报或不稳定上报 `+MATREADY`，启动主线必须优先完成短信接收配置。

8. 设置短信 PDU 模式：

   ```text
   AT+CMGF=0
   ```

9. 设置短信自动上报：

   ```text
   AT+CNMI=2,2,0,0,0
   ```

   该配置表示新短信直接通过串口上报。

10. 查询蜂窝网络注册状态：

   ```text
   AT+CEREG?
   ```

   当前代码认为 `+CEREG:` 中状态为 `1` 或 `5` 时网络已注册：

   - `1`：已注册本地网络
   - `5`：已注册漫游网络

11. 启动主线完成后，后台 data guard 按 ML307R 手册断开应用层数据连接，避免默认应用层连网消耗流量：

   ```text
   AT+MIPCALL?
   AT+MIPCALL=0,1
   ```

   ML307R 手册说明自动拨号模式下不建议用 `AT+CGACT` 手动激活或去激活 PDP，因此固件不再发送
   `AT+CGACT=0,<cid>`。Guard 只查询 `AT+MIPCALL?`；如果应用层连接已建立，则发送 `AT+MIPCALL=0,1`。
   若返回 `ERROR`、`+CME ERROR` 或超时，固件会记录 `data_guard_status=command_failed` 并每 60 秒重试。
   Data guard 不阻塞 `AT+CMGF=0` / `AT+CNMI=2,2,0,0,0` 短信初始化。


重构注意：

- 当前初始化中多处使用无限 `while` 等待，模组或网络异常时设备可能无法进入 Web 管理界面。
- 新固件建议改成有限重试，并把失败状态暴露到 Web 状态页和日志页。

## AT 命令封装

当前有两个主要 AT helper。

### `sendATCommand()`

用途：发送任意 AT 指令，返回完整响应字符串。

简化逻辑：

```cpp
String sendATCommand(const char* cmd, unsigned long timeout) {
  while (Serial1.available()) Serial1.read();
  Serial1.println(cmd);

  unsigned long start = millis();
  String resp = "";
  while (millis() - start < timeout) {
    while (Serial1.available()) {
      char c = Serial1.read();
      resp += c;
      if (resp.indexOf("OK") >= 0 || resp.indexOf("ERROR") >= 0) {
        delay(50);
        while (Serial1.available()) resp += (char)Serial1.read();
        return resp;
      }
    }
  }
  return resp;
}
```

### `sendATandWaitOK()`

用途：发送 AT 指令，只关心是否收到 `OK`。

简化逻辑：

```cpp
bool sendATandWaitOK(const char* cmd, unsigned long timeout) {
  while (Serial1.available()) Serial1.read();
  Serial1.println(cmd);
  ...
  if (resp.indexOf("OK") >= 0) return true;
  if (resp.indexOf("ERROR") >= 0) return false;
}
```

### 当前问题

这两个函数都会在发送 AT 前清空 `Serial1`：

```cpp
while (Serial1.available()) Serial1.read();
```

这在现有架构中有风险，因为 ML307R 可能刚好上报了短信：

```text
+CMT: ...
089168...
```

如果这时 Web 查询、Ping、主动发短信或其他 AT 命令执行了清空串口，就可能把短信 URC 或 PDU 数据丢掉。

### 重构建议

新架构中应有单独的 `modem_at` 模块独占 `Serial1`：

- 只有 `modem_at` 可以读取 `Serial1`。
- Web、Ping、主动发短信、状态查询都提交 AT 命令任务。
- `modem_at` 同时负责识别 AT 响应和 URC。
- 不允许业务模块直接清空 UART。

## 短信接收流程

ML307R 收到短信后，通过 UART 主动上报。当前代码使用 `AT+CNMI=2,2,0,0,0`，优先期望新短信直接发送到串口。

典型数据形态：

```text
+CMT: ...
089168...
```

其中：

- `+CMT:` 是短信上报头。
- 下一行是 PDU 编码后的短信内容。

模块也可能只上报存储通知：

```text
+CMTI: "SM",37
```

这种情况下固件必须通过 `modem_at` 提交 `AT+CMGR=37` 读取该存储位置的短信，解析返回中的 PDU 并先入队。确认短信已被接收链路接受后，再提交 `AT+CMGD=37` 删除该存储位置，避免重启后重复处理。长短信可能拆成多个 PDU 并存储到多个 SIM index，合并入队成功后应删除参与合并的所有 index。业务模块不得绕过 `modem_at` 直接读取或清空 UART。

当前处理函数：

```cpp
void checkSerial1URC()
```

读取串口行的函数：

```cpp
String readSerialLine(HardwareSerial& port)
```

当前 `checkSerial1URC()` 状态机只有两个状态：

```cpp
static enum {
  IDLE,
  WAIT_PDU
} state = IDLE;
```

处理流程：

1. 在 `IDLE` 状态读取一行。
2. 如果该行以 `+CMT:` 开头，说明检测到短信上报。
3. 进入 `WAIT_PDU` 状态。
4. 下一行如果是十六进制字符串，认为是 PDU 数据。
5. 调用 `pdulib` 解码 PDU。
6. 读取发送者、时间戳、短信内容和长短信信息。
7. 普通短信直接进入 `processSmsContent()`。
8. 长短信进入长短信缓存，收齐后合并再进入 `processSmsContent()`。

PDU 解码相关调用：

```cpp
pdu.decodePDU(line.c_str())
pdu.getSender()
pdu.getTimeStamp()
pdu.getText()
pdu.getConcatInfo()
```

## 长短信处理

当前代码使用 `pdulib` 的 concat 信息识别长短信。

相关字段：

```cpp
int* concatInfo = pdu.getConcatInfo();
int refNumber = concatInfo[0];
int partNumber = concatInfo[1];
int totalParts = concatInfo[2];
```

当前缓存限制：

```cpp
#define MAX_CONCAT_PARTS 10
#define CONCAT_TIMEOUT_MS 30000
#define MAX_CONCAT_MESSAGES 5
```

当前逻辑：

- `totalParts > 1 && partNumber > 0` 时认为是长短信分段。
- 用 `refNumber + sender` 查找缓存槽。
- 每个分段按 `partNumber - 1` 存入数组。
- 收齐后调用 `assembleConcatSms()` 合并。
- 超过 `CONCAT_TIMEOUT_MS` 后，即使缺片也会强制合并并转发，缺失部分用占位文本标记。

重构建议：

- 长短信合并逻辑应放入独立 `sms_receiver` 或 `concat_sms` 模块。
- 收齐或超时后生成统一 `SmsMessage`。
- 短信生成后先入队，不直接执行 HTTP/SMTP 推送。

## 短信内容处理与转发

当前短信解析完成后进入：

```cpp
void processSmsContent(const char* sender, const char* text, const char* timestamp)
```

该函数负责：

1. 打印短信内容到 USB 串口。
2. 检查号码黑名单。
3. 检查是否为管理员短信命令。
4. 调用 `sendSMSToServer()` 执行 HTTP 多通道推送。
5. 调用 `sendEmailNotification()` 执行邮件通知。

当前问题：

- HTTP 和邮件推送在短信接收路径里同步执行。
- 如果 HTTP/SMTP 阻塞，主循环会被阻塞，后续短信解析可能延迟或丢失。

重构建议：

- `processSmsContent()` 只做过滤、命令识别和入队。
- HTTP/SMTP 推送由后台 `forwarder` 模块处理。
- 失败短信保留在队列中等待重试。

## 主动发送短信流程

当前主动发送短信使用 PDU 模式。

入口函数：

```cpp
bool sendSMS(const char* phoneNumber, const char* message)
```

流程：

1. 使用 `pdulib` 编码 PDU：

   ```cpp
   pdu.setSCAnumber();
   int pduLen = pdu.encodePDU(phoneNumber, message);
   ```

2. 发送 `AT+CMGS=<pduLen>`：

   ```cpp
   Serial1.println(cmgsCmd);
   ```

3. 等待 `>` 提示符。

4. 写入 PDU 内容：

   ```cpp
   Serial1.print(pdu.getSMS());
   ```

5. 写入 `0x1A`，即 Ctrl+Z，表示短信内容结束：

   ```cpp
   Serial1.write(0x1A);
   ```

6. 等待 `OK` 或 `ERROR`。

当前问题：

- 发送短信前会清空串口。
- 等待 `>` 最多 5 秒。
- 等待最终响应最多 30 秒。
- 期间会阻塞主循环。

重构建议：

- 主动发短信应实现为 AT 命令状态机任务。
- 等待 `>`、写入 PDU、等待最终响应都交给 `modem_at` 统一调度。
- Web 页面只提交发送请求，不直接阻塞等待完整发送流程。

## Web 调试和模组查询

当前 Web 工具箱支持发送任意 AT 指令：

```cpp
String resp = sendATCommand(cmd.c_str(), 5000);
```

同时提供多个模组查询功能。

常用 AT 指令：

| 功能 | AT 指令 |
|---|---|
| 模组固件信息 | `ATI` |
| 信号质量 | `AT+CESQ` |
| IMSI | `AT+CIMI` |
| ICCID | `AT+ICCID` |
| 本机号码 | `AT+CNUM` |
| 网络注册状态 | `AT+CEREG?` |
| 运营商 | `AT+COPS?` |
| 应用层数据连接 | `AT+MIPCALL?` |
| 断开应用层数据连接 | `AT+MIPCALL=0,1` |
| PDP 激活状态 | `AT+CGACT?` |
| APN / PDP 上下文 | `AT+CGDCONT?` |
| 功能模式 / 飞行模式 | `AT+CFUN?` |
| 设置全功能模式 | `AT+CFUN=1` |
| 设置飞行模式 | `AT+CFUN=4` |

首页 `Data connection` 表示 ML307R 应用层数据连接，优先由 `AT+MIPCALL?` 决定。`AT+CGACT?`
只作为底层 PDP 诊断信息，不直接决定首页 Data connection。APN 为 `IMS` 的 context 是运营商内部 IMS
通道，不应作为普通移动数据连接展示。

当前问题：

- Web 查询直接调用 `sendATCommand()`。
- `sendATCommand()` 会清空串口。
- Web 查询可能与短信 URC 竞争同一个 UART。

重构建议：

- Web 层只提交 AT 查询任务。
- `modem_at` 返回异步结果。
- Web 页面轮询任务状态或读取最近查询结果。

## Ping / 数据连接流程

当前 Web Ping 功能用于让 4G 模块消耗少量流量并验证数据连接。

流程：

1. 清空串口缓冲区。

2. 建立应用层数据连接：

   ```text
   AT+MIPCALL=1,1
   ```

3. 等待网络稳定。

4. 发送 MPING：

   ```text
   AT+MPING="8.8.8.8",30,1
   ```

5. 等待 `+MPING:` URC 结果。

6. 断开应用层数据连接：

   ```text
   AT+MIPCALL=0,1
   ```

当前问题：

- Ping 前后都会清空串口。
- 等待 MPING 结果最长 35 秒。
- Ping 期间主循环被长时间占用。

重构建议：

- Ping 属于低优先级辅助任务。
- 不应直接清空串口。
- 不应阻塞短信接收。
- 建议通过 `modem_at` 状态机执行，并将结果写入日志或任务结果。

## 当前交互架构的问题

现有代码能跑通功能，但对长期稳定运行不够理想，主要风险如下：

- 多个功能直接读写 `Serial1`，缺少统一串口所有权。
- 多处直接清空串口缓冲，可能丢失短信 URC 或 PDU。
- Web 查询、Ping、主动发短信、短信接收共享同一个 UART，但没有统一调度器。
- 收到短信后同步执行 HTTP/SMTP 推送，网络异常可能阻塞短信接收。
- Ping 和主动发短信流程可能长时间阻塞主循环。
- 初始化阶段存在无限等待，模组异常时 Web 管理界面无法启动。
- 日志只输出到 USB 串口，Web 页面无法查看运行过程。

## 重构建议

后续重构时建议围绕以下原则设计：

1. `modem_at` 模块独占 `Serial1`。
2. 所有 AT 请求进入命令队列。
3. AT 响应和 URC 在同一个串口解析器中处理。
4. `sms_receiver` 只接收 `modem_at` 投递的短信事件。
5. 收到短信后先生成 `SmsMessage` 并入队。
6. HTTP/SMTP 推送由后台 `forwarder` 模块处理。
7. Web 查询、Ping、主动发短信都作为后台任务提交。
8. 不允许业务模块直接清空串口。
9. 所有串口事件、AT 超时、短信解析、模组重启都写入 Web 日志。
10. 模组异常时按顺序恢复：重新设置短信模式、重启 ML307R、最后重启 ESP32。

当前集成板无法通过 GPIO5 硬重启 ML307R，恢复策略应调整为：重新设置短信模式、重新设置短信上报、检查 SIM/网络注册、尝试 AT 功能模式恢复或模块软件复位命令，最后使用 `ESP.restart()` 重启 ESP32。`ESP.restart()` 只复位 ESP32，不会给整板或 ML307R 断电。

## 重构后的建议模块边界

建议拆分：

```text
modem_at
  - 初始化 UART
  - 读取 Serial1
  - 发送 AT 命令
  - 匹配响应
  - 识别 URC
  - 提供模块软件恢复命令；不要默认依赖 GPIO5 硬重启模组

sms_receiver
  - 接收 +CMT 事件
  - 接收 PDU 行
  - 解码 PDU
  - 合并长短信
  - 生成 SmsMessage

sms_queue
  - 保存待转发短信
  - 管理重试状态
  - 防止推送失败时丢短信

forwarder
  - HTTP/SMTP/其他通道推送
  - 超时控制
  - 失败重试

web_server
  - 状态查看
  - 配置管理
  - 日志查看
  - 提交 AT/Ping/发短信任务

health_monitor
  - AT 健康检查
  - 网络注册检查
  - 模组软件恢复
  - ESP32 重启兜底
```

## 重构测试清单

- [ ] ESP32C3 能通过 `Serial1` 发送 `AT` 并收到 `OK`。
- [x] 当前集成板上 `GPIO5` 不能控制 ML307R EN；拉低期间 `AT` 仍返回 `OK`。
- [ ] `AT+CMGF=0` 设置成功。
- [ ] `AT+CNMI=2,2,0,0,0` 设置成功。
- [ ] 收到短信时能识别 `+CMT:`。
- [ ] `+CMT:` 后的 PDU 行能被正确读取。
- [ ] PDU 能正确解码为发送者、时间和内容。
- [ ] Web 查询 AT 指令时不影响短信接收。
- [ ] Ping 测试不影响短信接收。
- [ ] 主动发短信不影响短信接收。
- [ ] HTTP 推送失败不会阻塞串口解析。
- [ ] 模组 AT 无响应时能执行 AT 软件恢复；当前集成板不支持已验证的 GPIO 硬重启。
