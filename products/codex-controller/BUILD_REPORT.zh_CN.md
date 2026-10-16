<p align="right"><strong>简体中文</strong> · <a href="BUILD_REPORT.md">English</a></p>

# 构建与交付报告

验证日期：2026-10-14。产品版本：0.1.0，BLE 修复检查点。原目录仍没有 `.git`；
仓库门禁在带临时 Git 元数据的可丢弃副本执行。没有提交、推送或发布。

## 分项结果

| 字段 | 结果 | 范围 |
| --- | --- | --- |
| Build | PASS | ESP-IDF 5.5.3 独立 ESP32-C3 固件；Windows x64 发布程序；合并布局与匹配 ELF/MAP 校验 |
| Host tests | PASS | 产品 C/CTest，含 CCCD 四种转换；18 项 Rust 测试、格式与禁止警告的 Clippy；仓库静态、固件及完整门禁 |
| Device tests | PASS（局部 BLE 范围） | 已批准的 COM3 烧录/写入校验与启动；Realtek USB BLE 发现、认证配对、GUI 连接约 45 秒待机心跳、保存绑定加密握手和独立 30 秒双向心跳诊断。完整验收矩阵仍未完成 |
| Unverified | 待执行 | 完整按键/UI/协议矩阵、MTU 与故障注入；语音路由及真实 Codex 控制/语音输入；取消、关闭蓝牙、休眠恢复、权限拒绝；100 次开关/20 次重连/30 分钟采集；macOS 原生构建与真机项目 |

用户曾在 2026-10-13 初始镜像确认中文控制台和长按确认进出设置正常。
当时 Windows VB-CABLE 本机录回通过：48 kHz、双声道、F32，1 kHz 测试音
测得约 995.3 Hz，最后 500 ms 峰值为零。这些历史项目未在本次 BLE 修复中
重复执行，不证明 Passport 讲话到 Codex 的完整链路。macOS 仍为 **NOT RUN**。

## 修复与证据

Windows 访问状态为 **Allowed**，蓝牙已开启。固件把 Windows 初始化 CCCD 的
`0 -> 0` 当成取消订阅，在配对期间终止连接。现在只在 `1 -> 0` 停止连接；
四种转换均有主机回归测试。仍要求加密、身份认证、安全连接和持久绑定。

桥接扫描不再使用后端的广播 UUID 过滤，在应用层检查 NUS UUID。Windows
改用自定义 `ProvidePin` 配对和认证加密，由桥接提供六位码输入框。首次配对
和保存绑定重连均用原生 `GattSession` 保持物理连接，并保留到协议会话结束。
后端连接完成就释放它曾复现后续写入超时；现已增加会话资源生命周期回归测试。
取消配对会移除处理器、完成延迟回调、取消系统操作和清空输入。错误保留步骤
及底层详情，写入超时标明消息类型和分片。

用户确认 `6f6485a7...` 程序首次配对成功。之后 `a3b7a61b...` 检查点仍在连接后
超时，已由最终版本替代。下列最终程序通过保存绑定的加密握手和 30 秒双向心跳
诊断，设备日志确认 `encrypted=1 authenticated=1 bonded=1`。诊断结束主动
断开，这属于清理，不是意外断线。完整稳定性矩阵仍待执行。未记录配对码、
绑定密钥、个人链接或音频。

后续 GUI 验证未完成：发现设备变得间歇性，重启后一次服务发现没有 RX 特征。
扫描 RSSI 约为 -98 至 -104 dBm。只读电脑检测确认 Intel 蓝牙已启用、故障码为
0、服务运行、访问 Allowed，前 12 小时没有相关错误或警告。用户确认华硕
ROG STRIX X570-I GAMING 台式机没有接外置天线；其[厂商手册](https://dlcdnets.asus.com/pub/ASUS/mb/SocketAM4/ROG_STRIX_X570-I_GAMING/E15891_ROG_STRIX_X570-I_GAMING_UM_V2_WEB.pdf)
要求在后面无线接口安装附带天线。缺少天线是有依据的射频假设，不证明电脑
或 Passport 无线模块损坏。需接好天线或改用独立 BLE 适配器，再重复发现、
持续 GUI 连接及重连验收。之前短时通过不能视为端到端连接已稳定。
检测中未修改驱动或系统蓝牙设置。

### Realtek USB 适配器复测

2026-10-14，用户接入 Realtek Bluetooth 5.0 USB 适配器，驱动版本为
1.9.1038.3002。板载 Intel 同时启用时，新适配器故障码为 31；Windows BTHUSB
事件 6 明确提示只支持一个活动蓝牙无线收发器。用户批准后，停用板载 Intel，
重新启用 Realtek，故障码变为 0。Windows 确认它支持 BLE 和中央角色。

Realtek 发现 Passport 时信号约 -66 dBm，之前板载结果约 -98 至 -104 dBm。
设备日志确认认证加密配对成功，GUI 连接持续约 45 秒待机心跳。一轮 GUI/CLI
重叠诊断出现认证错误，不能作为独立重连结果。用户关闭全部 GUI 实例后，
最终程序独立通过发现、保存绑定的加密 NUS 握手和 30 秒双向心跳，麦克风
保持关闭。结束后主动断开，并释放 COM3。未移除固件、驱动或绑定。

测试后用户明确选择保留 Realtek 启用、板载 Intel 停用。此结果解决了本次
USB 配置下的发现阻碍，不证明板载无线模块损坏。实际距离、讲话到 Codex
输入和完整稳定性矩阵仍未验收。GUI 使用后，音频任务报告的历史栈余量为
452 字节；独立待机诊断没有运行采集。音频/稳定性签收前需检查采集负载下
的栈使用，不能把 BLE 通过视为音频通过。

Windows 流程依据微软的[自定义配对说明](https://learn.microsoft.com/en-us/windows/uwp/devices-sensors/pair-devices)、
[桌面 API 限制](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/desktop-to-uwp-supported-api)
和 [MaintainConnection 约定](https://learn.microsoft.com/en-us/uwp/api/windows.devices.bluetooth.genericattributeprofile.gattsession.maintainconnection?view=winrt-26100)。

## 交付产物

路径相对于产品目录。生成文件保持忽略。包附中英使用/协议/验收文档及依赖
许可。初始 ZIP 在 `build/initial-delivery/` 保留历史证据，不是当前交付版本。

| 产物 | SHA-256 |
| --- | --- |
| `build/codex-controller-firmware.zip` | `46c1656a7101663fb860039cfd55a0eeaa637ebae32ec43716b515ed3f06fd11` |
| `build/desktop-windows.zip` | `843113bc8dba9e61686819cf39ce0f6ced1f026301d20fe8f5d9ac414ec3d7fe` |
| `build/desktop-windows/passport-codex-bridge.exe` | `7bc79b6adb56f5f3a24ebf7517fa1d8894725d27e23e1fcce33a85a78c876a51` |
| 0x0 合并镜像 | `1f15a1fdd07d76de043dbc037e460e2673b86bc4d50e3338a19ca09f98bf45e9` |
| 匹配固件 ELF | `730d61dc2b413ac776028ee41467f4d45435118c22cface4feff35a207d6aaf1` |
| 匹配固件 MAP | `c242434ec68123d2506b362970757c3d344b8c432fe400b6985eae055bdf6878` |

已验证固件归档为 `build/firmware/1f15a1fdd07d76de043dbc037e460e2673b86bc4d50e3338a19ca09f98bf45e9/`。
清单关联应用描述符、保留 ELF/MAP、分项镜像、合并字节、偏移和 8 MB 容量。
桌面清单记录程序/文档/许可哈希；固件 ZIP 在 `firmware/` 保留该归档，含调试
产物和分项镜像。

## 工具链、布局与运行范围

ESP-IDF v5.5.3、RISC-V GCC esp-14.2.0_20251107、LVGL 9.5.0、定点
`78/esp-opus` 1.0.5；ESP32-C3，无 PSRAM，DIO 80 MHz，8 MB Flash。
NVS 仍为 0x9000/0x6000，PHY 0xf000/0x1000，factory 0x10000/0x7f0000。
应用镜像 1,722,256 字节，小于 8,323,072 字节分区；合并镜像 1,787,792 字节。
中文字体仍包含 6,874 字形，16 px、2 bpp。

2026-10-14，用户明确批准向 COM3 烧录此精确镜像并重置 NVS/绑定。
esptool 4.12.0 确认 ESP32-C3 v1.1、8 MB Flash，写入哈希校验通过。
从 0x0 写至 0x001b478f，扇区擦除至 0x001b4fff，没有全片擦除。
启动 ELF 身份与 `730d61dc2b41...` 一致。最终桌面连接修复未再次烧录固件。

最终待机心跳检查中，采样空闲堆为 55,808–55,836 字节，最大连续块 47,104
字节。主任务/音频/BLE 发送栈余量为 4,404 / 至少 24,260 / 1,308 字节。
有限日志窗口未出现崩溃/重启。这是配对后待机数据；采集/编码负载和 60 ms
编码时限仍待验证。结束后关闭串口观察并释放 COM3。

Windows 使用 Rust 1.96.0、MSVC C++ Build Tools 和 CMake 3.30.2，程序需要
x64 Microsoft Visual C++ 运行库。不附虚拟声卡安装器。Windows 使用 VB-CABLE，
macOS 使用开源 BlackHole。轻量桥接提供 Passport BLE/会话控制及 Opus 路由，
无需自研虚拟声卡驱动。

## 剩余验收

按[八项验收表](VALIDATION.zh_CN.md#逐项验收)继续。Windows 已绑定诊断先退出
GUI，再运行 `passport-codex-bridge ble-check <扫描标识>`；首次配对使用 GUI。
诊断不修改配置、不打开 Codex 链接、不采集音频。本机声卡验证使用
`passport-codex-bridge loopback "CABLE Input" "CABLE Output"`。
实际导航需在 Codex 核对；设备显示配置目标，没有读取 Codex 当前页面。
端到端讲话验收前，手动开启语音输入并选择虚拟麦克风。
