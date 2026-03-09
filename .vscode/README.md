# vsc编译说明

本文件是专门为**STC32G144K256**配置的VSC配置文件，满足的功能有：

- 保留原来**intellisense**的语法解析，能够`F12`自动跳转到定义，语法高亮，禁用**error**和**dimInactiveRegions**（淡化未被使用的代码）
- 能够在安装不插件其他插件，只使用vsc的c/cpp插件和keil下成功编译
- 美丽的`CLI`输出编译信息，高亮错误

# 如何配置？

在`~/.vscode/`下创建`settings.json`，并复制：

```json
{
    // Keil 编译器路径配置
    "UV4_PATH": "你的UV4地址",    //例如 "D:/keilv5/UV4/UV4.exe" 一定要打双引号
    "KEIL_C251_INC": "你的C251/INC地址",    //例如 "D:/keilv5/C251/INC" 
    
    // 项目配置
    "PROJECT_FILE": "${workspaceFolder}/project/mdk/seekfree.uvproj",
    "TARGET_NAME": "stc32g144k246",
    
    // 文件编码配置
    "files.encoding": "utf8",
    "files.autoGuessEncoding": true,
    "[c]": {
        "files.encoding": "gb2312"
    },
    "[cpp]": {
        "files.encoding": "gb2312"
    },
    "[h]": {
        "files.encoding": "gb2312"
    },
    "[html]": {
        "files.encoding": "windows1252"
    },
    "files.associations": {
        "*.c": "c",
        "*.h": "c",
        "*.cpp": "cpp",
        "**/out_file/**/*.htm": "html"
    },
    
    // C/C++ IntelliSense 配置 - 针对 Keil C251 优化
    "C_Cpp.errorSquiggles": "disabled",
    "C_Cpp.dimInactiveRegions": false
}

```

# 如何食用？

直接按快捷键 `Ctrl+Shift+B` 编译，结果文件在 `~/project/mdk/Out_File`
