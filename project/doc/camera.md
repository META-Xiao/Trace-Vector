# camera & track 模块文档

> 适用平台：STC32G144K · 摄像头型号：MT9V03X（总钻风）  
> 文件位置：`project/code/camera.h` / `project/code/camera.c` / `project/code/track.h` / `project/code/track.c`  
> 依赖库：`zf_device_mt9v03x`、`zf_device_mt9v03x_dma`  
> 作者：Trace Vector · 创建日期：2026-03-10  
> 最后更新：2026-03-10（添加 track 循迹模块；精简命名）

---

## 1. 模块概述

`camera` 模块封装了 MT9V03X 摄像头的全部初始化与运行时控制逻辑，核心功能包括：

| 功能 | 说明 |
|------|------|
| 摄像头初始化 | 调用底层 SCCB/DMA 初始化，完成硬件配置 |
| 手动曝光控制 | 随时读取或设置曝光时间 |
| **中心 ROI 间隔采样自动曝光（AE）** | 以图像中心 ROI 为测光焦点，间隔采样 + 帧降频，高效稳定调节画面亮度 |
| 中心亮度查询 | 返回当前帧中心区域平均灰度，可用于赛道判断 |
| 帧就绪轮询 | 统一接口检测并清除新帧标志 |

---

## 2. 快速上手

```c
#include "zf_common_headfile.h"
#include "camera.h"

void main(void)
{
    clock_init(SYSTEM_CLOCK_96M);
    debug_init();

    // 1. 初始化摄像头
    if (camera_init() != 0)
    {
        // 串口会输出：[Camera] init failed, ret=XX
        while(1);  // 初始化失败，检查接线
    }
    // 串口会输出：[Camera] init ok, exposure=512

    while (1)
    {
        // 2. 等待新帧就绪
        if (camera_frame_ready())
        {
            // 3. 自动曝光（内部帧降频，每4帧执行一次，其余帧直接返回）
            camera_auto_exposure();

            // 4. 边缘检测、位置计算等图像处理
            // ...（获得充裕 CPU 时间）
        }
    }
}
```

---

## 3. 宏配置说明

所有可调参数均位于 `camera.h`，无需修改 `.c` 文件。

### 3.1 自动曝光 ROI（测光区域）

```
图像坐标系（左上角为原点）

+------------------------------------------+  <- 第 0 行
|                                          |
|        +--------------------+            |  <- CAMERA_AE_ROI_Y
|        |  x x . x x . x x  |            |
|        |  . . . . . . . .  |            |  <- 中心测光焦点
|        |  x x . x x . x x  |  (x=采样点)|
|        +--------------------+            |  <- CAMERA_AE_ROI_Y + H
|                                          |
+------------------------------------------+  <- 第 MT9V03X_H-1 行
```

| 宏名 | 默认值 | 说明 |
|------|--------|------|
| `CAMERA_AE_ROI_W` | `MT9V03X_W / 3`（约62列） | ROI 宽度（列数） |
| `CAMERA_AE_ROI_H` | `MT9V03X_H / 3`（40行） | ROI 高度（行数） |
| `CAMERA_AE_ROI_X` | `(MT9V03X_W - ROI_W) / 2` | ROI 左上角列坐标（自动居中） |
| `CAMERA_AE_ROI_Y` | `(MT9V03X_H - ROI_H) / 2` | ROI 左上角行坐标（自动居中） |

> **提示**：若需要更灵敏的测光，可缩小 ROI；若赛道背景干扰大，可适当扩大 ROI。

### 3.2 自动曝光调节参数

| 宏名 | 默认值 | 说明 |
|------|--------|------|
| `CAMERA_AE_TARGET` | `120` | 目标灰度值（0-255），建议 100-140 |
| `CAMERA_AE_STEP` | `16` | 单次最大调整步长，值越大收敛越快但越不稳定 |
| `CAMERA_AE_TOLERANCE` | `8` | 死区容差，误差在此范围内不调整（防抖） |
| `CAMERA_EXP_MIN` | `1` | 曝光时间下限 |
| `CAMERA_EXP_MAX` | `600` | 曝光时间上限（摄像头内部会进一步限幅） |

### 3.3 采样效率参数（优化新增）

| 宏名 | 默认值 | 说明 |
|------|--------|------|
| `CAMERA_AE_ROW_STEP` | `2` | 行采样步长，`1`=全采样，`2`=隔1行，`4`=隔3行 |
| `CAMERA_AE_COL_STEP` | `2` | 列采样步长，同上 |
| `CAMERA_AE_FRAME_INTERVAL` | `4` | AE 执行帧间隔，`1`=每帧，`4`=每4帧执行一次 |

**采样点数对比：**

| 配置 | 采样点数 | 相对耗时 | 适用场景 |
|------|----------|----------|----------|
| ROW/COL_STEP=1（全采样） | ~2480 | 100% | 调试验证 |
| ROW/COL_STEP=2（默认） | ~620 | ~25% | 正常比赛 |
| ROW/COL_STEP=4 | ~155 | ~6% | 极限性能 |

> **奇偶行错位**：奇数行列起点偏移 1，避免固定模式采样偏差，均值精度优于简单跳行。

---

## 4. API 详细说明

### 4.1 `camera_init`

```c
uint8 camera_init(void);
```

**功能**：完成 MT9V03X 硬件初始化，包括 SCCB 参数下发、GPIO 配置、DMA 及外部中断初始化，同时复位帧计数器。

**调用时机**：`clock_init()` 之后，主循环之前调用一次。

**返回值**：

| 返回值 | 含义 |
|--------|------|
| `0` | 初始化成功 |
| 非 `0` | 失败（检查摄像头连接、IIC引脚 PB6/PB7） |

**示例**：
```c
if (camera_init() != 0)
{
    // 模块内部已打印：[Camera] init failed, ret=XX
    while(1);
}
// 模块内部已打印：[Camera] init ok, exposure=512
```

---

### 4.2 `camera_get_exposure`

```c
uint16 camera_get_exposure(void);
```

**功能**：返回模块内部记录的当前曝光时间值（上电默认为 `MT9V03X_EXP_TIME_DEF = 512`）。

**返回值**：`uint16`，范围 `[CAMERA_EXP_MIN, CAMERA_EXP_MAX]`。

**示例**：
```c
uint16 exp = camera_get_exposure();
// 可用于调试显示当前曝光值
```

---

### 4.3 `camera_set_exposure`

```c
uint8 camera_set_exposure(uint16 exp);
```

**功能**：手动设置曝光时间，内部自动限幅后通过 SCCB 下发到摄像头。

| 参数 | 类型 | 说明 |
|------|------|------|
| `exp` | `uint16` | 期望曝光时间，范围 `[1, 600]` |

| 返回值 | 含义 |
|--------|------|
| `0` | 设置成功 |
| `1` | SCCB 通信失败 |

**示例**：
```c
camera_set_exposure(300);   // 固定曝光为 300
// 成功时串口输出：[Camera] set exposure=300
// 失败时串口输出：[Camera] set exposure failed, ret=XX
```

---

### 4.4 `camera_auto_exposure` ⭐

```c
void camera_auto_exposure(void);
```

**功能**：以图像中心 ROI 为测光焦点，执行带帧降频的高效自动曝光调节。

**算法流程**：

```
帧计数器 +1
         |
    未到 FRAME_INTERVAL
         |-----> 直接返回（本帧 CPU 全留给图像处理）
         |
    到达间隔，计数器清零
         |
    ROI 间隔采样（行步长 ROW_STEP，列步长 COL_STEP，奇偶行错位）
         |
    计算采样均值 avg
         |
    error = avg - TARGET
         |
    printf("[Camera] AE avg=XX, error=XX, exp=XX")  // 每次 AE 执行均打印
         |
    |error| <= TOLERANCE -----> printf("[Camera] AE within tolerance, no adjustment")
         |                       不调整，返回（防抖死区）
         |
    delta = -error / 4，限幅至 +-CAMERA_AE_STEP
         |
    new_exp = s_exposure + delta，限幅至 [EXP_MIN, EXP_MAX]
         |
    printf("[Camera] AE adjust delta=XX, new_exp=XX")  // 实际调整时打印
         |
    camera_set_exposure(new_exp)  // SCCB 写入摄像头，同时打印 set exposure=XX
```

**调用时机**：每帧图像采集完成后调用一次，内部自动处理帧降频。

```c
if (camera_frame_ready())
{
    camera_auto_exposure();        // 内部每4帧才真正执行一次
    // 边缘检测、位置计算...       // 每帧都执行，获得充裕 CPU 时间
}
```

**调参建议**：

| 场景 | 建议调整 |
|------|----------|
| 光线变化剧烈（室外） | 增大 `CAMERA_AE_STEP`（24-32），缩小 `CAMERA_AE_TOLERANCE`（4-6），减小 `CAMERA_AE_FRAME_INTERVAL`（1-2） |
| 光线均匀（室内赛道） | 减小 `CAMERA_AE_STEP`（8-12），增大 `CAMERA_AE_TOLERANCE`（10-15），`FRAME_INTERVAL` 可设为 4-8 |
| 赛道中心有强反光 | 缩小 `CAMERA_AE_ROI_W/H`，聚焦更小中心区域 |
| 极限性能需求 | `ROW_STEP=4`、`COL_STEP=4`、`FRAME_INTERVAL=8`，采样耗时降至约 6% |

---

### 4.5 `camera_get_center_brightness`

```c
uint8 camera_get_center_brightness(void);
```

**功能**：返回当前帧中心 ROI 区域的间隔采样平均灰度值（0-255），可供图像处理模块使用。

**返回值**：`uint8`，`0` 为全黑，`255` 为全白。

**示例**：
```c
uint8 bright = camera_get_center_brightness();
if (bright < 50)
{
    // 中心区域过暗，可能是隧道/遮挡
}
```

---

### 4.6 `camera_frame_ready`

```c
uint8 camera_frame_ready(void);
```

**功能**：检测 DMA 是否完成当前帧采集，若就绪则自动清零 `mt9v03x_finish_flag` 并返回 `1`。

| 返回值 | 含义 |
|--------|------|
| `1` | 新帧就绪，图像数据已写入 `mt9v03x_image` |
| `0` | 尚未采集完毕，本次轮询跳过 |

**示例**：
```c
while (1)
{
    if (camera_frame_ready())
    {
        camera_auto_exposure();   // AE（帧降频，绝大多数帧直接跳过）
        // 边缘检测、位置计算...
    }
}
```

---

## 5. 日志输出说明

所有 `printf` 日志均以 `[Camera]` 为前缀，通过串口（`debug_init()` 初始化后）输出，格式如下：

| 触发时机 | 日志内容 |
|----------|----------|
| `camera_init()` 成功 | `[Camera] init ok, exposure=<值>` |
| `camera_init()` 失败 | `[Camera] init failed, ret=<错误码>` |
| `camera_set_exposure()` 成功 | `[Camera] set exposure=<值>` |
| `camera_set_exposure()` 失败 | `[Camera] set exposure failed, ret=<错误码>` |
| AE 每次执行（到达帧间隔后） | `[Camera] AE avg=<均值>, error=<误差>, exp=<当前曝光>` |
| AE 误差在死区内，不调整 | `[Camera] AE within tolerance, no adjustment` |
| AE 实际触发曝光调整 | `[Camera] AE adjust delta=<步长>, new_exp=<新曝光>` |

> **注意**：`camera_auto_exposure()` 内部含帧降频，只有到达 `CAMERA_AE_FRAME_INTERVAL` 间隔时才会输出上述 AE 日志，其余帧静默返回，不产生日志。

---

## 6. 底层库函数速查（zf_device_mt9v03x）

以下为本模块直接调用的底层库函数，一般不需要用户直接调用。

| 函数 | 原型 | 说明 |
|------|------|------|
| `mt9v03x_init` | `uint8 mt9v03x_init(void)` | 摄像头硬件完整初始化（含 DMA、外部中断） |
| `mt9v03x_set_exposure_time` | `uint8 mt9v03x_set_exposure_time(uint16 light)` | 单独设置曝光时间，值越大图像越亮 |
| `mt9v03x_set_reg` | `uint8 mt9v03x_set_reg(uint8 addr, uint16 dat)` | 直接写摄像头内部寄存器（高级用法） |
| `mt9v03x_vsync_handler` | `void mt9v03x_vsync_handler(void)` | 场同步中断回调，已在 `isr.c` 的 `INT1_IRQHandler` 中调用 |
| `mt9v03x_dma_handler` | `void mt9v03x_dma_handler(void)` | DMA 完成中断回调，已在 `isr.c` 的 `DMA_LCM_IRQHandler` 中调用 |

### 关键全局变量

| 变量 | 类型 | 说明 |
|------|------|------|
| `mt9v03x_image[MT9V03X_H][MT9V03X_W]` | `uint8 [120][188]` | 图像数据缓冲区，每元素为一个像素灰度值（0-255） |
| `mt9v03x_finish_flag` | `vuint8` | DMA 采集完成标志，`1` 表示新帧就绪 |

---

## 7. 摄像头默认参数（来自 zf_device_mt9v03x.h）

| 参数 | 宏定义 | 默认值 | 说明 |
|------|--------|--------|------|
| 图像宽度 | `MT9V03X_W` | `188` | 列数，禁止修改 |
| 图像高度 | `MT9V03X_H` | `120` | 行数，禁止修改 |
| 曝光时间 | `MT9V03X_EXP_TIME_DEF` | `512` | 初始曝光时间 |
| 帧率 | `MT9V03X_FPS_DEF` | `100` | 目标帧率（FPS） |
| 增益 | `MT9V03X_GAIN_DEF` | `32` | 范围 16-64 |
| 自动曝光 | `MT9V03X_AUTO_EXP_DEF` | `0` | 0=关闭硬件AE，本模块实现软件AE |
| IIC SCL | `MT9V03X_COF_IIC_SCL` | `IO_PB6` | SCCB 时钟引脚 |
| IIC SDA | `MT9V03X_COF_IIC_SDA` | `IO_PB7` | SCCB 数据引脚 |

---

## 8. 引脚占用总览

| 引脚 | 功能 | 说明 |
|------|------|------|
| PB6 | SCCB SCL | 摄像头 IIC 时钟 |
| PB7 | SCCB SDA | 摄像头 IIC 数据 |
| P20~P27 | D0~D7 | 图像数据总线（禁止修改） |
| P33 | FIFO_VSY | 场同步信号，接外部中断 INT1 |
| P37 | FIFO_RCK | FIFO 读时钟 |
| P41 | FIFO_WE | FIFO 写使能 |
| P43 | FIFO_RRST | FIFO 读复位 |
| P45 | DMA 占用 | 禁止复用 |
| P36 | DMA 占用 | 禁止复用 |
| P72 | FIFO_OE | FIFO 输出使能 |
| P73 | FIFO_WRST | FIFO 写复位 |

---

## 9. 中断配置说明

本模块依赖以下两个中断，已在 `project/user/isr.c` 中配置，**无需用户修改**：

```c
// 场同步中断（每帧开始时触发，启动 DMA 采集）
void INT1_IRQHandler() interrupt INT1_VECTOR
{
    mt9v03x_vsync_handler();
}

// DMA 完成中断（一帧图像采集完毕时触发，置位 mt9v03x_finish_flag）
void DMA_LCM_IRQHandler() interrupt DMA_LCM_VECTOR
{
    mt9v03x_dma_handler();
}
```

---

## 10. 常见问题

**Q：`camera_init()` 返回非 0，初始化失败？**  
A：检查摄像头供电（3.3V）、IIC 引脚 PB6/PB7 连接是否正常，确认 `MT9V03X_INIT_TIMEOUT`（默认 400ms）内摄像头已上电完成。

**Q：图像全黑或全白？**  
A：尝试手动调用 `camera_set_exposure(256)` 固定曝光后观察，若图像正常则是 AE 参数问题；若仍异常则检查 FIFO 引脚和数据总线连接。

**Q：自动曝光调节缓慢？**  
A：增大 `CAMERA_AE_STEP`（如改为 32），减小 `CAMERA_AE_TOLERANCE`（如改为 4），减小 `CAMERA_AE_FRAME_INTERVAL`（如改为 1-2）。

**Q：图像亮度在目标值附近震荡？**  
A：增大 `CAMERA_AE_TOLERANCE`（如改为 15-20）；或减小 `CAMERA_AE_STEP`；或增大 `CAMERA_AE_FRAME_INTERVAL` 让调节更稀疏。

**Q：间隔采样会影响 AE 精度吗？**  
A：对于赛道图像（灰度分布连续均匀）影响极小。奇偶行错位设计进一步消除了固定模式偏差。如需最高精度，将 `ROW_STEP` 和 `COL_STEP` 均设为 `1` 即可回退到全采样。

**Q：能否同时使用硬件 AE（`MT9V03X_AUTO_EXP_DEF`）和本模块的软件 AE？**  
A：不建议同时开启，两者会相互干扰。推荐保持 `MT9V03X_AUTO_EXP_DEF = 0`，使用本模块的软件 AE。

---

## 11. track 模块概述

`track` 模块基于对比度（差比和）算法实现摄像头循迹搜线，采用「跳行跳列」优化策略，综合耗时约 **2～7ms**，满足 100FPS 处理需求。所有可调参数均位于 `track.h`，无需修改 `.c` 文件。

| 功能 | 说明 |
|------|------|
| 参考白点采样 | 近端 ROI 间隔采样求均值，得到判白基准 |
| **最长白列搜索** | 跳行跳列从近到远找最远白列，确定搜线起点 |
| 左右边界搜索 | 从起点向左右跳列扩展，差比和对比度判断边界 |
| PID 数据接口 | 输出中线偏差 `err`、有效行数 `n_valid`、赛道宽度 `width` |

---

## 12. 快速上手

```c
#include "zf_common_headfile.h"
#include "camera.h"
#include "track.h"

void main(void)
{
    clock_init(SYSTEM_CLOCK_96M);
    debug_init();

    // 1. 初始化摄像头与循迹模块
    if (camera_init() != 0)
    {
        // 串口会输出：[Camera] init failed, ret=XX
        while(1);
    }
    // 串口会输出：[Camera] init ok, exposure=512
    track_init();
    // 串口会输出：[Track] init ok

    while (1)
    {
        // 2. 等待新帧就绪
        if (camera_frame_ready())
        {
            // 3. 自动曝光（内部帧降频）
            camera_auto_exposure();

            // 4. 三步搜线
            track_process();

            // 5. 读取 PID 误差
            int16 err = track_error();   // 正=偏右，负=偏左
        }
    }
}
```

---

## 13. 宏配置说明

所有可调参数均位于 `track.h`，无需修改 `.c` 文件。

### 13.1 搜索范围与步长

```
图像坐标系（左上角为原点，行号向下增大）

+------------------------------------------+  <- 第 0 行（远端）
|                                          |
|   TRACK_ROW_FAR  ~~~~~~~~~~~~~~~~~~~     |  <- 远端截止行
|        . . . . . . . . . . . . . . .    |     （跳格遍历范围）
|        . . . . . . . . . . . . . . .    |
|   TRACK_ROW_NEAR ~~~~~~~~~~~~~~~~~~~     |  <- 近端起始行
|                                          |
+------------------------------------------+  <- 第 MT9V03X_H-1 行（近端）
```

| 宏名 | 默认值 | 说明 |
|------|--------|------|
| `TRACK_SCAN_ROW_STEP` | `3` | 最长白列搜索行步长，每隔3行采一次，运算量约降低9倍 |
| `TRACK_SCAN_COL_STEP` | `3` | 最长白列搜索列步长，同上 |
| `TRACK_EDGE_STEP` | `2` | 左右边界搜索列步长，`1`=逐列，`2`=隔1列 |
| `TRACK_ROW_NEAR` | `MT9V03X_H * 2 / 3` | 搜索近端起始行（较大行号=图像下方） |
| `TRACK_ROW_FAR` | `MT9V03X_H / 6` | 搜索远端截止行（较小行号=图像上方） |

**步长与运算量对比：**

| ROW/COL_STEP | 运算量 | 适用场景 |
|---|---|---|
| 1（全遍历） | 100% | 调试验证 |
| 3（默认） | ~11% | 正常比赛 |
| 4 | ~6% | 极限性能 |

### 13.2 对比度与参考白点

| 宏名 | 默认值 | 说明 |
|------|--------|------|
| `TRACK_CONTRAST_THRESH` | `80` | 差比和阈值，超过此值判定为边界（0~255，值越大越严格） |
| `TRACK_REF_ROW0` | `MT9V03X_H * 3 / 4` | 参考白点采样起始行 |
| `TRACK_REF_ROW1` | `MT9V03X_H - 2` | 参考白点采样终止行 |
| `TRACK_REF_COL0` | `MT9V03X_W * 1 / 3` | 参考白点采样起始列 |
| `TRACK_REF_COL1` | `MT9V03X_W * 2 / 3` | 参考白点采样终止列 |
| `TRACK_REF_STEP` | `2` | 参考白点采样步长 |
| `TRACK_MID_COL` | `MT9V03X_W / 2`（= 94） | 图像中线列坐标，用于计算偏差 |

> **提示**：`TRACK_CONTRAST_THRESH` 是最关键的调参点。赛道对比度高（室内标准赛道）可适当增大（90~120）；光线较暗或对比度低时应减小（50~70）。

---

## 14. 数据结构

### 14.1 `track_line_t` — 单行搜线结果

```c
typedef struct
{
    uint8 valid;   // 1=本行搜线有效  0=无效（未找到白区或边界）
    uint8 left;    // 左边界列坐标
    uint8 right;   // 右边界列坐标
    uint8 mid;     // 中线列坐标 = (left + right) / 2
} track_line_t;
```

### 14.2 `track_t` — 整帧搜线结果，供 PID 及上层逻辑读取

```c
typedef struct
{
    /* ---- 基础搜线数据 ---- */
    uint8        ref;                 // 参考白点灰度均值
    uint8        src_row;             // 搜线起点行（最长白列所在行）
    uint8        src_col;             // 搜线起点列（最长白列所在列）
    track_line_t lines[MT9V03X_H];    // 每行搜线结果（仅 src_row 附近有效行被填充）

    /* ---- PID 接口数据 ---- */
    int16  err;       // 中线偏差 = 有效行中线均值 - TRACK_MID_COL
                      // 正值：赛道偏右，负值：赛道偏左
    uint8  n_valid;   // 有效搜线行数
    uint8  width;     // 平均赛道宽度（right-left 均值），可用于特殊路况判断
} track_t;
```

---

## 15. API 详细说明

### 15.1 `track_init`

```c
void track_init(void);
```

**功能**：清零内部 `track_t` 缓冲区，复位所有搜线结果。

**调用时机**：`camera_init()` 之后，主循环之前调用一次。

**示例**：
```c
track_init();
// 模块内部已打印：[Track] init ok
```

---

### 15.2 `track_process` ⭐

```c
void track_process(void);
```

**功能**：执行完整三步对比度搜线，结果写入内部 `track_t` 缓冲区。

**调用时机**：每帧 `camera_frame_ready()` 返回 `1` 后调用一次，内部不做帧降频，由调用方决定频率。

**算法流程**：

```
_ref_avg()
         |
    近端 ROI 间隔采样 → ref（判白基准，判白阈值 = ref * 3/4）
         |
_find_start()
         |
    跳行跳列从 ROW_NEAR 向 ROW_FAR 遍历
         |-----> 未找到白区 → printf "[Track] no white area"，返回
         |
    找到最远白列 → src_row, src_col
         |
_scan_edge() × N 行
         |
    从 src_row 向近端逐行（步长 SCAN_ROW_STEP）
    每行向左右跳列（步长 EDGE_STEP），差比和 > THRESH 判边界
         |
    汇总 err, n_valid, width
         |
    printf "[Track] ref=XX src=(XX,XX) valid=XX err=XX width=XX"
```

**示例**：
```c
if (camera_frame_ready())
{
    camera_auto_exposure();
    track_process();
    // 串口输出：[Track] ref=XX src=(XX,XX) valid=XX err=XX width=XX
}
```

**调参建议**：

| 场景 | 建议调整 |
|------|----------|
| 搜线不稳、边界误判 | 增大 `TRACK_CONTRAST_THRESH`（更严格）或减小（更宽松） |
| 最长白列找不到 | 检查 `TRACK_ROW_NEAR/FAR` 范围，或降低判白阈值系数 |
| 需要更高处理速度 | 增大 `TRACK_SCAN_ROW/COL_STEP`（如设为 4，运算量降至约 6%） |
| 需要更高边界精度 | 减小 `TRACK_EDGE_STEP`（设为 `1` = 逐列搜索） |
| 赛道较窄、误搜背景 | 缩小 `TRACK_ROW_FAR` 范围，或增大对比度阈值 |

---

### 15.3 `track_result`

```c
const track_t* track_result(void);
```

**功能**：返回最近一次 `track_process()` 的搜线结果指针（只读）。

**返回值**：`const track_t*`，永不为 NULL，可访问每行 `lines[]` 的详细数据。

**示例**：
```c
const track_t *t = track_result();
if (t->n_valid > 3)
{
    // 有效行数足够，赛道可信
    uint8 w = t->width;   // 平均赛道宽度
}
```

---

### 15.4 `track_error`

```c
int16 track_error(void);
```

**功能**：返回 `track_t.err`，即中线偏差，为 `track_result()` 的快捷访问接口，直接供 PID 控制器使用。

**返回值**：

| 返回值 | 含义 |
|--------|------|
| 正值 | 赛道整体偏右，车需右转修正 |
| 负值 | 赛道整体偏左，车需左转修正 |
| `0` | 居中，无需修正 |

**示例**：
```c
int16 err = track_error();
// 直接作为 PID 误差输入
```

---

## 16. 日志输出说明

所有 `printf` 日志均以 `[Track]` 为前缀，通过串口（`debug_init()` 初始化后）输出，格式如下：

| 触发时机 | 日志内容 |
|----------|----------|
| `track_init()` 调用 | `[Track] init ok` |
| `track_process()` 未找到白区 | `[Track] no white area` |
| `track_process()` 正常完成 | `[Track] ref=<值> src=(<行>,<列>) valid=<行数> err=<偏差> width=<宽>` |

---

## 17. 常见问题

**Q：`track_error()` 返回值一直为 0？**  
A：检查 `track_process()` 是否在 `camera_frame_ready()` 后调用；串口观察是否持续输出 `[Track] no white area`，若是则调低 `TRACK_CONTRAST_THRESH` 或检查曝光设置。

**Q：搜线结果左右抖动严重？**  
A：适当增大 `TRACK_CONTRAST_THRESH`（如调至 100~120），减少噪声点误判为边界；或结合上层对 `err` 做低通滤波。

**Q：`n_valid` 有效行数经常为 0？**  
A：参考白点 `ref` 可能偏低，导致判白阈值过低而找不到白列；检查曝光是否正常，或扩大 `TRACK_REF_ROW0/ROW1`、`TRACK_REF_COL0/COL1` 采样范围。

**Q：远端赛道搜不到？**  
A：调小 `TRACK_ROW_FAR`（如改为 `MT9V03X_H / 8`），扩大向远端的搜索范围。

**Q：`track_process()` 耗时超预期？**  
A：增大 `TRACK_SCAN_ROW_STEP` 和 `TRACK_SCAN_COL_STEP`（如设为 4），或减小 `TRACK_ROW_NEAR` 与 `TRACK_ROW_FAR` 之间的行范围，缩短遍历距离。
