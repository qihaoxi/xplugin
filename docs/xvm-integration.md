# xplugin → xvm 落地分析与分阶段计划

> 本文回答:**如何把 `design.md` 的微内核插件框架用到 `~/workspace/xvm` 上**。
> 方法:概念映射(已有/缺失)→ 冲突裁决(与 xvm 硬约束的 6 个张力)→ 插件化边界 → 分阶段计划。
> xvm 的约束以 `xvm/docs/constraints.md` 为准(下称 §N 均指该文档章节)。

---

## 0 结论(TL;DR)

1. **xvm 不需要推倒重来**。xvm 的第一性原理(§0:无全局状态、hooks 解耦、组装层接线、反序销毁)与 xplugin 设计同源,大约 40% 的框架思想 xvm 已经以工程纪律的形式存在。
2. **真正的缺口是 4 件运行期设施**:服务注册表(命名服务)、事件总线、通用 Effect 栈、组件拓扑装配。这正是 xplugin 要实现的全部内核(模式 A)。
3. **模式 B(外进程隔离)是纯增量**,只挂在组装层,xvm 核心(L1-L4)零感知,不与任何现有约束冲突。
4. **插件化的是"装配与扩展",不是"热路径"**。interp 主循环、GC、value 核心保持硬编码;插件服务只落在冷路径(能力发现、校验策略、观测订阅、工具链、JIT 后端切换)。
5. **落地形态**:xplugin 做成与 xgc/xlog 同待遇的独立库(将来 submodule),xvm 仅在组装层与 L5 消费;对接代码放 xvm 侧 adapter,xplugin 本身零 xvm 依赖。

---

## 1 概念映射:xvm 已有什么

| xplugin 概念 | xvm 对应物 | 状态 |
|--------------|-----------|------|
| Ctx 根上下文(无全局状态) | `xvm_runtime_t`(进程级)+ `xvm_context_t`(隔离域) | ✅ 已有,且比框架单 Ctx 更细(两级) |
| Allocator 注入 | xgc `gc_alloc_typed` 唯一分配入口(§3.1) | ✅ 已有(GC 堆);框架元数据内存见张力 T3 |
| vtable 契约(接口优于实现) | hooks vtable:`xvm_hooks_t` / `gc_vm_hooks` / linker hooks(§4) | ✅ 已有,但是**编译期接线** |
| ABI 演进规则 | §2.4 尾部追加 + NULL fallback | ✅ 已有;与 vtable 首字段 version 握手天然兼容 |
| Effect 可逆副作用(逆序撤销) | §9 销毁顺序"自顶向下严格反序" + lifecycle CAS | ⚠️ 精神一致,**无通用 effect API** |
| ComponentMeta + 依赖拓扑 | `scripts/components.defs`(构建期依赖声明 + CI 门) | ⚠️ 构建期有,**运行期无** |
| 宿主=协调者/组装层 | main.c 组装层 ~100 行只接线(README) | ✅ 已有 |
| 对等组件/可替换 | 换 GC / 换 JIT 只改组装层 | ✅ 精神已有 |
| **服务注册表**(运行时命名服务) | 无(hooks 是组装期固定结构体) | ❌ 缺 |
| **事件总线** | observability spec L2 "Event subscription"(仅设计,未实现) | ❌ 缺(有 spec 位置可合流) |
| **版本握手**(加载期) | 无(全静态链接,无加载期) | ❌ 缺(dlopen/模式B 前必须) |
| **模式 A dlopen** | 无(全静态,拷走即用 §11) | ❌ 缺(可选) |
| **模式 B 外进程 IPC** | 无 | ❌ 缺(纯增量) |

---

## 2 缺口分析

框架六件套按 xvm 视角分三类:

**必须补(内核本体)**:服务注册表、事件总线、Effect 栈、ComponentMeta 拓扑装配 —— 全部收口在 xplugin 实现,xvm 不重复造。

**按需补(模式 A 动态)**:dlopen 加载器 —— xvm 当前"拷走即用"的静态哲学下不是刚需;先支持,但 xvm 侧初期只用静态组件。

**纯增量(模式 B)**:外进程 supervisor + IPC 代理 —— 挂组装层,不碰核心。

---

## 3 关键张力与裁决

框架思想与 xvm 硬约束的 6 个冲突点,逐一裁决。**每条裁决即为后续实现的硬性约束。**

### T1 interp 分派循环禁虚调用 ↔ opcode 插件化

- 冲突:§10 规定指令分派循环内禁止函数指针链虚调用;design.md 附录 A.1 说"所有指令集扩展都是普通组件"。
- **裁决:插件化的是"注册入口与生命周期",不是"每条指令一次插件回调"。**
  - 核心指令集编译期固定(computed goto),不做任何插件化。
  - 扩展指令:组件 install 时向 interp 注册**区间分派表**(`[opcode_base, len)` → 处理函数数组);interp 对扩展区做一次数组跳转,与现有单次间接跳转形态等价,非指针链。
  - `vm:instr:*` 事件仅 Debug/观测构建启用(§7 零开销:无订阅者 ≤1 分支)。
  - 前置:需要 rhino-spec ISA 预留 user opcode 区间(开放问题 #5)。

### T2 "VM 是库,不是进程" ↔ 模式 B 子进程

- 冲突:§0.1 要求任何路径不得杀宿主;库不应自作主张管理进程。
- **裁决:进程管理(supervisor)是组装层可选组件。**
  - xvm 核心(L1-L4)零 xplugin 依赖,不 include、不链接。
  - 库内不隐式 fork/spawn;子进程由宿主在组装层显式装配(与 PEL backend 同待遇:backends/ 下的一个可选件)。
  - 这样模式 B 完全不触碰 §0.1——它保护宿主,而不是被库强加。

### T3 gc_alloc_typed 唯一入口 ↔ 框架 Allocator 注入

- 冲突:design.md 要求组件统一走 Ctx Allocator;§3.1 规定堆对象一律 `gc_alloc_typed`。
- **裁决:两级分配,互不越界。**
  - 框架元数据(注册表节点、事件订阅、组件记录):走 xplugin 注入 allocator(默认 libc,可换 arena),destroy 时统一回收。
  - GC 堆对象:**永远走 `gc_alloc_typed`**,插件框架不经手;插件之间传递的是 `xvm_value_t`,不是裸堆指针。
  - dlopen 场景(design 风险 #6)由这条裁决自动覆盖:xvm 侧插件规范写明"堆对象只经 gc 收口点"。

### T4 组件独立性(拷走即用)↔ 新增 xplugin 依赖

- 冲突:§11.4 规定外部前置只能是 submodule / 系统库 / vendored;随便引入依赖破坏独立性。
- **裁决:xplugin submodule 化,与 xgc/xlog 同待遇;且消费点收窄到组装层 + L5。**
  - L1-L4 组件零 xplugin include(可扩 `check_structure.sh` 加门)。
  - xplugin 自身保持通用框架定位,**零 xvm 依赖**;对接代码放 xvm 侧 adapter(`src/backends/` 或组装层)。

### T5 窄腰冻结 ↔ IPC 线格式

- 冲突:`xvm_value_t` 16B 布局冻结(§2.1),OBJ 指针不能跨进程地址空间。
- **裁决:定义 value wire format,作为新 ABI 面纳入 §2.4 演进规则。**
  - 标量(INT/FLOAT/BOOL/SPECIAL)直传;OBJ 不传裸指针——跨域对象引用仅允许 OID/句柄映射或"不可跨域"(服务契约标注)。
  - 线格式只追加不改(与窄腰同款纪律)。

### T6 hooks(编译期接线)↔ 服务注册表(运行期发现)

- 冲突:两者都是"接口解耦",职责重叠风险。
- **裁决:按调用频率分流,不混用。**
  - 热路径(每指令/每分配/每 hook 回调):hooks 结构体直调,保留现状(§4、§10 约束不变)。
  - 冷路径(能力发现、工具链装配、校验策略、观测订阅、后端切换):命名服务注册表 + 事件总线。
  - 判据:**每秒 >1e6 次的接口禁止进注册表**(进 hooks);其余默认走注册表。

---

## 4 插件化边界

### 4.1 插件化(模式 A 优先)

| 域 | 方式 | 服务/事件 |
|----|------|----------|
| verifier 校验策略 | 模式 A | 服务 `xvm.verifier.rule.*`;事件 `verify:before/after` |
| observability 订阅者(profiler/debugger/trace dump) | 模式 A(采样器可模式 B) | 事件总线 —— **与 observability spec L2 "Event subscription" 合流实现**(开放问题 #3) |
| 内建函数扩展 | 模式 A | 经注册表装配成分发表,再挂 `call_builtin` hook(热路径不走注册表,见 T6) |
| JIT 后端切换 | 模式 A(可信)/ 模式 B(实验) | 服务 `vm.executor`(interp ↔ JIT 运行时替换,design v2 §5.2) |
| rse 工具链(disasm/汇编/打包) | 模式 A/B | 独立工具天然是模式 B 形态 |
| FFI / 绑定生成器、第三方解析器 | 模式 B | 不可信代码,强制隔离 |

### 4.2 不插件化(红线)

- interp 主循环与核心指令分派(T1)
- GC、value 窄腰、帧栈、异常 unwind(§0/§2/§10 硬约束)
- L1-L4 任何组件的内部数据通路

> 红线的意义:xplugin 失效/移除时,xvm 必须仍是一个完整可用的静态链接 VM。

---

## 5 xplugin 自身工程纪律(对齐 xvm)

xplugin 作为可被 xvm 消费的库,执行与 xvm 同款纪律(xvm `CLAUDE.md`/`AGENTS.md`):

1. C11,`-Wall -Wextra -Wpedantic -Werror`,零警告;kernel 缩进 K&R。
2. 回归测试 ASan + TSan 打底。
3. **收口点原则**:effect 登记/回放、服务注册/查询、事件 emit/订阅、组件 install/undo —— 各自唯一入口函数。
4. **lifecycle CAS 状态机**:ctx(NEW→RUNNING→DESTROYING→DESTROYED)、插件实例(LOADED→INSTALLED→FAILED),流转唯一经 CAS 收口点,禁直写。
5. **一函数一权威实现**:注册表、事件总线、拓扑排序各一份,不出现第二实现。
6. **可独立引用**:零 xvm/xgc 依赖,单独 cmake 构建 + 测试;对接层放 xvm 侧。
7. API 前缀 `xpl_`;头文件 `include/xplugin/*.h`;内部头不出组件。

---

## 6 接入时机:触发表(不是固定计划)

> 框架自身的实施在 `roadmap.md`(M0-M2 核心无条件,M3/M4 触发门)。
> **xvm 侧不排期**——以下每一行都是"信号出现才做对应接入",防止为集成而集成(过度设计结论的落地)。

| 触发信号(可观察) | 接入动作 | 依赖 xplugin 模块 |
|------------------|---------|------------------|
| interp 端到端跑通,profiler/debugger 需要挂点 | observability L2 "Event subscription" 落在 `xpl_bus` 上(spec 增补引用) | core · bus(M1c) |
| JIT 原型出现,需要运行时切换后端 | 组装层 unbind+bind `vm.executor`(§8 预览形态) | core · registry(M1b) |
| 组装层超 ~300 行,且拆卸顺序出过错 | 组装层改用 `xpl_ctx` 事务化装配(install 失败自动回滚) | core · topo(M1d) |
| 出现第一个"宿主之外的使用者"想独立发布组件 | xplugin submodule 化 + loader | loader(M3) |
| 插件崩溃已杀死宿主进程 ≥2 次 | 不可信扩展迁入隔离域(按 T5 wire format 定值编码) | remote(M4) |

**现在就白拿的三个便宜货**(文档级,零基础设施,不等任何触发):

1. "服务 vs 事件"区分规则 → 补进 observability spec L2 设计(带返回值走调用,通知走事件)。
2. "跨边界禁裸指针 / 可序列化 blob"规则 → 补进 hooks 契约说明(为将来 backend/远程立规矩)。
3. 反序销毁纪律 → xvm §9 已有,维持即可(effect 栈思想的纪律级形态)。

---

## 7 开放问题(需拍板)

| # | 问题 | 建议 |
|---|------|------|
| 1 | 模式 B 序列化选型:protobuf-c / flatbuffers / 自研 TLV | 自研小 TLV——xvm 值线格式很小,~200 行可控且零依赖;已按此定入 design v2 §8.2,M4a 实现前可推翻 |
| 2 | xplugin 是否 submodule 进 xvm | 是,与 xgc/xlog 同待遇(§11.4) |
| 3 | 事件总线与 observability spec L2 合流 | 合流:`xpl_bus` 作为 L2 Event subscription 的唯一实现,spec 增补引用 |
| 4 | xvm 侧初期是否启用 dlopen(模式 A 动态) | 否:先用静态组件,保持"拷走即用";dlopen 留给工具链场景 |
| 5 | rhino-spec ISA 是否预留扩展 opcode 区间 | 需确认;无则先预留一段 user 区,影响 interp spec(T1 前置) |
| 6 | xpl_ctx 与 xvm runtime/context 层级 | xpl_ctx 在组装层包住 `xvm_runtime_t`(1:1 或 1:N),不下探 L1-L4 |

---

## 8 组装层形态预览(触发后的目标样貌)

```c
/* main.c —— 组装层:xpl_ctx 装配,业务只面对服务与事件 */
int main(void) {
    xpl_ctx* k = xpl_ctx_new(NULL);            /* 默认 allocator */

    xpl_components_add(k, &xvm_comp_runtime);  /* 静态组件:runtime(包 gc) */
    xpl_components_add(k, &xvm_comp_interp);   /* 提供 vm.executor */
    xpl_components_add(k, &xvm_comp_verifier); /* 提供 xvm.verifier.rule.core */
    xpl_components_add(k, &my_trace_plugin);   /* 订阅 verify:* / vm:instr:after */

    if (xpl_components_install(k) != XPL_OK)   /* 拓扑序装配,失败自动回滚 */
        return 1;

    xvm_executor* ex = xpl_service_get(k, "vm.executor");
    /* ... 加载 .rse,ex->run(...) ... */

    xpl_ctx_destroy(k);                        /* 逆序:trace→verifier→interp→runtime */
}
```

换 JIT、换 verifier 规则集、加观测插件 = 改 `components_add` 一行;组件代码与 xvm 核心不动。
