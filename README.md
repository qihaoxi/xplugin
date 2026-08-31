# xplugin

通用 C 微内核插件框架:用一份核心抽象(上下文 + 可逆副作用 + 服务注册表 + 事件总线 + 拓扑装配),让任何 C 项目获得**事务化装配组件**的能力;进程内直调与进程外隔离两种执行域共用同一契约。

思想来源:HashiCorp go-plugin(契约、握手、进程隔离、宿主协调者)+ Cordis/DSH(薄内核、上下文、effect、服务+事件双原语、拓扑依赖)。

## 功能特点

- **事务化装配**:组件 install 失败自动逆序回滚;`ctx_destroy` 走同一条回放路径
- **可逆副作用内建**:注册类 API 自动登记 undo,组件只为自定义资源手写 effect
- **双执行域统一契约**:进程内直调 / 进程外代理,业务代码零感知
- **版本握手内建**:服务 vtable 首字段 version,bind/resolve 强制校验
- **服务/事件双原语**:带返回值的调用 vs 无返回值广播,职责硬分离
- **零依赖 C11,三档可裁剪**:core(嵌入式可用,≤1500 行)→ +loader(dlopen)→ +remote(进程隔离)
- **分配器注入 + 日志注入**:库不 malloc 直调、不打印、不 fork、不 exit
- **确定性行为**:拓扑序稳定、事件按订阅序、undo 严格逆序
- **故障隔离**:子进程崩溃不杀宿主,失效事件 + 重启策略
- **一致性测试套件**:第三方组件用同一套用例验证合规

## 文档

| 文档 | 内容 |
|------|------|
| [`docs/design.md`](docs/design.md) | 设计规范 v2:公共契约、核心 API 规格、成本模型、loader/remote 模块、设计修正记录(v1→v2)、v1 蓝图保留要点(附录 A) |
| [`docs/roadmap.md`](docs/roadmap.md) | 实施路线图:M0-M5 详细任务清单、验收标准、规模预算、触发门 |
| [`docs/xvm-integration.md`](docs/xvm-integration.md) | 在 [xvm](../xvm) 上的落地:概念映射、冲突裁决(T1-T6)、插件化边界、接入触发表 |

## 状态

- ✅ **M0 工程地基**(2026-08-31):CMake 骨架(C11 + `-Werror`/`/WX`、三档裁剪宏 `XPLUGIN_LOADER`/`XPLUGIN_REMOTE`)、ASan/TSan 双趟、`verify` target、`format` target、`.clang-format`、冒烟测试。
- ✅ **M1a ctx + effect 栈**(2026-08-31):`xpl_ctx`(生命周期 CAS、分配器注入、日志 sink、last_error)+ `xpl_effect` 栈(push/mark/rollback_to,逆序回放,扩容),`test_ctx`/`test_effect` 三趟全绿。
- ✅ **构建溯源 build-info**(2026-08-31):`cmake/git.cmake` 采集 git commit/branch + 时间戳/主机/系统/工具链 → 生成头;`xpl_build_info()` 运行期可查(`cmake/buildinfo.cmake` 钉符号防死代码消除)。
- ✅ **ABI 兼容纪律**(`docs/design.md` §4.7):公共结构体按"谁构造/谁拥有 × 调用方分配点"分三型——类型 A opaque+访问器(`xpl_build_info_t`)、类型 B version+尾部追加(allocator/服务 vtable)、类型 C sizeof-query/shadow 指针/预留 padding(栈分配场景)。教训来源:raw-spofer-pel 的 info/stats 结构体直接暴露字段。
- ⏭ **M1b 服务注册表 + 版本握手**(下一步):`xpl_service_bind/resolve` + `xpl_version_compatible`(见 `docs/design.md` §5.2)。
- 本地验证:`cmake --build cmake-build-debug --target verify`(构建 + ctest 全绿)。
