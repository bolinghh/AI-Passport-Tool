<p align="right"><strong>简体中文</strong> · <a href="mac-source-handoff.md">English</a></p>

# Mac 开发交接

快照日期：2026-10-14，Asia/Hong_Kong。先读根目录 `AGENTS.md`，再读本文与
产品 [README](../../products/codex-controller/README.zh_CN.md)。这是源码交接，不是已完成的 macOS 发布。
请保持目录结构：固件复用根目录 `components/bsp`，桌面与固件通过相对路径
引用根目录字体资产。

## 用户要求与当前优先级

产品是 AI Passport BLE Codex 会话控制器及虚拟麦克风桥接。上下键循环切换
最多 16 个配置会话；点击确认开始传送音频，再次点击停止。用户手动打开
Codex 语音输入。macOS 使用 BlackHole，无需自研虚拟声卡。硬件逻辑放共享
BSP，产品状态/UI/BLE/编码任务放产品 `main`。设备已有独立中文 UI，禁止
复用基线 demo 测试菜单。

最新开发要求：改善桌面端样式，修复连接时偶尔“先报错、再连接成功”。前一个
未解决要求：说话时设备电平变化，但 Codex 语音输入没有识别出文字。
请完成桌面优化并继续排查音频链路，不要把两项都描述为已经完成。

## 快照实际状态

| 范围 | 交接时状态 |
| --- | --- |
| 固件 | 已实现、构建，经明确批准在 Windows 烧录；下文精确固件归档随包提供 |
| 桌面端 | Rust/btleplug/CPAL/libopus/egui 实现；最后验证的 Windows 发布版通过 18 项测试 |
| 设备 BLE | Realtek USB 发现、加密认证配对、短时 GUI 心跳及独立 30 秒保存绑定重连通过 |
| macOS | 原生构建、蓝牙权限/配对、BlackHole 与真机验收均 **NOT RUN** |
| UI 优化 | 仅完成检查和诊断，仍运行 `main.rs` 原界面，没有新版 UI 交付 |
| 连接草稿 | `desktop/src/connection.rs` 已存在，但 `main.rs` **未导入**、`bridge.rs` **未使用**；其中三项测试 **未执行** |
| 讲话到 Codex | 用户报告的问题未解决，没有确认根因 |

最后一轮 UI 工作在用户按 Escape 停止 Windows 电脑操作后中断。除未接入的
连接状态机草稿外，桌面应用没有更新。不能把草稿当作可运行修复，也不能把
其测试计入此前 18 项测试通过结果。

原源码副本 **没有 `.git`**、分支历史或远程。Windows 门禁使用临时校验副本，
原目录没有初始化、提交、推送或发布。此前参考过 `demo/claude-buddy-port`
模式，但包中不含该分支 checkout/历史。参考档案在 `docs/reference/`，尤其
`shinku-chen/ble-voice-uplink-and-pairing.zh_CN.md`。

## 应继续利用的发现

### 连接与桌面界面

- 启动会自动连接 `Config.peripheral`；UI 按钮只依赖 `Status.connected`，
  连接期间仍可点击。重复 `Command::Connect` 会排队，在第一次会话开始后
  可能又发起连接；`session` 没有忽略相同设备请求。
- 新尝试没有始终清除旧错误；普通未连接状态显示泛化的配对失败提示。应区分
  扫描、连接/配对、同步、就绪、断开中和失败，并去重请求、拒绝过期完成事件。
  草稿提出基于 epoch 的状态转换，仍需接入、设计取消/清理并真正测试。
- 原生截图实际呈现浅色默认界面：控件偏小，标题拥挤，大量空白。`App::new`
  调用了 `set_visuals(dark())`，实际仍是浅色，需检查 eframe 主题处理。
  改善层级、间距、可读字体、状态/进度、空/错误状态及音频路由说明，保留三项功能。
- GUI/CLI 重叠曾导致认证错误。每次只保留一个 BLE 连接来源，不把竞争测试
  当作射频失败。

Windows 的 Impeccable 上下文加载器因运行组件/缓存不可用而失败，没有创建
正式产品/设计文件或获批的新 UI。这不是 Mac 的前置条件，不应阻塞开发。

### 音频诊断

- 用户确认缺少的是 **Codex 转写结果**，没有明确报告桥接或 Windows 输入端
  电平缺失。
- Windows 桥接选中 `CABLE Input`，系统存在 `CABLE Input` 输出端和
  `CABLE Output` 输入端；包中不包含个人配置。
- 采集期间日志约每 60 ms 一次通知，用户确认设备电平会变化。这不证明桌面
  解码音频、虚拟输入信号、语音可懂度或 Codex 输入设备选择正确。
- 之前本机 VB-CABLE 测试音录回通过：48 kHz、双声道、F32，约 995.3 Hz，
  最后静音峰值为零。没有包含 BLE 讲话，也未在最近转写失败时重复执行。
- GUI 使用后，24 KiB 音频任务的历史最小栈余量为 **452 字节**。未观察到
  崩溃，但音频/稳定性签收前必须检查负载。不要在无 PSRAM 板上任意增加所有
  任务栈，应检查 Opus 调用路径和 RAM 预算。
- 重点检查 `main/capture.c`、`main/link.c`、`desktop/src/bridge.rs`、
  `desktop/src/audio.rs`：流 ID、丢包、解码返回值、输出门控、队列预充/重采样、
  虚拟端点以及 Codex 手动选择的麦克风。测量电平时不要保存私人讲话。

### 已实现的 BLE 修复

固件原来把 Windows CCCD `0 -> 0` 初始化当作取消订阅，现在只在 `1 -> 0`
断开；C 测试覆盖四种转换。仍要求加密/认证 NUS、安全连接和持久绑定。
Windows 自定义 `ProvidePin` 配对使用桥接六位码窗口，原生 `GattSession`
保持整个协议会话，包括保存绑定重连。保留超时、取消和资源清理逻辑。
macOS 使用 CoreBluetooth/系统配对，Windows 实现不代表 Mac 已验收。

Windows 台式机的板载 Intel 没有外置天线，信号约 -98 至 -104 dBm。新 Realtek
USB Bluetooth 5.0 起初因 Windows 只支持一个活动无线收发器而报故障码 31。
经批准切换后 Realtek 故障码为 0，支持 BLE/中央角色，Passport 信号约 -66 dBm。
用户选择保留 Realtek 启用、Intel 停用。这是 Windows 证据，不要求在 Mac
停用任何设备。

## 包含内容与精确固件

ZIP 保留第一方源码、根 BSP/基线测试/门禁、文档/参考档案、仓库技能、必要
字体/许可和锁文件。生成产物只包含当前烧录固件及匹配调试归档，路径为：

`build/firmware/1f15a1fdd07d76de043dbc037e460e2673b86bc4d50e3338a19ca09f98bf45e9/`

上面相对于产品目录。从仓库根目录验证：

```bash
python3 tools/archive_firmware.py verify products/codex-controller/build/firmware/1f15a1fdd07d76de043dbc037e460e2673b86bc4d50e3338a19ca09f98bf45e9
```

| 身份 | 值 |
| --- | --- |
| 合并镜像 SHA-256 | `1f15a1fdd07d76de043dbc037e460e2673b86bc4d50e3338a19ca09f98bf45e9` |
| 匹配 ELF SHA-256 | `730d61dc2b413ac776028ee41467f4d45435118c22cface4feff35a207d6aaf1` |
| 匹配 MAP SHA-256 | `c242434ec68123d2506b362970757c3d344b8c432fe400b6985eae055bdf6878` |
| 合并大小 / 偏移 | 1,787,792 字节 / 0x0 |
| 最后 Windows 程序 SHA-256（不附程序） | `7bc79b6adb56f5f3a24ebf7517fa1d8894725d27e23e1fcce33a85a78c876a51` |

目标：ESP32-C3、ESP-IDF 5.5.3、8 MB Flash、无 PSRAM、DIO/80 MHz。
NVS 0x9000/0x6000、PHY 0xf000/0x1000、factory 0x10000/0x7f0000。
解码崩溃前先匹配此 ELF。历史 Windows COM3/地址不能直接作为 Mac 设备选择，
需重新发现 Mac 串口和当前 BLE 标识。

ZIP 排除构建缓存、下载的 Managed Components、Cargo target、node_modules、
Windows 工具链/程序、旧镜像、临时 Git 副本、原始运行日志、配对码/密钥、
用户设置和个人会话链接。Mac 需重新下载依赖。ZIP 顶层 `HANDOFF_MANIFEST.json`
记录各文件哈希/大小、执行权限与排除项，旁边 `.sha256` 校验完整 ZIP。

## Mac 启动与验证

1. 解压得到顶层 `ai-passport-codex-controller/`。将该目录作为 Codex 项目，
   先读 `AGENTS.md`。AI 应自行检查/安装 `skills/` 中五个必要 `passport-*`
   技能；`python3 tools/install_passport_skills.py --install` 是可选方法，
   随后运行 `--check` 并确认 Codex 能发现技能。保留既有用户技能/配置。
2. 检测 `uname -m`，使用 Apple Silicon 或 Intel 原生 Rust。桌面构建准备
   Python 3.11+、Rust、Xcode Command Line Tools、CMake。ESP-IDF 5.5.3
   和 ESP32-C3 环境按 `passport-setup` 与
   `docs/development/engineering/environment-setup.md` 准备，不复制 Windows
   路径，也不假定 Mac 已有工具。系统安装/权限遵守 Mac 当前环境要求。
3. 在产品目录运行：

   ```bash
   bash tools/build-macos.sh
   cargo clippy --locked --all-targets --manifest-path desktop/Cargo.toml -- -D warnings
   ```

   启动 `desktop/target/release/Passport Codex Controller.app`，其 `Info.plist`
   提供蓝牙/麦克风用途说明。按正常 Mac 用户流程授予必要权限。这是本机未签名
   应用，不是已签名/公证发布。若没有 BlackHole，单独安装；桥接输出和 Codex
   麦克风输入都选择对应 BlackHole 端点。
4. 产品主机/固件门禁，在产品目录执行：

   ```bash
   python3 tools/validate.py --host
   python3 tools/validate.py --firmware
   ```

   固件要求激活 ESP-IDF 5.5.3。无 Git 源码包的原仓库完整门禁，从根目录执行：

   ```bash
   python3 products/codex-controller/tools/source-gate.py --mode=--static
   python3 products/codex-controller/tools/source-gate.py --mode=--firmware
   python3 products/codex-controller/tools/source-gate.py --mode=--all
   ```

   只在可丢弃校验副本初始化 Git，原源码不动。未安装的依赖和 actionlint 需要
   网络。根固件门禁编译的是基线 demo，不要把其输出当成产品固件烧录。
   根主机门禁目前传递 `-Wl,--gc-sections` 等 GNU 链接参数，应检查 Mac 原生
   链接器兼容性，必要时移植调用，保留测试，不通过屏蔽测试解决。
5. 在 macOS 执行[逐项验收](../../products/codex-controller/VALIDATION.zh_CN.md)。先确保一个安全连接，使用
   明确选择的 BlackHole 输入/输出执行 `loopback`，然后验证设备讲话和手动
   Codex 语音输入。只开一个桥接实例。没有活跃的 Codex 语音输入会话时，
   不要承诺开启设备麦克风就能自动生成文字。

## 授权与报告

收到此包不代表批准烧录、全片擦除、取消绑定、修改驱动、提交、推送或发布。
以前 Windows 烧录批准不覆盖新的 Mac 烧录。烧录前将精确已验证镜像绑定到
新检测目标/串口，并确认存储数据影响。0x0 合并烧录可能重置 NVS/绑定；
不要求先备份原固件。

历史结果：Windows 固件/桌面 **Build PASS**，产品 C 与 18 项 Rust
**Host tests PASS**，**Device tests 仅记录的 BLE/UI 范围 PASS**。
原生 macOS 构建/测试和语音验收 **NOT RUN**。未完成草稿及 UI 改版不属于
这些通过结果。本次交接检查 ZIP CRC、SHA-256、相对依赖、双语文档和产物
身份，不把打包描述为重新构建了固件。

Mac 工作后分别报告 `Build`、`Host tests`、`Device tests`、`Unverified`。
更新中英权威记录，不把 Windows 历史 PASS 复制成 Mac 结果。保护用户音频/配置。

## 给 Mac Codex 的建议起始任务

在源码机器复现源码/调试包：
`python3 products/codex-controller/tools/package-handoff.py --date 20261014`。
它保留已存在 ZIP，并校验 CRC、逐文件哈希及 Unix 执行权限。

```text
先读 AGENTS.md 和 products/codex-controller/HANDOFF.zh_CN.md。自行验证/安装五个
必要 Passport 技能，准备原生 Mac 环境。保留源码快照和未接入的 connection.rs
草稿。完成桌面 UI 优化与连接请求/状态修复，再通过实际 BLE、解码和虚拟输入
电平诊断设备讲话为何没有进入 Codex 转写。执行原生测试/构建及仓库门禁，
把 Windows 历史证据与 Mac 结果分开报告。未经新的适用授权，不烧录、不取消
绑定、不提交、不推送、不发布。
```
