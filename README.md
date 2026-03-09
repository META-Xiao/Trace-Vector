# Trace Vector - STC32G144K 智能车项目

基于 STC32G144K (Keil C251) 的智能车控制系统。

## 项目结构

```
SkFSTC32G144K256/
├── libraries/              # 逐飞科技开源库
│   ├── zf_common/         # 通用功能（时钟、中断、调试等）
│   ├── zf_driver/         # 底层驱动（GPIO、UART、PWM、ADC 等）
│   ├── zf_device/         # 外设驱动（屏幕、传感器、无线模块等）
│   └── zf_components/     # 组件库（USB、助手等）
├── project/
│   ├── mdk/               # Keil 项目文件
│   ├── user/              # 用户核心代码（main.c、isr.c）
│   └── code/              # 用户业务逻辑（模块化功能代码）
├── .vscode/               # VSCode 配置
└── .idea/                 # IntelliJ IDEA/CLion 配置
```

## 开发环境配置

### 必需工具
- Keil μVision 5 (C251 工具链)
- VSCode 或 CLion (推荐)

### VSCode 配置
详见 [VSCode 配置说明](.vscode/README.md)

1. 复制 `.vscode/settings.example.json` 为 `.vscode/settings.json`
2. 修改 Keil 安装路径
3. 按 `Ctrl+Shift+B` 编译

### CLion 配置
项目已包含 CMakeLists.txt，直接打开即可使用代码补全和跳转功能。实际编译仍需使用 Keil。

## Git 协作

详见 [Git 使用指南](docs/GIT_GUIDE.md)

## 编译与烧录

### 编译
- **VSCode**: `Ctrl+Shift+B` 或运行任务 "Keil Build"
- **Keil**: 直接打开 `project/mdk/TraceVector.uvproj` 编译

### 输出文件
编译后的文件位于 `project/mdk/Out_File/`：
- `TraceVector.hex` - 烧录文件
- `TraceVector.bin` - 二进制文件

## 代码规范

- 用户代码放在 `project/code/` 目录
- 不要修改 `libraries/` 下的库文件
- 中断服务函数统一写在 `project/user/isr.c`

## 许可证

本项目基于 GPL-3.0 许可证开源。

原始库版权归 SEEKFREE 逐飞科技所有。  
由 Trace Vector TEAM 修改和维护。

详见 [LICENSE](LICENSE)

## 联系方式

- 团队: Trace Vector
- 仓库: https://github.com/META-Xiao/Trace-Vector

