# 游戏边界与 Hook

涉及游戏对象、native 入口或写入时选读。当前接口与支持范围以实际 `SDK_DIR/README.md`、头文件及源实现为准。

## 就绪、线程和退出

`CoreReady`、DLL/导出已存在、窗口存在或 `Runtime.probe_open` 成功，只能证明各自的状态，不能证明 IL2CPP 的 GC 与线程注册设施已就绪。某些 getter 会隐式分配或初始化，尤其不能拿 `domain_get` 直接充当无副作用的启动探针。

参考当前 SDK 的 `applepie_il2cpp.h::Il2CppBootstrap`：核验目标构建的启动顺序后，通过 corlib 和既有 VM 线程的存在/数量判断能否开始 worker 元数据工作；不遍历借用的线程数组。它需要调用方提供退出互斥，不能单独构成生命周期保证。

将就绪检查、必要的自有线程 attach、元数据访问、结果发布和自有 attach 的 detach 放在可靠的 VM 访问边界内。退出入口先关闭新访问，等待已进入的有限区间结束；实现需分析重入与锁顺序，不能持有业务锁调用会等待同一锁的退出逻辑。检查一个 stop 布尔值之后直接访问 VM，并不能排除两者之间发生 shutdown。

仅 detach 本次实际拥有的 attach。元数据线程 attach 不授予 Unity 对象操作权限；对象查找、委托调用、骨骼写回等放在有证据支持的游戏阶段。证据通常来自已核对 ABI 的 native 回调及真实游戏窗口所属线程，窗口消息本身不足以证明阶段安全。

运行时关闭边界优先使用已协商的共享 shutdown/ApplicationQuit 契约。没有 shutdown 时，备用边界必须有实际支持证据，不能在后端状态未知时继续安装另一套 Hook。Stopping 关闭新调用；只有确实执行到对应终止边界后才报告 Dead。退出之后不再为了清理托管 root 调用已经停止的 VM。

只需要独立游戏服务的插件可以维护与该服务匹配的线程/对象证据；不要为只读 HUD 或输入功能抢占 Core 的单一当前角色 source。需要公共 Runtime/Write 票据时，则必须满足公共契约，不能用本地布尔值冒充票据。

## 查证方法与数据布局

对每个将调用或 Hook 的方法，记录并核验：

- 类/命名空间、实例或静态、重载、参数顺序和类型、返回类型、泛型/实例化情况。
- 隐藏 `MethodInfo*`、按值结构体、结构体返回缓冲区、引用/值类型区别及编译器 ABI。
- 字段所属类型、字段类型、偏移来源、数组/列表/字典的实际布局和上限。
- 原函数调用阶段、次数、返回值、输出参数以及适用的 `GetLastError`/`errno` 契约。

仅方法名与参数数量一致不足以确认类型。以 HUD 为例，`PreTick` 的 `float` 参数与指针使用不同参数传递规则；`ObjectPtr<Buff>` 的 16 字节值类型也不能随意改成裸 `void*`。

元数据缺失或代码裁切时，区分“无托管元数据”“native 实现仍存在”“只有 wrapper/缓存槽可达”。可用原生 wrapper、icall 或构建限定的地址作为有证据的适配；记录失败条件，不用模糊匹配到的第一个同名方法代替验证。

`runtime_invoke` 的托管异常输出要显式处理，成功返回的空值与调用失败分开。原始内存解引用、native 执行异常和托管异常是不同通道，SEH 不能把错误原型变成正确调用。

## 选择与安装 Hook

| 需求 | 路径及注意点 |
|---|---|
| 标准准确原型，插件独占业务 | `NativeHook<Tag, Function>`；Request 后在控制/初始化侧推进 Advance，OK 才确认激活 |
| 特殊 ABI、廉价参数筛选、系统 API 错误状态 | `Hook` 加准确入口，可复用 `APH_TypedNative`；入口本身仍承担计数与异常阶段 |
| 透传一次并观察参数/结果 | `SharedHook`，统一 native 签名、contract/version、payload、original policy |
| 标准终末地入口 | 原型确实匹配时使用 `applepie_endfield_adapters.h`，具体 payload 见 contracts 头 |
| 抑制 void original | 只有 `APH_CONTROLLED_VOID` 下唯一 controller 可在 BEFORE 返回 `APH_SUPPRESS` |
| 非 void 短路、返回值仲裁、任意指令/匿名代码 patch | 先确认公共接口是否支持；当前通用 Shared 不表达这些返回语义 |

注册对象、entry/context、original 槽具有模块期生命，每个 Tag/签名组合固定一个目标。激活前发布 original；多入口业务组全部准备完成后再发布 group-ready。Shared provider 的 native 入口在本插件停用后仍服务其他成员，不能随业务停用清空 original。

新游戏函数可以直接请求 Core，不需要 Core 逐个认识游戏函数。相同地址的请求应核对各插件真实行为，能组合才统一契约；签名字符串不是运行时 ABI 适配器，也不能自动接管未迁移的第三方补丁。

入口的 E9/EB/间接 JMP 可以来自原生跳板，本身不是外部补丁证据。保留 Core 按申请地址的管理，让后端判断可重定位性；失败时核对 request/prepare/activate 阶段、claim 和后端错误，不能仅因看到跳转就换地址继续装。

系统/运行库 Hook 要分析 Manager 的加载、日志、文件操作是否再次经过该入口。保留输入及有效输出 LastError 到最外层 finally 之后；通用 NativeHook 未承诺自动保存这些状态。避免在同一系统 Hook 内递归触发文件日志或相关 I/O。

## original、SEH 与共享 payload

入口维持 `enter_call → 准确原型执行 → __finally leave_call`。业务准入、native 已激活、递归标志分别检查。original 的 NotEntered / Running / Returned 状态决定异常后能做什么；原调用中抛出异常不能重放 original，也不能把失败改成成功返回。

默认采用 SDK 的 `APH_RECOVERY_NONE`。只有明确只读、没有锁/待恢复修改/未释放资源的业务阶段才可声明只读恢复；不能以读取为主就给混合读写回调使用 READ_ONLY。C++ 实现异常在本模块语言边界处理；不能越过 C ABI，也不能用 `/EHa` catch-all 吞掉硬件和 original 异常。

共享 payload 的复制不代表内部对象被保活。借用指针、委托和字符串只在契约声明期间有效；跨回调持有须有相应 root/handle，并同步使用、替换及释放。当前 Job 适配器的 AFTER 仅表示调度函数返回，不能据此释放作业仍在使用的数据。

## 目标、写入与恢复

Animation 和 Camera 当前是两个全局写域，不是每个角色一把锁。公开 SDK 尚无四角色 ActorContext 服务；四人小队索引不能证明实体一直相同，也不能绕开全局写域。新增多角色行为应在当前契约中明确协调，或列出需扩展的具体能力。

在用户明确开始播放/冻结/接管时申请 lease，每次实际写入仍需 `enter/leave`。保留 owner epoch、runtime epoch、target generation；多域按进入顺序逆序退出。获得原生 POD worker 的准入不等于获得 Unity/IL2CPP 调用权限。

保存首次修改前的原值、对象 root/身份、原始分配地址及对应域；不要用播放缓存覆盖恢复基线。GC root 不证明 Unity native `m_CachedPtr`、实例 ID、骨架或内部分配仍有效。不同对象类型使用有证据的验证方法，不把某个 Actor 的布局强加给全部对象。

切换目标先关闭旧操作、排空并恢复旧对象；保留旧清理上下文，同时单独持有新候选。旧域 Pending 时不能把新对象绑定到旧命令，不能提前让出未恢复的资源。恢复只确认本次捕获的 domain/lease/epoch，不能顺带清掉并发才开始释放的另一个域。

无写 lease 也可能仍持有需在游戏线程释放的只读 root。反之，初始化未共享的插件私有对象不自动需要 Animation/Camera；判断依据是被改变资源的所有权与公共契约。

## 指针槽与裁切路径

直接解析/调用 icall 与改写缓存函数指针槽是两种操作。存在裁切时保留所需 wrapper 路径，并分别记录 slot、明确的 expected/replacement、模块构建及原调用原型；不能把槽里偶然读到的值作为可信 expected。

当前 Pointer 服务要求 slot 是已加载 PE 内对齐、可写且非执行的数据，expected/replacement 是已加载 PE 的可执行地址，并需要相应 Runtime/目标/线程票据；不改变页保护，也不是通用堆内存/vtable 修改器。

临时路径使用 `APH_WithPointerPatch` 包围一次 wrapper 调用与 finally 恢复；持久 lease 也需显式 release。Core 用条件交换恢复自己的修改，预期不符或恢复不确定时保留占用与记录。不能绕过 Core 再直接赋值。

Core 能协调合作插件，但不能阻止游戏本身或重入调用在临时替换期间进入该槽。新功能必须核对安全执行阶段和并发条件；wrapper 异常不重试，成功返回后仍检查目标/epoch 是否过期。
