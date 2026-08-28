# xplugin 设计规范 v2 —— 通用 C 微内核插件框架

> 一句话:**用一份核心抽象(上下文 + 可逆副作用 + 服务注册表 + 事件总线 + 拓扑装配),让任何 C 项目获得"事务化装配组件"的能力;进程内直调与进程外隔离两种执行域共用同一契约。**
>
> 思想来源:HashiCorp go-plugin(契约、握手、进程隔离、宿主协调者)+ Cordis/DSH(薄内核、上下文、effect、服务+事件双原语、对等组件、拓扑依赖)。
>
> v2 相对 v1 蓝图(本文末尾"设计修正记录")的核心变化:**回应过度设计批评**——核心最小化(目标 ≤1500 行)、loader/remote 降为可选模块、按触发条件启用;所有注册 API 事务化(自动登记 undo),组件不再手写回滚。

---

## 目录

1. [目标与非目标](#1-目标与非目标)
2. [功能特点](#2-功能特点)
3. [总体架构:三档可裁剪](#3-总体架构三档可裁剪)
4. [公共契约](#4-公共契约)
5. [核心 API 规格](#5-核心-api-规格)
6. [确定性与成本模型](#6-确定性与成本模型)
7. [loader 模块(可选)](#7-loader-模块可选)
8. [remote 模块(可选)](#8-remote-模块可选)
9. [系统事件与可观测](#9-系统事件与可观测)
10. [C 语言风险清单](#10-c-语言风险清单)
11. [通用性:消费场景矩阵](#11-通用性消费场景矩阵)
12. [设计修正记录(v1 → v2)](#12-设计修正记录v1--v2)

---

## 1 目标与非目标

### 1.1 解决的痛点(任何 C 项目通用)

1. 模块耦合:新增扩展要改核心源码。
2. 扩展故障直接摧毁宿主进程(不可信代码无隔离)。
3. 装配顺序硬编码,无法按需启停组件。
4. 全局状态泛滥,无法多实例并行。
5. 卸载残留:注册了的东西没人撤销,资源/回调泄漏。
6. 两个矛盾诉求:**高性能直接调用** vs **故障安全隔离**。

### 1.2 非目标(边界)

- ❌ 内核态代码;纳秒级极致开销路径;频繁 dlclose 热卸载。
- ❌ 不做业务:内核不含任何领域逻辑,只做生命周期、依赖解析、分发。
- ❌ 不自带线程/事件循环:核心是纯同步、无 OS 依赖的算法库。
- ❌ 不定义值系统:事件 payload、服务参数类型全部由宿主定义。

---

## 2 功能特点

| # | 特点 | 说明 |
|---|------|------|
| 1 | **事务化装配** | 组件 install 失败自动逆序回滚已装部分;`ctx_destroy` 走同一条回放路径——卸载逻辑只写一遍,不可能是两套行为 |
| 2 | **可逆副作用内建** | `service_bind` / `bus_on` 等注册 API **自动**登记 undo;组件手写 effect 仅用于自定义资源(文件句柄、子进程) |
| 3 | **双执行域统一契约** | 进程内直调与进程外代理共用同一 vtable;业务代码零 if-else 区分本地/远程 |
| 4 | **版本握手内建** | 所有服务 vtable 首字段 `uint32_t version`;bind/resolve 阶段强制校验,major 不符即拒 |
| 5 | **服务/事件双原语,职责硬分离** | 服务=带返回值的能力调用;事件=无返回值广播;禁止拿事件做 RPC |
| 6 | **零依赖 C11,可裁剪三档** | core(无线程/无文件 IO,嵌入式可用)→ +loader(dlopen)→ +remote(进程隔离);链接什么用什么 |
| 7 | **分配器注入** | 宿主可整体替换 alloc/free,跨库堆隔离,destroy 全量回收无泄漏 |
| 8 | **确定性行为** | 拓扑序稳定(平局按注册序)、事件按订阅序同步派发、undo 严格逆序——同样输入永远同样装配结果 |
| 9 | **开销透明** | 每个 API 的复杂度/分配次数有公开成本表;事件零订阅快路径一次哈希探测(intern id 路径 ≤1 分支) |
| 10 | **故障隔离(remote 档)** | 子进程崩溃不影响宿主;服务失效发系统事件,可配置重启策略 |
| 11 | **宿主始终是协调者** | 库不 fork、不 exit、不打印日志(日志 sink 注入)、不隐式创建线程 |
| 12 | **一致性测试套件** | `xplugin-conformance`:第三方组件/插件用同一套用例验证行为合规 |

---

## 3 总体架构:三档可裁剪

```
┌──────────────────────────────────────────────────┐
│ 宿主应用(VM / 编译器 / 服务进程 / CLI 工具)         │
│ 只消费服务、订阅事件;不触碰组件内部符号               │
└───────────────────────┬──────────────────────────┘
                        │
┌───────────────────────▼──────────────────────────┐
│                core(必选,≤1500 行)                │
│  xpl_ctx │ effect 栈 │ 服务注册表 │ 事件总线        │
│  组件元数据 + 拓扑排序 + 事务化 install              │
├──────────────────────────────────────────────────┤
│         loader(可选,编译宏 XPLUGIN_LOADER)        │
│  dlopen/LoadLibrary · 元符号导出协议 · 失败回滚      │
├──────────────────────────────────────────────────┤
│        remote(可选,编译宏 XPLUGIN_REMOTE)         │
│  fork-exec supervisor · socketpair IPC · TLV 编解码 │
│  代理 vtable · 事件双向转发 · 崩溃检测/重启           │
└──────────────────────────────────────────────────┘
```

- **core**:纯算法库,唯一外部依赖 libc(且经分配器可替换);无线程、无文件、无信号——嵌入式裸机只要有分配器就能用。
- **loader**:在 core 上加动态库加载;平台差异(POSIX/Win)收口在本模块内。
- **remote**:在 core 上加进程隔离域;协议自包含,不依赖 loader(外进程组件是独立可执行文件)。
- 模块间依赖只允许向下;remote 不依赖 loader,loader 不依赖 remote。

---

## 4 公共契约

### 4.1 命名与版本

- API 前缀 `xpl_`,公共宏 `XPL_*`,单公共头 `include/xplugin/xplugin.h`(loader/remote 各自附加头 `xplugin/loader.h`、`xplugin/remote.h`)。
- 库版本 `XPLUGIN_VERSION_MAJOR/MINOR`;ABI 契约版本 `XPLUGIN_ABI_VERSION`(当前 1)。
- 版本号打包:`(major<<16)|minor`;握手规则 `xpl_version_compatible(have, need)`:**major 必须相等,provider minor ≥ 需求 minor**。

### 4.2 错误模型

```c
typedef enum xpl_status {
    XPL_OK       = 0,
    XPL_ENOMEM   = 1,   /* 分配失败(经注入分配器) */
    XPL_EINVAL   = 2,   /* 参数非法(NULL name、空 install 等) */
    XPL_ENOTFOUND= 3,   /* 服务/组件/事件不存在 */
    XPL_EVERSION = 4,   /* ABI 版本握手失败 */
    XPL_ECYCLE   = 5,   /* 依赖环 */
    XPL_EDUP     = 6,   /* 服务名重复提供 */
    XPL_EDEP     = 7,   /* 依赖缺失(install 时报全缺失清单) */
    XPL_EFAILED  = 8,   /* 组件 install 回调返回失败 */
    XPL_ECAP     = 9,   /* effect 栈/表容量耗尽 */
    XPL_ESTALE   = 10,  /* remote:子进程失效/通道断开 */
    XPL_EPROTO   = 11   /* remote:协议帧错误 */
} xpl_status;
```

- 全部 API 返回 `xpl_status`(查询类返回指针,NULL+状态可查)。
- 诊断:`xpl_ctx_last_error(ctx, char* buf, size_t n)` 取 ctx 局部的最近错误详情——**不依赖 errno,无线程局部存储**(与 4.5 线程模型一致)。
- 库内禁止 abort/exit/裸 assert;`XPL_ASSERT` 仅 Debug 编译,失败走日志 sink 不终止。

### 4.3 分配器

```c
typedef struct xpl_allocator {
    void* (*alloc)(void* user, size_t size);
    void* (*realloc)(void* user, void* p, size_t size);
    void  (*free)(void* user, void* p);
    void* user;
} xpl_allocator;            /* 传 NULL = libc 默认 */
```

- 框架**全部**堆分配经此收口(注册表节点、事件订阅、effect 栈、组件记录)。
- 宿主的业务对象不归框架管(如 xvm 的 GC 堆对象走 `gc_alloc_typed`,框架不经手)。
- 分配失败统一映射 `XPL_ENOMEM`,不 abort。

### 4.4 生命周期状态机(CAS 收口)

```
xpl_ctx:   NEW ──install()──► RUNNING ──destroy()──► DESTROYING ──► DESTROYED
组件实例:   LOADED ──install 成功──► INSTALLED
                                 └─install 失败──► FAILED(已回滚,无残留)
remote 子进程: SPAWNING ─► HELLO_OK ─► SERVING ─► (崩溃/退出) DEAD ─(重启)─► SPAWNING
```

- 流转唯一经内部 `xpl_cas` 收口点,生命周期字段直写是违规(静态可查)。
- `ctx_destroy` 可重入保护:DESTROYING 状态下再次调用返回 `XPL_EINVAL` 并记日志。

### 4.5 线程模型

- **core/ctx 非线程安全**,单线程所有权;多线程宿主每线程一个 ctx,或自行外部加锁——文档明示,不假装安全。
- remote 的 supervisor 串行化对子进程的调用(v1 同步请求-应答;异步池为 v2 扩展)。
- loader 的 dlopen/LoadLibrary 本身线程安全(平台保证)。

### 4.6 日志

```c
typedef void (*xpl_log_fn)(void* user, int level, const char* fmt, va_list ap);
void xpl_ctx_set_log(xpl_ctx*, xpl_log_fn, void* user);   /* 默认:静默 */
```

库内任何输出必须经此 sink;默认零输出。

---

## 5 核心 API 规格

> 完整签名以下为准;实现细节见 `roadmap.md` 各里程碑。

### 5.1 上下文与 effect 栈

```c
typedef struct xpl_ctx xpl_ctx;

xpl_ctx* xpl_ctx_new(const xpl_allocator* a);
xpl_status xpl_ctx_destroy(xpl_ctx* ctx);      /* 逆序回放全部 effect,回收资源 */

/* effect:自定义可逆副作用(注册类 API 已内建,此接口供文件句柄/子进程等) */
typedef void (*xpl_undo_fn)(xpl_ctx* ctx, void* userdata);

xpl_status xpl_effect_push(xpl_ctx*, xpl_undo_fn undo, void* userdata);
size_t     xpl_effect_mark(xpl_ctx*);                  /* 记录回滚水位 */
xpl_status xpl_effect_rollback_to(xpl_ctx*, size_t mark); /* 逆序回放到水位 */
```

### 5.2 服务注册表

```c
/* 所有服务 vtable 的公共序言:version 必须是第一个成员 */
typedef struct xpl_any_service {
    uint32_t version;
} xpl_any_service;

xpl_status   xpl_service_bind(xpl_ctx*, const char* name, void* service_vtable);
void*        xpl_service_resolve(xpl_ctx*, const char* name, uint32_t need_version);
xpl_status   xpl_service_unbind(xpl_ctx*, const char* name);

int          xpl_version_compatible(uint32_t have, uint32_t need);
```

- `bind`:读 `((xpl_any_service*)vt)->version` 与核心 ABI 比对(约束:version ≥1);重名 `XPL_EDUP`;**自动 push undo**。
- `resolve`:哈希查找 + 版本握手;不匹配返回 NULL 并置 `XPL_EVERSION`。
- 替换语义:先 `unbind` 再 `bind`(均带 undo);bind/unbound 各发系统事件(§9),持缓存指针的宿主监听 `xpl:service:unbound` 失效重取。
- 服务名规范:`<域>.<名称>`,长度 ≤63,字符集 `[a-z0-9._]`;`xpl:*` 事件/服务前缀保留给框架。

### 5.3 事件总线

```c
typedef void (*xpl_event_fn)(xpl_ctx* ctx, const char* name,
                             void* payload, void* userdata);

xpl_status xpl_bus_on(xpl_ctx*, const char* name, xpl_event_fn fn, void* userdata);
void       xpl_bus_off(xpl_ctx*, const char* name, xpl_event_fn fn, void* userdata);

/* 快路径:事件名驻留为 int id,emit 只查订阅计数 */
int  xpl_bus_intern(xpl_ctx*, const char* name);
void xpl_bus_emit(xpl_ctx*, const char* name, void* payload);   /* 通用路径 */
void xpl_bus_emit_id(xpl_ctx*, int event_id, void* payload);    /* 零订阅≤1分支 */
```

- 同步派发,按订阅先后顺序;回调内禁止 bind/unbind/emit 同名事件(重入保护,违者记日志并忽略该次操作)。
- `bus_on` 自动 push undo;`bus_off` 幂等。
- payload 类型由事件名约定(文档责任归宿主);remote 域跨进程 payload 需注册 codec(§8.4)。

### 5.4 组件与拓扑装配

```c
typedef struct xpl_component {
    const char* name;            /* 组件名(非服务名) */
    const char* const* provides; /* 提供的服务名,以 NULL 结尾 */
    const char* const* requires; /* 依赖的服务名,以 NULL 结尾 */
    xpl_status (*install)(xpl_ctx* ctx, void* userdata);
    void* userdata;
} xpl_component;

xpl_status xpl_components_add(xpl_ctx*, const xpl_component* c);
xpl_status xpl_components_install(xpl_ctx*);
```

- `install` 前置校验:重复 provides(`XPL_EDUP`)、依赖缺失(`XPL_EDEP`,错误详情列全缺失项)、依赖环(`XPL_ECYCLE`)。
- 排序:Kahn 算法,**平局按 `components_add` 注册序**——输出稳定可复现。
- **事务语义**:`install` 开始记 `effect_mark`;任一组件返回非 OK 或框架出错 → `rollback_to(mark)` → 已装组件的注册全部撤销,返回 `XPL_EFAILED` + 详情。成功路径零特殊分支,失败路径与 destroy 同一实现。

---

## 6 确定性与成本模型

| 操作 | 复杂度 | 堆分配次数 | 备注 |
|------|--------|-----------|------|
| `xpl_service_bind` | O(1) 均摊 | 1(表节点)+1(undo) | 名字长度 ≤63,复制存储 |
| `xpl_service_resolve` | O(1) 均摊 | 0 | 热路径零分配 |
| `xpl_bus_on/off` | O(1) 均摊 | 1 / 0 | |
| `xpl_bus_emit`(通用) | O(哈希探测 + 该事件订阅数) | 0 | 零订阅 = 一次探测即返回 |
| `xpl_bus_emit_id` | O(该事件订阅数) | 0 | 零订阅 ≤1 分支(读计数) |
| `xpl_components_install` | O(V+E) | 组件数相关 | Kahn 稳定排序 |
| `ctx_destroy` | O(总 effect 数) | 0(只回放) | 逆序 |
| remote 调用(代理) | 1×RTT + 序列化 | 视 codec | v1 同步 |

确定性保证(同样输入 → 同样行为):

1. 拓扑序唯一(平局按注册序)。
2. 事件派发序 = 订阅序。
3. undo 序 = 注册序严格逆序。
4. 无隐藏哈希随机化(自研哈希,固定种子——单进程内无 HashDoS 威胁面,remote 协议层自带帧校验)。

---

## 7 loader 模块(可选)

`XPLUGIN_LOADER` 编译宏启用;公共头 `xplugin/loader.h`。

```c
typedef struct xpl_loaded_lib xpl_loaded_lib;

/* 扫描目录(后缀 .so/.dll/.dylib),逐个 dlopen 读取元符号并入 components 待装队列 */
xpl_status xpl_loader_scan_dir(xpl_ctx*, const char* dir);
/* 精确加载单个库 */
xpl_status xpl_loader_load(xpl_ctx*, const char* path);
```

插件侧导出协议(唯一符号):

```c
/* 插件源码内一行导出;内部确保 -fvisibility=hidden 下唯一外部符号 */
XPLUGIN_COMPONENT_EXPORT(my_plugin_name)
```

- 元符号:`const xpl_component* xplugin_component(void);`,宏生成,名字固定。
- 加载失败(dlopen 错误/符号缺失/校验失败)→ 回滚该库全部登记,ctx 完好。
- **不做可靠 dlclose**:库句柄保持到进程退出(代码段/静态变量残留问题);"卸载"仅指 effect 逻辑撤销。文档明示。
- 平台差异(dlopen/LoadLibrary)收口在 `loader/platform_{posix,win}.c`,公共头零平台宏。

---

## 8 remote 模块(可选)

`XPLUGIN_REMOTE` 启用;公共头 `xplugin/remote.h`。

### 8.1 进程模型

- 组件编译为独立可执行文件;宿主 `xpl_remote_spawn` fork-exec 启动,`socketpair`(或 Windows 匿名管道)作字节流;不用 TCP 端口。
- 子进程内跑裁剪版 core(同代码库编译开关),拥有自己的注册表/总线;IPC 只是进程边界桥梁。
- **宿主是协调者**:库不隐式 spawn——`xpl_remote_spawn` 由宿主显式调用。

### 8.2 协议(TLV 帧)

```
帧: magic "XPL1" | msg_type | req_id | payload_len | payload(TLV)
TLV: tag(varint) | len(varint) | bytes
消息: HELLO{core_ver, abi} / HELLO_OK / HELLO_ERR
     CALL{service, op, args_tlv} / RET{status, ret_tlv} / ERR
     EVENT{name, payload_tlv}          (双向)
     SUBSCRIBE{name} / UNSUBSCRIBE
     PING / PONG / SHUTDOWN
```

- 小端;varint 采用 LEB128;帧头定长 12B。
- v1 同步请求-应答(req_id 预留多路复用);校验和 v2 再议(本地 socketpair 通道,非不可信网络)。

### 8.3 代理 vtable

- 宿主侧 `service_bind` 绑定的是**代理 vtable**:每个函数做 序列化 → 帧发送 → 等应答 → 反序列化。
- v1 手写代理 + 宏样板辅助(`XPL_REMOTE_PROXY_*`);代码生成器归 M5+。
- 约束:**支持 remote 的服务接口禁止裸 C 指针参数**;只能传可序列化 blob(长度+字节)或值类型——代理生成宏在编译期以类型 trait 拒绝裸指针(静态断言)。

### 8.4 事件穿透与 codec

- 跨进程事件 payload 必须注册 codec:`xpl_remote_codec(ctx, event_name, encode_fn, decode_fn)`;未注册的 emit 跨域时丢弃并 WARN(经日志 sink)。
- 子进程 emit → supervisor 转发到宿主总线;宿主 emit → 转发给已 SUBSCRIBE 的子进程。

### 8.5 故障处理

- supervisor 监控子进程(管道 EOF / waitpid);崩溃 → 全部其代理服务标记失效 → 系统事件 `xpl:plugin:died{lib, exit_code}` → 按策略(不重启/重启 N 次/重启不限)重新 spawn。
- 失效后调用代理服务返回 `XPL_ESTALE`;**宿主进程不崩**。

---

## 9 系统事件与可观测

框架自身经事件总线暴露生命周期(前缀 `xpl:`,普通订阅方式):

| 事件 | 时机 | payload |
|------|------|---------|
| `xpl:service:bound` | bind 成功 | `const char* name` |
| `xpl:service:unbound` | unbind | 同上 |
| `xpl:component:installed` | 单组件装好 | `const char* name` |
| `xpl:component:failed` | install 失败(含回滚完成) | `struct { const char* name; xpl_status why; char detail[128]; }` |
| `xpl:plugin:died` | remote 子进程死亡 | `struct { const char* lib; int exit_code; }` |
| `xpl:plugin:restarted` | 重启完成 | 同上 |

宿主可在此之上自建 trace(每次服务调用的包装代理、耗时统计)——框架不内建性能计数(v2 扩展方向)。

---

## 10 C 语言风险清单

| # | 风险 | 规避 |
|---|------|------|
| 1 | 组件内可变全局/静态 | 规范禁止;多 ctx 并行测试用例强制验证 |
| 2 | dlopen 无内存隔离 | 不可信代码走 remote 域;进程内崩溃无法防御(文档明示) |
| 3 | effect 撤销顺序错乱 | 栈语义唯一实现;rollback 与 destroy 共用同一段回放代码 |
| 4 | ABI 演进破坏兼容 | vtable 只允许尾部追加字段;version major/minor 语义见 4.1;禁止原地改旧字段 |
| 5 | 动态库符号污染 | `-fvisibility=hidden` + 唯一元符号;loader 提供 nm 自检脚本 |
| 6 | 跨库堆不匹配 | 全部框架分配走注入 allocator;宿主对象不经框架分配 |
| 7 | 事件总线被当 RPC | 文档 + conformance 用例:事件回调无返回值语义 |
| 8 | remote 传裸指针 | 代理宏编译期静态断言拒绝;接口规范要求 blob |
| 9 | dlclose 残留 | 不做可靠卸载,句柄留到进程退出 |
| 10 | ctx 误跨线程 | 线程模型文档化;DEBUG 构建加 owner-tid 断言 |
| 11 | 重入破坏注册表 | bus 回调内禁 mutating API,记日志忽略 |

---

## 11 通用性:消费场景矩阵

| 场景 | 用到的档位 | 典型用法 |
|------|-----------|---------|
| VM/解释器(xvm) | core(远期 +remote) | executor 切换、观测订阅、校验策略 |
| 编译器/链接器 | core + loader | Pass 流水线拓扑装配、重定位处理器 |
| 服务进程 | core + loader | 配置驱动的模块启停;灰度替换实现 |
| CLI 工具 | core + remote | 不可信格式解析器放子进程,崩溃不杀 CLI |
| 测试框架 | core | fixture 组件按依赖装配,用例结束自动拆卸 |
| 嵌入式 | 仅 core | 静态组件表 + 自定义 allocator,零 OS 依赖 |
| 游戏/图形引擎 | core + loader | 平台后端、资源 codec 插件 |

跨场景不变量:**core 五件套 API 不变;宿主对象模型/值系统完全自由**(payload 与 vtable 参数类型归宿主定义,框架只见 `void*` 与 blob)。

---

## 12 设计修正记录(v1 → v2)

> v1 = 本文档前一版蓝图(完整保留于 git 历史);v2 修正均源自"是否过度设计"的评审结论。

| # | v1 蓝图 | v2 修正 | 理由 |
|---|---------|---------|------|
| 1 | 组件每次注册**手动** `ctx_effect` 登记 undo | 注册类 API(bind/on)**自动**登记 undo,手写 effect 仅限自定义资源 | 手动登记必被遗漏;事务性应是 API 的属性,不是使用者的义务 |
| 2 | 服务多实现 + 优先级查找进核心 | 移出 v1 核心(平级多实现);保留 unbind+bind 替换语义 | 零个已知消费者;优先级查找是投机抽象 |
| 3 | 单一大框架整体采用 | 三档可裁剪:core / +loader / +remote,编译宏控制 | 嵌入式与 xvm 类宿主只需 core;隔离与动态加载是两种独立需求 |
| 4 | `vm:instr:*` 高频事件、"所有 opcode 都是组件" | 高频路径明确排除;事件快路径 intern id 化 | 每 Hz>1e6 的接口不进间接层是硬约束 |
| 5 | destroy 与失败清理两套逻辑 | install 失败回滚 = destroy 同一 `rollback` 实现 | 两套清理路径必然漂移 |
| 6 | 未定义线程模型 | 明确 core 非线程安全 + owner 断言 + supervisor 串行化 | 假装线程安全比明说不安全危险 |
| 7 | 隐含可用任意分配器/日志 | allocator 收口 + 日志 sink 注入 + 默认静默 | 库纪律:不打日志、不 fork、不 exit |
| 8 | payload/值类型隐含宿主语义 | 框架只见 `void*`/blob;跨进程 codec 由宿主注册 | 通用性关键:框架不定义值系统 |
| 9 | 固定 P0-P5 全量实施计划 | 触发式启用(见 roadmap.md §0);core 之外模块各有准入触发条件 | 防止为集成而集成 |
| 10 | 目标 ≤ 无规模约束 | core ≤1500 行 / loader ≤600 / remote ≤2000 硬预算 | 规模是过度设计的可测量防线 |

---

## 附录 A:v1 蓝图保留要点(业务映射与流程,通用性素材)

> v2 正文按"通用框架"重写后,以下 v1 蓝图的应用场景与流程要点原样保留于此——它们是 §11 消费矩阵的具体化,也是宿主接入时的参考形态。

### A.1 虚拟机场景

1. 微内核 ctx 不内置任何 Opcode 处理器;指令集扩展是普通组件(高频路径约束见 §12-4 修正)。
2. 组件 install 注册 opcode 处理器;注册 API 自动登记 undo。
3. `vm.executor` 作为可替换命名服务,实现解释器/JIT 后端切换(unbind+bind)。
4. 事件钩子 `vm:instr:before/after`(仅观测构建)、`gc:begin`,组件实现 trace、断点、性能统计。

### A.2 编译器 Pass 流水线

1. 全部 Pass 作为组件,注册到服务注册表(如 `compiler.pass.ir_opt`)。
2. 通过 requires/provides 声明依赖;框架拓扑排序自动生成流水线顺序。
3. Pass 执行前后抛 `pass:before/after` 事件;外部组件监听实现 IR 打印、校验、改写。
4. 新增优化 Pass 不需要修改编译器主循环源码。

### A.3 链接器场景

1. 不同架构重定位处理器作为可插拔命名服务(如 `linker.reloc.x86_64`)。
2. 重定位、符号解析各阶段抛 `reloc:before/after` 事件;插件实现符号审计、map 导出、段合法性校验。

### A.4 完整启动执行流程

1. 创建 `xpl_ctx`,注入分配器(或默认 libc)。
2. 收集组件:`components_add` 静态组件;loader 档另加 `loader_scan_dir` 动态组件;remote 档 `remote_spawn` 外进程组件。
3. `components_install`:校验(重名/缺失/环)→ 拓扑排序 → 按序 install;注册类 API 自动登记 undo。
4. 任一步失败:自动逆序回滚到 install 前水位,ctx 完好可重试。
5. 上层业务 `xpl_service_resolve` 获取服务 vtable,执行业务;订阅事件。
6. `xpl_ctx_destroy`:逆序回放全部 effect,自动撤销注册,释放资源。

### A.5 服务命名与典型事件示例

服务名:`vm.executor` / `compiler.pass.ir_opt` / `linker.reloc.x86_64` / `parser.syntax`
典型事件:`pass:before|after`、`reloc:before|after`、`vm:instr:before|after`、`gc:begin`

### A.6 演进方向全集(M5 逐项触发,含两项 v2 未排期的)

v2 已排期(roadmap M5):配置系统、trace 观测、semver 范围、子进程沙箱、代理代码生成、多路复用异步。
保留待触发:

7. **调试诊断模式**:强制把原本进程内的组件代理到外进程隔离域运行,用于捕获内存越界——作为调试工具(依赖 remote 档 + 代理透明性)。
8. **有限快照能力**:对 ctx 状态做有限快照,用于装配过程重放调试(依赖:注册表/总线/组件表可序列化)。
