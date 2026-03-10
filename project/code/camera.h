/*********************************************************************************************************************
 * 文件名称          camera.h
 * 功能描述          MT9V03X 摄像头初始化、参数配置、以图像中心为焦点的自动曝光(AE)模块
 * 团队              Trace Vector
 * 创建日期          2026-03-10
 *
 * 修改记录
 * 日期              作者           备注
 * 2026-03-10        Trace Vector   first version
 * 2026-03-10        Trace Vector   新增采样步长宏、AE执行频率宏，提升算法效率
 ********************************************************************************************************************/

#ifndef __CAMERA_H__
#define __CAMERA_H__

#include "zf_common_typedef.h"
#include "zf_device_mt9v03x.h"

/* ------------------------------------------------- 用户可调参数 -------------------------------------------------- */

/* 自动曝光 ROI 区域（以图像中心为焦点）
 * 默认取图像中央 1/3 宽 x 1/3 高 的矩形区域作为测光区
 */
#define CAMERA_AE_ROI_W         ( MT9V03X_W / 3 )
#define CAMERA_AE_ROI_H         ( MT9V03X_H / 3 )
#define CAMERA_AE_ROI_X         ( (MT9V03X_W - CAMERA_AE_ROI_W) / 2 )
#define CAMERA_AE_ROI_Y         ( (MT9V03X_H - CAMERA_AE_ROI_H) / 2 )

/* 目标亮度（0-255），期望 ROI 均值稳定在此值附近 */
#define CAMERA_AE_TARGET        ( 120 )

/* 单次最大曝光调整量 */
#define CAMERA_AE_STEP          ( 16  )

/* 曝光时间范围限制 */
#define CAMERA_EXP_MIN          ( 1   )
#define CAMERA_EXP_MAX          ( 600 )

/* 死区容差：误差在此范围内不调整，防抖 */
#define CAMERA_AE_TOLERANCE     ( 8   )

/* ROI 间隔采样步长
 * 行步长=2、列步长=2 时采样点约为全采样的 1/4，耗时减少约 75%
 * 可选: 1=全采样  2=隔1行/列  4=隔3行/列
 */
#define CAMERA_AE_ROW_STEP      ( 2 )
#define CAMERA_AE_COL_STEP      ( 2 )

/* AE 执行频率（帧间隔）
 * 1=每帧执行  4=每4帧执行一次，其余帧 CPU 留给边缘检测和位置计算
 */
#define CAMERA_AE_FRAME_INTERVAL ( 4 )

/* ------------------------------------------------- 对外接口 ------------------------------------------------------- */

/* 摄像头初始化，clock_init()后调用，返回0成功 */
uint8  camera_init(void);

/* 获取当前曝光时间 */
uint16 camera_get_exposure(void);

/* 手动设置曝光时间 [CAMERA_EXP_MIN, CAMERA_EXP_MAX]，返回0成功 */
uint8  camera_set_exposure(uint16 exp);

/* 自动曝光（含帧降频），每帧采集完成后调用一次 */
void   camera_auto_exposure(void);

/* 获取中心 ROI 平均灰度（0-255） */
uint8  camera_get_center_brightness(void);

/* 检测新帧是否就绪，就绪时自动清标志，返回1就绪/0未就绪 */
uint8  camera_frame_ready(void);

#endif /* __CAMERA_H__ */
