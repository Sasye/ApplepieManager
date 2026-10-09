# 功能模式与源码索引

以下为按功能选读的源码模式。路径以各仓库根目录为起点，函数/类型名可用 `rg` 定位；仓库不在使用者机器上时，按本文模式和实际 SDK 开发，不依赖这些本地目录才能使用 skill。

EIEM、AntiKick、CombatHUD、SynchroFocus 与 Poser master 可参考已接入的客户端代码。Poser mmd/mirror 本地分支仍含 DllMain 启动和旧 Hook/扩展接入，下面取其业务分层与数据设计；新功能的加载和资源契约按当前 SDK 实现。

## 只读 HUD 与数据面板

参考 `EndfieldCombatHUD`：

- `src/applepie_hud.h`：`FilteredBuffHook`、`BuffRef`、`GameReadAllowed`。
- `src/skill_data.h`：`SkillHudState`、`hkSkillBtnPreTick`。
- `src/combat_hud.cpp`：搜索 `emptyNode`、`newBuffs`，查看普通 UI/GPUI 数据源选择与快照发布。

在合法的游戏回调读取借用的对象，尽快转成插件拥有的数值/文本快照；绘制线程短暂同步后复制快照，锁外绘制。缓存身份用于筛选，不作为稍后解引用的裸对象缓存。

采样频率可以低于绘制频率，按数据变化需求限制反射开销。示例技能状态约每 33ms 采样一次，这不是所有 HUD 的固定要求。保留早期 self/参数筛选，避免全场景 Buff 都进入昂贵业务。

“有效空列表”“读取失败”“当前来源未使用”要分开。有效空列表应清除旧内容；某来源空但备用来源仍有效时，保留原数据源优先级，不能误清空。区分未就绪、无数据与数值真的为 0。

验证重点：实际 float/结构体参数、原函数一次、UI 关闭/重建、空数据更新、备用来源选择、跨线程一致快照、晚加载以及采样次数。

## 基础设置与旧配置导入

先参考当前 SDK 的 `examples/config_plugin.cpp`：模块期 `applepie::Config`、schema 注册、`SettingsChanged` 通知与插件本地缓存。业务循环读取缓存，示例中的 `refresh_ms` 是业务设置，不是配置修订号查询周期。

已有插件接入可选读：

- `EndfieldCombatHUD/src/combat_hud.cpp`：`RegisterConfig`、`ConfigChanged`，关注 HUD 开关、两个快捷键与业务缓存的映射。
- `EndfieldSynchroFocus/src/focus_config.h`：`RegisterConfig`、`ConfigChanged`，关注旧文件导入条件、初始快照、保存通知和输入设置应用。

按目标插件的字段、单位和键位语义接入，不直接沿用示例配置 ID。新文件优先；只有新文件缺失时才导入旧文件，成功保存后才算完成。基础设置由 Manager 的原生“配置”入口自动保存，插件自己的 HUD/复杂界面继续独立工作。

验证重点：录入小键盘键等非字母键后立即生效、清除绑定、重启恢复、按住旧键期间改绑、停用时修改再启用，以及保存失败不会应用未落盘的新值。Manager 自动迁入自己的旧配置不等于会迁入插件配置。

## 输入、快捷键和成对动作

参考 `EndfieldSynchroFocus`：

- `src/managed_input.h`：`InputModel`、`FindPair`，通过实际输入表发现已存在绑定。
- `src/synchro_focus.cpp`：`ActionState`、`Held`、`BeginHeld`、`EndHeld`、`InputTick`。

分别保存物理键状态与已成功提交的逻辑动作。按下前持有本次 press/end 委托和绑定身份；只有成功提交才转为 Pressed。释放、失焦、配置切换、绑定删除、停用都需完成对应旧动作，不只清一个布尔值。

新绑定不能接收旧绑定的释放。提交中失败或重入删除时，保留 Pressing/Releasing/Uncertain 等足以表达真实结果的状态，不能盲目重试。重新启用时仍按住的键是否视为新按下要明确设计，避免意外重复触发。

输入发现、观察与动画写权是不同职责；无需让这类功能等 EIEM 的当前角色。使用本插件保存的键沿状态，避免依赖可能被另一插件消费的全局按键边沿。

验证重点：先有绑定再启用、按住期间切换绑定/配置/焦点、停用后释放、重入回调、失败结果不重放、与其他输入监听同时工作。

## 周期任务与系统 API 包装

参考 `EndfieldAntiKick`：

- `src/managed_runtime.h`：`WorkerAttachReady`、`MethodIs`、`FindMethod`。
- `src/anti_afk.cpp`：`OnUpdate`、`InitializeWorker`、`Disabling`、`Restore`。
- `src/managed_hooks.h`：`FileHook::Entry` 的准确签名、错误状态和 finally 计数。

计时、配置和到期判断可在本地完成；需要游戏对象或内部服务的实际调用在已验证的游戏回调执行。到期前只走廉价路径，不每帧重新扫描元数据、分配对象或写文件。

会话、sender、控制器等会变化，使用前重验；必要时在一次回调内取得、持有并释放，避免长期缓存失效会话。已经提交的外部副作用不能靠停用“撤销”，结果不确定时也不自动重放。

系统 API Hook 需保存输入及有效输出 LastError，考虑 Core 自己也调用该 API；详见[游戏边界与 Hook](game-boundaries.md)。初始化 worker 的取消事件与状态发布需覆盖 Enable/Disable 交错。

验证重点：VM 启动不同阶段、取消/超时、会话重建、只有到期才执行业务、系统 API 返回值与错误码、重入及真实 worker 排空。

## 动画、相机与多阶段写入

参考 `EIEM`：

- `src/applepie_hooks.h`：`EiemBipedListener`、`EiemSMCListener`、`EiemMorphListener`，区分共享阶段及目标筛选。
- `src/applepie_write.h`：`EiemInvokePoseScoped`；`src/trojan.h` 的三处调用保留裁切所需的 wrapper 路径。
- `src/applepie_lifecycle.h`：`EiemDisabling`、`EiemRestoreBody`、恢复阶段及 Pending 状态。

分开设计资源异步加载、运行状态、动画写权、相机写权和停止恢复。插件启用不等于有角色；播放停止也不等于插件停用。后台载入只产出自有数据，游戏对象的准备与写入在合适边界执行。

对不同写者标出顺序：动画、IK、SMC、布料/job。确认写入发生在谁之前或之后、谁会覆盖它；调度返回不代表 job 结束。仅对实际需要的写者和当前对象处理，不为了减少冲突关闭全场景系统。

停止先关新生产，再排空异步任务/作业、恢复实际改过的状态，最后释放对应域。准备记录保留到恢复成功，避免先清空 ready 或资源后丢失恢复入口。重复播放应创建新的业务任务身份，不能沿用已取消任务的等待状态。

验证重点：三条适用 slot 路径、播放→停止→再播放、相机独立开关、异步载入期间停止、切目标、同域争用、不同域清理交错及每帧业务次数。

## 手动编辑与可恢复命令

参考 `Endfield-Poser-master`：

- `src/core/applepie_commands.h`：`PoserCommand`、`PoserRunSlot`、`PoserCancelCommands`。
- `src/core/applepie_restore.h`：`PoserBaseline`、`PoserBeforeInvoke`、`PoserRestoreJournal`。
- `src/core/applepie_discovery.h`、`src/core/applepie_runtime.h`：目标发现和访问条件。

UI/Web 请求复制到有界命令槽，通过 serial 与 epoch 区分重用；Core 消息只传槽身份。游戏阶段取出命令并复验票据，再执行 setter，结果以同一 serial 返回。UI 不持有可直接操作的骨骼对象。

首次写某项属性时保存原值与有效对象身份。Animation、Camera 分域记录；用匹配的 getter/setter 或有证据的字段布局恢复，不能把不同组件都当成 Behaviour 调 `enabled`。大块原值快照还应保存内部分配地址/长度，以免恢复进已重建的数组。

渲染可继续展示不可用原因；命令被拒绝时反馈实际权限、对象或状态原因，不靠伪造成功掩盖。复原是游戏资源恢复，不能用“关闭面板”替代。

验证重点：冻结/解冻、关闭 UI 后恢复、排队期间切人/停用、同槽复用旧 serial 被拒、实际 receiver 类型、恢复失败留存，以及再次编辑。

## 播放器、校准及数据资产

参考 `Endfield-Poser-mmd`：

- `src/math/mmd_motion.h`：`Timeline`；`src/math/mmd_audio_sync.h`：主时钟与音频同步。
- `src/math/character_face.h`：`Read`、`FindMorph`、`Bind`；`src/math/mmd_face_controls.h`：带 owner/generation 的状态。
- `src/game/mmd_player.h`：`MmdLoadResult`、`MmdSession`、载入与运行态。

文件解析、插值、坐标转换和校准计算与游戏写回分离。明确局部/世界/身体空间、单位、手性、四元数顺序、休止姿及相对增量，纯逻辑可用构造数据验证。

定义权威时间轴以及暂停、seek、循环和偏移语义；音频独立采样时钟不应在每个绘制帧被强制 seek。异步文件结果带本次请求身份，只有仍匹配选择/角色代次时才应用。

校准文件按 schema 版本、模型/骨架匹配、数量和有限数值验证。名称归一化只用于匹配，保留原名；遇到歧义不随意选第一个。缺失校准只影响依赖它的功能，提供明确状态与用户可选的已有回退。

验证重点：解析边界、空轨道、暂停/seek/循环、跨模型校准拒绝、字符名称歧义、坐标基准与中性姿、切角色后的旧载入结果，以及停用期间异步任务排空。

## 网络同步、多实体与扩展服务

参考 `Endfield-Poser-mirror`：

- `extensions/poser_mirror/net_protocol.h`：`Classify`、`ValidPose`、`Newer`。
- `extensions/poser_mirror/net_session.h`、`pose_buffer.h`：数据交换、会话和延迟插值。
- `extensions/poser_mirror/poser_mirror.cpp`：`AssignSlots`；`src/game/puppet.h`：`Acquire`、`Apply`、`Release`。
- `sdk/poser_extension.h`、`src/core/extension_host.h`：该分支自己的 POD 扩展接口。

网络线程处理连接、收发和协议校验，游戏线程只交换已经复制的状态。包检查长度、版本、来源、会话、序号、数值范围及有效骨骼掩码；乱序/重复、超时与重连都要有定义。时间戳缓冲与短弧四元数插值属于业务层，不放进 Core。

远端玩家 ID、小队槽位、实体实例是不同身份。映射变化、队员变成操控角色、场景/骨架变动时关闭旧应用并恢复该实体；不能仅用 0–3 槽号长期绑定对象。当前公共 SDK 的两个全局写域不能直接当作四角色独立权限。

该分支 `Session::stop` 在等待超时后关闭线程句柄的路径不能证明 worker 已结束。新实现应可唤醒取消并确认退出，未完成时保持 Pending 和上下文，不能复制这个分支作为热停用完成条件。

新插件之间的纯 POD 业务服务优先使用当前 SDK 的依赖声明与版本化服务：必需依赖未 Active 则等待，可选依赖缺失则处理未提供；服务调用同时参与双方排空，提供方重新启用后旧句柄失效，使用方重新查找。当前服务同步执行且每个输入/输出上限 4096 字节，GAME_THREAD 策略只检查线程、不调度、不授予写权。

现有 Mirror 的 UI 回调、整套实体操作和宿主阶段并不能仅换成 service invoke 就自动等价。设计扩展时明确每个操作的线程、资源归属、消息布局和停止语义；更大的数据采用有界分块/独立协议，保留版本与消息身份，不跨 DLL 传 STL 或裸对象指针。

验证重点：畸形/重复/乱序包、序号回绕、超时重连、槽位重新分配、网络取消排空；服务提供方缺失/停用/重新启用、旧 handle、游戏线程策略及输入大小限制。回环协议测试与真实局域网、游戏姿态效果分别报告。
