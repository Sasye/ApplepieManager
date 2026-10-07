# ApplepieManager

[English](README_EN.md) | 中文

为《明日方舟：终末地》设计的 DLL 插件管理器。Manager 内置 Core，提供新版插件加载、独占与 typed 共享 Hook、受管指针槽及窗口路由，以及异步逻辑启停和动画/相机独立写权；保留独立 DX11 面板和 UID 隐藏功能。插件接入协议见 [SDK](sdk/README.md)。

## 主要功能

- **插件管理**：CoreReady 后扫描 `plugin\`，核对新版清单、ABI、重复 ID、必要能力；分别显示加载、接入和活动状态
- **通用 Hook**：动态独占与 typed 共享目标、两阶段激活、同址去重、准确原型与 SEH 薄层；SDK 位于 `sdk/`
- **共享服务**：PointerPatch 条件恢复、统一游戏窗口链、最小 Runtime/目标代次票据
- **启停/写权**：运行时按钮异步加载、启用和停用已适配插件；确认恢复后才显示 Disabled；Enable 不授予动画/相机写权，DLL 保持常驻
- **启动配置**：面板勾选项控制下次启动，与本次运行的启停按钮分开
- **UID隐藏**：隐藏游戏内 UID、延迟显示、菜单 UID/名字、名片 UID/名字
- **多语言界面**：Manager 面板中文/英文切换

支持可选插件依赖与版本化服务、失败详情及有界日志轮换。SDK 1.1 兼容现有 1.0 插件；内置 UID 的游戏对象操作在已验证的游戏线程执行。接口与示例见 [SDK 说明](sdk/README.md)。

## 用户协议与免责声明

<details>
<summary>在下载、安装或使用本插件之前，请您仔细阅读本协议。<b>使用本插件即代表您已完整阅读、充分理解并同意遵守以下所有条款。</b></summary>

### 1. 开源许可与最终用户权利
- 本插件基于 **AGPL-3.0** 许可证在 GitHub 平台完全开源。用户可在遵守该许可证的前提下自由使用、修改和分发本插件的源代码。
- 最终用户（End User）在不对本插件进行修改的前提下，使用和分发本插件**不受任何限制**。此权利不因用户是否违反本协议而改变。

### 2. 反欺诈声明
- 您**不得**在网络销售平台公然售卖本插件**软件本体**且未提供 GitHub 仓库地址与售后服务。
- 本插件在 GitHub 平台完全免费开源，如果您是付费购买获取的，请知悉本插件可从 GitHub 免费获取。

### 3. 内容合规与行为约束
- 本插件本身不包含任何游戏美术资产。用户知悉并同意，《明日方舟：终末地》游戏内置的官方动画、场景、模型等资产其版权完全隶属于鹰角网络，并不适用 AGPL-3.0 协议。

### 4. 风险与免责声明
- 本项目仅供学习、技术研究和交流目的。本插件中使用的明日方舟游戏数据资产版权均隶属于鹰角网络。使用本工具可能违反游戏服务条款，存在账号封禁的风险。因使用本插件而直接或间接导致的任何损失（包括但不限于账号封禁、游戏数据损坏等），**本项目不承担任何法律或经济责任**。用户需自行承担所有风险，强烈建议您在测试账号上运行。

</details>

## 安装

将以下文件复制到游戏目录（`Endfield.exe` 所在文件夹）：

```
bin/applepie_manager.dll   → 游戏目录/plugin/applepie_manager.dll
bin/vulkan-1.dll           → 游戏目录/vulkan-1.dll
bin/d3dcompiler_47.dll     → 游戏目录/d3dcompiler_47.dll
```

> `d3dcompiler_47.dll`（DX）和 `vulkan-1.dll`（Vulkan）只引导 Manager，二者放其一或全放均可。插件通过 SDK 接入，不提供旧代理、旧业务包或旧宿主桥接。

> 如果您**没有安装过此类型的插件**，您可能需要自行创建plugin文件夹。

## 使用方法

1. 按上述方式安装后启动游戏。
2. 按 `HOME` 键打开/关闭管理器面板。
3. 查看插件加载/Attach/能力状态，设置下次启动是否加载，并调整 UID 隐藏选项。
4. 点击面板语言切换按钮切换语言。

## 插件开发

插件可使用 [C++ 客户端封装](sdk/applepie_client.h)，需要常用终末地共享 Hook 时再包含[可选适配器](sdk/applepie_endfield_adapters.h)；底层 [C API](sdk/applepie.h) 继续开放。接入协议与可编译示例见 [SDK 说明](sdk/README.md)。运行 `sdk\examples\build.bat` 编译最小插件，加 `/verify` 可运行独立的离线验证，不会启动或部署到游戏。

迁移旧插件时，可让编码代理使用 [Applepie 插件迁移 skill](skills/applepie-plugin-migration/SKILL.md)，按实际操作选择接入方式，保留特殊 Hook、slot 和业务恢复逻辑。

安装 Visual Studio 的 C++ 桌面开发工具后，在项目根目录运行 `build.bat`。正式构建生成 `bin/applepie_manager.dll`、`bin/d3dcompiler_47.dll` 和 `bin/vulkan-1.dll`。
