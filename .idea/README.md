# IDEA/CLion 配置说明

本项目为 Keil C251 (STC32G) 项目，使用 CMake 仅用于 IDE 代码解析，实际编译通过 Keil μVision 完成。

## 配置文件说明

- `CMakeLists.txt` - CMake 配置，用于 IDE 解析代码结构
- `.idea/encodings.xml` - 文件编码配置（C/H 文件使用 GB2312）
- `.idea/misc.xml` - 项目根目录和源码路径配置
- `.idea/inspectionProfiles/` - 代码检查配置（禁用部分不适用的检查）

## 使用方法

1. 用 CLion 或 IntelliJ IDEA Ultimate 打开项目
2. IDE 会自动加载 CMake 配置
3. 代码编辑、跳转、补全功能可用
4. 实际编译请使用 VSCode 的 Build 任务或直接用 Keil

## 注意事项

- C251 特有关键字（interrupt、xdata 等）可能仍有部分误报
- 建议关闭 Clang-Tidy 检查
- 编码问题已配置，但如遇乱码请手动设置文件编码为 GB2312

