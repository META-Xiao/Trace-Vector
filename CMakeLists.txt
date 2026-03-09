cmake_minimum_required(VERSION 3.20)
project(SkFSTC32G144K256 C)

set(CMAKE_C_STANDARD 99)

# Keil C251 工具链路径
set(KEIL_C251_PATH "D:/keilv5/C251")
set(KEIL_C251_INC "${KEIL_C251_PATH}/INC")

# 添加 C251 特有的宏定义
add_definitions(
    -D__C251__
    -D__KEIL__
    -D__VSCODE_C251__
)

# 包含目录
include_directories(
    ${CMAKE_SOURCE_DIR}/libraries/zf_common
    ${CMAKE_SOURCE_DIR}/libraries/zf_driver
    ${CMAKE_SOURCE_DIR}/libraries/zf_device
    ${CMAKE_SOURCE_DIR}/libraries/zf_components/seekfree_assistant
    ${CMAKE_SOURCE_DIR}/libraries/zf_components/stc32g_usb_lib
    ${CMAKE_SOURCE_DIR}/project/user
    ${CMAKE_SOURCE_DIR}/project/code
    ${KEIL_C251_INC}
)

# 源文件
file(GLOB_RECURSE SOURCES
    "libraries/zf_common/*.c"
    "libraries/zf_driver/*.c"
    "libraries/zf_device/*.c"
    "libraries/zf_components/seekfree_assistant/*.c"
    "libraries/zf_components/stc32g_usb_lib/*.c"
    "project/user/*.c"
    "project/code/*.c"
)

# 创建可执行目标（仅用于 IDE 解析，不实际编译）
add_executable(${PROJECT_NAME} ${SOURCES})

# 设置编译选项（模拟 C251 环境）
target_compile_options(${PROJECT_NAME} PRIVATE
    -Wno-unknown-pragmas
    -Wno-builtin-macro-redefined
)

