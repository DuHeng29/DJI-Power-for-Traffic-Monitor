# DJI Power for TrafficMonitor

一个面向 Windows 的原生 [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) 插件，通过本地蓝牙低功耗（BLE）或 DJI Cloud 读取 DJI Power 户外电源的实时状态。

插件以单个 DLL 运行，不依赖 Python、Home Assistant 或额外后台进程。本地 BLE 模式中，DJI 账号仅用于首次获取 Pair Key，之后无需云端；DJI Cloud 模式通过 HTTPS 获取短期 MQTT 凭据，再通过 TLS 接收设备遥测，需要电脑和电源均联网。

```text
TrafficMonitor → DJIPowerPlugin.dll → Windows BLE → DJI Power
                                 → DJI Cloud MQTT（TLS）→ DJI Power
```

> [!IMPORTANT]
> 本项目是非官方社区项目，与 DJI 无隶属、合作或认可关系。项目使用逆向分析得到的非公开协议，设备固件或 DJI 账号接口更新可能导致部分功能失效。

## 功能

- 扫描附近的 DJI Power，结合广播名称、DJI 厂商数据、Service UUID 和已保存地址识别设备
- 使用 32 位十六进制 Pair Key 完成 `0x5A/0x6A` BLE 挑战鉴权
- 保持本地 BLE 长连接，接收约 1 Hz 的 `0x5A/0x61` 遥测推送
- 断线检测、自动重连和指数退避
- 向 TrafficMonitor 提供电量、输入功率、输出功率、净功率和剩余时间五个项目
- 支持手工粘贴 Pair Key
- 支持 DJI Home 账号、密码、图片验证码和可选二次验证码登录，一次性获取 Pair Key
- 兼容 `US_`、`CN_` 及其他合法大写区域前缀的 DJI Home Member Token
- 登录后始终显示独立设备选择窗口；只有一台设备时也需要确认
- 多台绑定设备可按名称和序列号选择
- Pair Key 使用当前 Windows 用户的 DPAPI 加密保存
- 原生 Win32 设置界面，支持 DPI 和 ClearType
- 可选择 DJI Cloud，以 Member Token 换取短期 MQTT 凭据并订阅实时遥测
- Cloud 支持账号登录选择设备，也支持手工填写完整 Token、SN 和 API 区域
- Cloud Token 使用当前 Windows 用户的 DPAPI 加密保存；短期 MQTT 凭据到期前自动更新
- Cloud 断线自动退避重连；Token 失效提示重新登录；超过 3 分钟没有遥测时显示未知值

## 支持的设备

| 型号 | 广播代码 | 状态 |
| --- | ---: | --- |
| DJI Power 1000 | `0x91` | 已实现识别，仍需更多实机验证 |
| DJI Power 1000 V2 | `0x97` | 已实现识别，仍需更多实机验证 |
| DJI Power 1000 Mini | `0x98` | 已实现识别，仍需更多实机验证 |
| DJI Power 2000 | `0x94` | 已实现识别，仍需更多实机验证 |

不同型号或固件版本的遥测字段可能存在差异。提交 Issue 时请说明型号、固件版本和脱敏后的现象。

## 当前限制

- 一次只连接和监控一台 DJI Power；账号多设备功能用于选择绑定对象，不代表同时监控多台
- 只实现鉴权和只读遥测，不提供电源控制功能
- DJI Power 通常只允许一个 BLE Central 同时连接；插件连接期间，手机应用可能无法连接同一设备
- DJI Home 登录接口不是公开 API，可能随时发生变化
- 已实测真实账号登录和 Pair Key 获取；长期 BLE 稳定性仍需更多设备验证

## 系统要求

- Windows 10 或 Windows 11
- 可用的 BLE 适配器
- 支持插件的 TrafficMonitor（插件机制自 1.82 起提供）
- 插件架构与 TrafficMonitor 一致，例如 x64 对应 x64

## 安装

1. 获取或自行构建 `DJIPowerPlugin.dll`。
2. 创建以下目录并放入 DLL：

   ```text
   TrafficMonitor/
   └─ plugins/
      └─ DJIPowerPlugin/
         └─ DJIPowerPlugin.dll
   ```

3. 重启 TrafficMonitor，在插件管理中确认插件已加载。
4. 打开插件设置，扫描并配置设备。

## 配置 Pair Key

### 使用 DJI Home 账号

1. 在插件设置页点击“账号登录”。
2. 输入账号、密码和图片验证码；需要时填写二次验证码。
3. 登录成功后，在独立窗口中确认设备。即使只有一台，该步骤也不会跳过。
4. Pair Key 回填后，选择扫描到的 BLE 设备并点击“测试连接”。
5. 测试成功后点击“确定”保存。

本地 BLE 模式的账号登录只用于读取 Pair Key，Member Token 不落盘。Cloud 模式会额外保存经 DPAPI 加密的 Member Token，以便启动后重新连接；账号、密码和验证码不写入配置。

### DJI Cloud 模式

1. 在设置中选择“DJI Cloud”。
2. 点击“账号登录”，完成验证码验证后选择设备，自动填写 Token 和 SN；也可手工填写完整 Member Token（如 `CN_…`、`US_…`）及电源序列号。
3. 选择 API 区域，默认与参考项目一致为海外 VG；国内账号可尝试国内 HZ。
4. 点击“测试连接”，确认收到遥测后点击“确定”。保存的 Token 使用 DPAPI 加密；取消设置会恢复原监控方式。

Cloud 模式仅监控所选 SN，不占用本地 BLE 连接。电源必须已在 DJI Home 中联网，且账号可访问该设备。状态显示“已连接，等待设备遥测”只表示 MQTT 订阅成功；收到遥测后才显示数值。退出 DJI Home 账号或令牌失效后，可能需要重新登录。

长期 Token 仅发送到白名单中的 `home-api.djigate.com`、`home-api-vg.djigate.com` 或 `home-api-hz.djigate.com`，不跟随 HTTP 重定向。MQTT 使用短期凭据，仅连接 DJI API 返回的 `*.djigate.com` 或 `*.dji.com` 地址，并验证 TLS 证书。当前仅实现监控项需要的遥测，不轮询未展示的 REST 容量、固件等设置。

### 手工配置

如果已有 Pair Key，可直接粘贴 32 位十六进制字符串，再选择 BLE 设备并测试连接。

## 隐私与安全

- 本地模式的遥测只通过 BLE 传输；Cloud 模式的遥测经过 DJI 云端，本项目没有服务器
- 不包含遥测收集、崩溃上报或用户统计
- 账号登录直接连接 DJI 账号及 DJI Home 服务
- 持久化设备名称、蓝牙地址、连接选项、云端 SN/API 区域，以及经 DPAPI 加密的 Pair Key 和 Cloud Member Token
- 不要在 Issue、日志或截图中公开账号、密码、Member Token、Pair Key 或完整序列号

## 从源码构建

需要 Visual Studio 2022 或更新版本（安装“使用 C++ 的桌面开发”）、Windows 10/11 SDK 和 CMake 3.24+。

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

产物位于 `build/bin/Release/DJIPowerPlugin.dll`。CMake 默认从 TrafficMonitor 官方仓库下载 `PluginInterface.h`；也可以指定固定 SDK：

```powershell
cmake -S . -B build -A x64 `
  -DTRAFFICMONITOR_SDK_DIR=C:\src\TrafficMonitor\include
```

## 测试与诊断

自动化测试覆盖 DUML 帧与 CRC、挑战鉴权及状态码 03 重试、Cloud JSON 字段、MQTT 分片/帧/订阅、域名和输入验证、Cloud 停止生命周期、DPAPI 配置往返、DJI Home 移动端签名、区域 Token、多设备列表、DLL 导出和五个监控项目。Cloud 测试使用合成数据，不使用真实账号；设备兼容性及真实云端连通性需实机验证。

```powershell
# 扫描附近设备 12 秒
.\build\bin\Release\ble_scan_probe.exe

# 只获取图片验证码，不提交账号或密码
.\build\bin\Release\cloud_login_probe.exe

# 与插件使用相同登录及设备选择流程
.\build\bin\Release\DJI认证调试器.exe
```

## 项目结构

```text
src/
├─ ble/       Windows BLE 扫描、鉴权、遥测与重连
├─ cloud/     DJI Home 登录、Pair Key 查询及 Cloud HTTPS/MQTT 遥测
├─ common/    配置、DPAPI 和遥测快照
├─ plugin/    TrafficMonitor 插件接口与监控项目
├─ protocol/  DUML 帧、CRC 和遥测解析
└─ ui/        Win32 设置、登录和设备选择窗口
tests/        自动化测试
tools/        独立认证调试工具
```

## 参与贡献

欢迎提交 Issue 和 Pull Request。提交前请确保 Release 构建及全部测试通过，不包含真实账号、Token、Pair Key 或设备序列号；新增协议字段时请说明设备型号、固件版本和验证方式，并在重要实现处添加必要的中文注释。

## 致谢

- [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor)：提供插件宿主、插件接口和开发文档，使 DJI Power 数据能够显示在任务栏与主窗口中。
- [ha-dji-power-ble](https://github.com/zuyan9/ha-dji-power-ble)：为 DJI Power 本地 BLE、DUML 通信、设备识别和通过 DJI Home 一次性获取 Pair Key 提供了重要灵感及协议验证参考。
- [dji-power-home-assistant](https://github.com/trfr3ak/dji-power-home-assistant)：提供 DJI Cloud MQTT 凭据接口、订阅主题及 `device_osd` 字段映射参考。本项目依据协议行为独立实现原生 C++ 通道，未复制其 Python 源码。

感谢以上项目的维护者和贡献者。本仓库不打包参考项目源码；第三方项目、接口文件和依赖继续适用其各自许可证。

## 许可证

本项目以 [MIT License](LICENSE) 开源。

DJI、DJI Power 和 DJI Home 是其各自权利人的商标。本许可证只适用于本仓库原创代码，不授予任何 DJI 固件、服务、商标或其他第三方材料的权利。
