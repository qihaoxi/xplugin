# xplugin

C 语言微内核插件框架:为 C 实现的虚拟机 / 编译器 / 链接器提供可插拔组件能力。
思想来源:HashiCorp go-plugin(契约、握手、进程隔离)+ Cordis/DSH(薄内核、上下文、可逆副作用、服务+事件)。

## 文档

- [`docs/design.md`](docs/design.md) —— 框架设计规范:双执行域(进程内 / 外进程 IPC)、Ctx + Effect 栈、服务注册表 + 版本握手、事件总线、组件拓扑装配
- [`docs/xvm-integration.md`](docs/xvm-integration.md) —— 在 [xvm](../xvm) 上的落地分析:概念映射、与 xvm 硬约束的冲突裁决、插件化边界、P0-P5 分阶段计划

## 状态

设计阶段(P0 未启动)。当前代码仅为占位骨架。
