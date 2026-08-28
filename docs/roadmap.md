# xplugin 实施路线图

> 详细实现路径与分阶段任务清单。设计依据 `design.md` v2(三档可裁剪 + 触发式启用)。
> 工时为净编码估算(含测试,不含评审返工);单位"人日"(pd)。

---

## 0 实施原则

1. **每个里程碑独立可测、可交付、可停**——任意时刻停下,已完成的都是有用资产。
2. **触发式启用**:core(M0-M2)无条件实施;loader(M3)、remote(M4)各有准入触发条件,未触发不排期。生态增强(M5)逐项触发。
   - loader 触发:出现第一个"宿主之外的使用者想独立发布组件"的需求。
   - remote 触发:出现真正不可信的宿主侧扩展,或宿主被插件崩溃杀死 ≥2 次。
3. **规模预算是验收项**:core ≤1500 行 / loader ≤600 / remote ≤2000(不含测试);超预算需评审砍功能而不是放宽。
4. 工程纪律对齐 xvm:`-Wall -Wextra -Wpedantic -Werror`、ASan+TSan 打底、Conventional Commits、每函数一权威实现、收口点原则。
5. API 冻结点:M1 结束冻结核心 API;此后只允许尾部追加(design §10-4)。

---

## 1 里程碑总览

| 里程碑 | 内容 | 工时 | 依赖 | 产出 |
|--------|------|------|------|------|
| **M0** | 工程地基 | 0.5 pd | — | 可构建、可测试、CI 门 |
| **M1** | 核心内核(五件套) | 4-5 pd | M0 | `libxplugin_core.a` + 核心 API 冻结 |
| M1a | ctx + effect 栈 | 1 pd | M0 | 事务原语 |
| M1b | 服务注册表 + 版本握手 | 1 pd | M1a | bind/resolve |
| M1c | 事件总线 | 0.5-1 pd | M1a | on/off/emit + intern id |
| M1d | 拓扑装配 + 事务 install | 1.5 pd | M1a-c | components_add/install |
| **M2** | 示例 + 一致性套件 | 1.5 pd | M1 | examples + conformance 用例 |
| **M3** | loader 模块(触发门) | 2-3 pd | M1 | dlopen 加载 + 样例插件 |
| **M4** | remote 模块(触发门) | 6-8 pd | M1 | 进程隔离域全链路 |
| M4a | TLV 编解码 + 帧协议 | 1.5 pd | M1 | proto.c + 模糊测试 |
| M4b | 子进程骨架 + 握手 | 1.5 pd | M4a | spawn/HELLO |
| M4c | 代理调用 + 服务导出 | 2 pd | M4b | 远程服务调用 |
| M4d | 事件转发 + 崩溃检测/重启 | 1.5-2 pd | M4c | 双向事件 + 容错 |
| **M5** | 生态增强(逐项触发) | 按项 | 各异 | config/trace/semver/sandbox/codegen |

目录规划(随 M0 建立,M1-M4 逐级填充):

```
xplugin/
├── include/xplugin/xplugin.h        # core 公共头(M1)
├── include/xplugin/loader.h         # M3
├── include/xplugin/remote.h         # M4
├── src/core/
│   ├── internal.h                   # ctx 布局/收口点声明
│   ├── ctx.c                        # M1a 生命周期 + last_error + 日志 sink
│   ├── effect.c                     # M1a effect 栈
│   ├── registry.c                   # M1b 服务表(自研小哈希)
│   ├── bus.c                        # M1c 事件订阅表 + intern
│   └── topo.c                       # M1d Kahn + 环检测 + 事务 install
├── src/loader/
│   ├── loader.c                     # M3 扫描/加载/回滚
│   └── platform_{posix,win}.c       # M3 dlopen/LoadLibrary 收口
├── src/remote/
│   ├── proto.c                      # M4a 帧 + TLV
│   ├── codec.c                      # M4a 基础类型编解码
│   ├── child_main.c                 # M4b 子进程裁剪内核入口
│   ├── supervisor.c                 # M4b/d spawn/监控/重启
│   └── proxy.c                      # M4c 代理样板宏的实现支撑
├── examples/
│   ├── pipeline/                    # M2 静态组件流水线(编译器 Pass 形态)
│   ├── observer/                    # M2 事件订阅/系统事件观测
│   ├── dyn_plugin/                  # M3 dlopen 插件样例
│   └── untrusted_parser/            # M4 不可信解析器隔离样例
└── tests/
    ├── test_ctx.c  test_effect.c  test_registry.c
    ├── test_bus.c  test_topo.c    test_conformance.c
    ├── test_loader.c              # M3
    ├── test_proto_fuzz.c  test_remote_e2e.c  test_remote_crash.c  # M4
    └── util.h
```

---

## 2 M0 工程地基(0.5 pd)

**任务**

- [ ] CMake:C11、`-Wall -Wextra -Wpedantic -Werror`(MSVC `/W4 /WX`)、Debug/Release 两配置;目标 `xplugin_core`(空)。
- [ ] 编译宏骨架:`XPLUGIN_LOADER` / `XPLUGIN_REMOTE`(默认 OFF)。
- [ ] 测试脚手架:极简断言宏 + ctest 注册;`cmake --build build --target verify` = 构建 + ctest。
- [ ] `.clang-format`(kernel 缩进 K&R,对齐 xvm)、`.gitignore`、LICENSE 确认。
- [ ] CI(如启用):零警告构建 + ASan/TSan 两趟。

**验收**:空库全平台编译零警告;`verify` target 跑通;hello-world 单测过。

---

## 3 M1 核心内核(4-5 pd)

### M1a ctx + effect(1 pd)

**实现**

- [ ] `xpl_ctx` 结构:allocator(拷贝)、effect 栈(动态数组,容量起步 16,倍增,失败 `XPL_ECAP`/`XPL_ENOMEM`)、registry/bus/组件表句柄(后续里程碑挂)、last_error 缓冲、日志 sink、lifecycle 字段 + `xpl_cas` 收口。
- [ ] `xpl_ctx_new`(allocator NULL → libc 包装)、`xpl_ctx_destroy`(状态机 DESTROYING → 回放 effect 逆序 → 释放自身 → DESTROYED;重入返回 `XPL_EINVAL`)。
- [ ] `xpl_effect_push/mark/rollback_to`;`rollback_to` 与 destroy 的回放共用同一内部函数 `effect_unwind(ctx, upto)`(一函数一权威)。
- [ ] `xpl_ctx_last_error`、`xpl_ctx_set_log`。

**测试**

- [ ] effect 逆序回放顺序断言(记录 undo 调用序)。
- [ ] rollback_to 水位正确:只回滚 mark 之后的。
- [ ] destroy 后句柄不可再用(owner 断言/ASan 验证 UAF 由调用方负责,但库内不再触)。
- [ ] 自定义 allocator 全程接管分配计数:destroy 后 alloc-free 配平。
- [ ] 多 ctx 并行互不干扰(各 1000 effect)。
- [ ] 容量耗尽路径(注入失败分配器)。

### M1b 服务注册表 + 版本握手(1 pd)

**实现**

- [ ] 自研开放寻址小哈希(FNV-1a,固定种子,装载因子 0.75 扩容);名字复制存储,≤63 字节 + 字符集校验(`XPL_EINVAL`)。
- [ ] `xpl_service_bind`:读 `xpl_any_service.version` ≥1;重名 `XPL_EDUP`;自动 push undo(unbind)。
- [ ] `xpl_service_resolve`:查找 + `xpl_version_compatible`(major 等、provider minor ≥ need);失败置 last_error。
- [ ] `xpl_service_unbind`:幂等;发系统事件 `xpl:service:{bound,unbound}`(依赖 M1c——实现顺序上先做 bus 最小内核亦可,集成测试补)。
- [ ] `xpl_version_compatible` 纯函数。

**测试**

- [ ] bind/resolve/unbind 全链路;resolve 零堆分配断言(分配计数器)。
- [ ] 版本握手:major 不符拒、minor 回退拒、minor 前进收。
- [ ] 重名拒绝;unbind 后 resolve NULL;undo 自动撤销验证(destroy 后表空)。
- [ ] 非法名(超长/非法字符/空)拒绝。

### M1c 事件总线(0.5-1 pd)

**实现**

- [ ] 订阅表:事件名 → 订阅数组{name 比较, fn, userdata};`xpl_bus_intern` 把名字驻留为 int id(自增序号),emit_id 查每事件订阅计数,零订阅 ≤1 分支。
- [ ] `on` 自动 push undo;`off` 幂等;派发序 = 订阅序,同步。
- [ ] 重入保护:回调内 bind/unbind/on/off/emit 同名 → 记日志忽略(标志位实现,不可重入断言)。

**测试**

- [ ] 派发顺序、payload 透传、userdata 透传。
- [ ] 零订阅 emit:通用路径一次探测即返回;emit_id 汇编级/计数器验证 ≤1 分支(反汇编抽查记入文档)。
- [ ] 回调内变更操作被忽略且不崩。
- [ ] destroy 自动解绑全部(on 的 undo 生效)。

### M1d 拓扑装配 + 事务 install(1.5 pd)

**实现**

- [ ] `xpl_component` 表(动态数组);`components_add` 仅收集(install 前可重复 add 去重)。
- [ ] install 三段:**校验**(重名 provides / 依赖缺失列全 / 环检测)→ **排序**(Kahn,平局按 add 序,输出稳定)→ **执行**(逐组件 install;前置记 effect_mark,任一失败 → `effect_unwind(mark)` → `XPL_EFAILED` + last_error 含组件名与原因)。
- [ ] 系统事件 `xpl:component:{installed,failed}`;ctx 状态 NEW→RUNNING。
- [ ] install 回调内可用 API 白名单文档化(bind/on/effect_push/resolve 均可)。

**测试**

- [ ] 拓扑序正确性:菱形依赖、链式、独立分量混合;平局稳定输出(两次 install 序列化结果一致)。
- [ ] 环检测报全部环成员;依赖缺失报全清单。
- [ ] **失败回滚**:5 组件装到第 4 个失败,前 3 个的 bind/on/effect 全部回滚(服务表空、事件表空、effect 水位归零、分配配平)。
- [ ] 二段式失败(组件 install 中途 push 了 3 个 effect 后返回失败)同样全回滚。
- [ ] install 后再 add → 拒绝(RUNNING 状态)。

**M1 里程碑出口**:API 冻结评审(对照 design §5 逐条);规模检查 core ≤1500 行;ASan/TSan 全绿;文档 `include/xplugin/xplugin.h` 顶部注释即 API 手册。

---

## 4 M2 示例 + 一致性套件(1.5 pd)

**任务**

- [ ] `examples/pipeline`:3 个静态组件模拟编译器 Pass(`parser → ir_opt → emit`,requires/provides 声明),装配→执行→拆卸;演示新增 Pass 只加一行 `components_add`。
- [ ] `examples/observer`:订阅系统事件打印装配过程;演示 unbind+bind 替换服务(执行后端切换形态)。
- [ ] `tests/test_conformance.c`:可被组件作者复用的合规套件——给定任意 `xpl_component`,验证:install 幂等拒绝、失败无残留、注册的 undo 完整、无全局状态(双 ctx 并行)、事件回调不改注册表。
- [ ] README 快速上手(编译运行两个示例)。

**验收**:示例 50 行内展示"加组件不改核心";conformance 套件对故意写错的组件能抓出 ≥4 类违规。

---

## 5 M3 loader 模块(2-3 pd,触发门)

> **准入触发**:出现宿主之外的使用者要独立发布组件。未触发不实施。

**任务**

- [ ] `platform_posix.c`(dlopen/dlsym/dlerror)/ `platform_win.c`(LoadLibrary/GetProcAddress),公共头零平台宏。
- [ ] `XPLUGIN_COMPONENT_EXPORT(name)` 宏:`-fvisibility=hidden` 下导出唯一符号 `xplugin_component`。
- [ ] `xpl_loader_load`:dlopen → dlsym 元符号 → 结构校验(name/provides 合法)→ components_add;任一步失败回滚句柄(不影响 ctx 既有状态)。
- [ ] `xpl_loader_scan_dir`:readdir/FindFirstFile 过滤后缀,逐个 load,单库失败默认跳过并 WARN(策略可设严格模式)。
- [ ] 句柄登记到 ctx effect(destroy 时进程退出前统一点名,**不 dlclose**,文档明示)。
- [ ] `examples/dyn_plugin` + nm 自检脚本(仅元符号外部可见)。

**测试**

- [ ] 样例 .so 装卸:服务可 resolve、destroy 无泄漏(ASan)。
- [ ] 坏库(缺符号/非库文件/名字非法)不损 ctx。
- [ ] 双平台冒烟(POSIX 全量,Windows 本地 VS)。

---

## 6 M4 remote 模块(6-8 pd,触发门)

> **准入触发**:出现不可信宿主侧扩展,或插件崩溃已杀死宿主 ≥2 次。

### M4a TLV + 帧协议(1.5 pd)

- [ ] `proto.c`:帧编解码(magic/type/req_id/len 定长头 + TLV 体)、LEB128 varint;`codec.c`:i32/i64/u64/f64/str/blob 基础编解码(宿主复合类型在其上组合)。
- [ ] `tests/test_proto_fuzz.c`:随机字节流/libFuzzer 风格循环(无 sanitizer 环境降级为固定种 子模糊)——畸形帧必须返回 `XPL_EPROTO`,零越界(ASan 验证)。

### M4b 子进程骨架 + 握手(1.5 pd)

- [ ] `child_main.c`:裁剪内核入口(链接 core,不含 loader/remote 宿主侧)——读环境变量拿 fd,装组件,进消息循环。
- [ ] `supervisor.c`:`xpl_remote_spawn(ctx, path, argv)` fork-exec + socketpair;HELLO 交换(core_ver/abi),不符 → 杀子进程返回 `XPL_EVERSION`。
- [ ] 超时控制(默认 5s,可配):握手/调用超时 → `XPL_ESTALE`。

### M4c 代理调用 + 服务导出(2 pd)

- [ ] 宿主侧 `xpl_remote_export(ctx, name)`:向子进程查询组件 provides → 为每个服务生成代理 vtable → `xpl_service_bind` 绑代理。
- [ ] 代理样板:`XPL_REMOTE_PROXY_FN(svc, op, ret, args...)` 宏——序列化参数、CALL 帧、等 RET、反序列化;**参数类型 trait 静态断言拒绝裸指针**(只允许值类型与 blob)。
- [ ] 子进程侧:CALL 分发到本地 resolve 的 vtable(本地直调),结果编码返回。
- [ ] `examples/untrusted_parser`:解析不可信输入的组件放子进程。

### M4d 事件转发 + 崩溃检测/重启(1.5-2 pd)

- [ ] 双向事件:子进程 emit → 转发宿主总线(已注册 codec 才转,否则 WARN 丢弃);宿主 emit → SUBSCRIBE 过的子进程。
- [ ] 崩溃检测:管道 EOF + waitpid;全部代理标记失效;`xpl:plugin:died` → 重启策略(0/N/∞)→ `xpl:plugin:restarted`;期间调用返回 `XPL_ESTALE`。
- [ ] `tests/test_remote_crash.c`:kill -9 子进程,宿主存活、事件到达、重启后服务恢复、未恢复期调用返回 ESTALE。

**M4 出口**:e2e + crash 两套测试 ASan 干净;remote ≤2000 行;协议文档(帧/TLV/消息)随代码评审冻结。

---

## 7 M5 生态增强(逐项触发,不整期排期)

| 项 | 触发 | 预估 | 内容 |
|----|------|------|------|
| 配置系统 | 组件需要用户可配参数 | 1-2 pd | ComponentMeta 附 config schema;内核统一读取注入,组件不读文件 |
| trace 观测 | 性能定位需求 | 1 pd | 代理包装:服务调用耗时/次数统计,经事件上报 |
| semver 范围 | 服务多版本共存需求 | 1 pd | requires 带版本范围;注册表多版本桶 |
| 子进程沙箱 | 不可信度升级 | 1-2 pd | rlimit/seccomp(Linux)、Job Object(Windows) |
| 代理代码生成 | 手写样板超过 3 个服务 | 2 pd | 头文件 → 代理 .c 生成器(离线工具) |
| 多路复用异步 | remote 并发调用需求 | 2-3 pd | req_id 复用 + 请求表 + 宿主侧 pump 线程(宿主显式启动) |

---

## 8 质量门(CI 常驻)

1. 零警告构建(GCC/Clang/MSVC 三套)。
2. ctest 全绿 + ASan 零泄漏零越界 + TSan(M1c 起的并发相关用例)零数据竞争。
3. `check_*.sh` 门(随里程碑补):生命周期直写检查、公共符号最小集检查(nm 白名单)、规模预算检查(wc -l 门)。
4. Conventional Commits;提交前 `verify` 全过。

---

## 9 与消费方的关系

- **xplugin 对消费方零反向依赖**(不 include 任何宿主头);对接代码在宿主侧 adapter。
- xvm 的接入时机与边界见 `xvm-integration.md` §6(触发表,不在本路线图内排期)。
