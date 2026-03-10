/*********************************************************************************************************************
 * 文件名称          camera.h
 * 功能描述          MT9V03X 摄像头初始化、参数配置、以图像中心为焦点的自动曝光（AE）模块
 * 团队              Trace Vector
 * 创建日期          2026-03-10
 *
 * 修改记录
 * 日期              作者           备注
 * 2026-03-10        Trace Vector   first version
 ********************************************************************************************************************/

#ifndef __CAMERA_H__
#define __CAMERA_H__

#include "zf_common_typedef.h"
#include "zf_device_mt9v03x.h"

/* ------------------------------------------------- 用户可调参数 -------------------------------------------------- */

/* 自动曝光 ROI 区域（以图像中心为焦点）
 * 默认取图像中央 1/3 宽 × 1/3 高 的矩形区域作为测光区
 * 用户可根据赛道情况适当缩放，但需保证 ROI 在图像范围内
 */
#define CAMERA_AE_ROI_W         ( MT9V03X_W / 3 )          // ROI 宽度（列）
#define CAMERA_AE_ROI_H         ( MT9V03X_H / 3 )          // ROI 高度（行）
#define CAMERA_AE_ROI_X         ( (MT9V03X_W - CAMERA_AE_ROI_W) / 2 )  // ROI 左上角列
#define CAMERA_AE_ROI_Y         ( (MT9V03X_H - CAMERA_AE_ROI_H) / 2 )  // ROI 左上角行

/* 自动曝光目标亮度（0-255）
 * 期望 ROI 区域的平均灰度值稳定在此值附近
 */
#define CAMERA_AE_TARGET        ( 120 )

/* 曝光时间调节步长（每次 AE 计算后的最大调整量）*/
#define CAMERA_AE_STEP          ( 16  )

/* 曝光时间范围限制 */
#define CAMERA_EXP_MIN          ( 1   )
#define CAMERA_EXP_MAX          ( 600 )

/* AE 收敛容差：若实际亮度与目标亮度差值在此范围内则不调整 */
#define CAMERA_AE_TOLERANCE     ( 8   )

/* ------------------------------------------------- 对外接口 ------------------------------------------------------- */

/**
 * @brief   摄像头初始化
 *          完成 MT9V03X 硬件初始化，并将曝光时间设为默认值。
 *          必须在 clock_init() 之后调用。
 * @return  0  成功
 *          非0 失败（摄像头未连接或 SCCB 通信错误）
 */
uint8 camera_init(void);

/**
 * @brief   获取当前摄像头曝光时间
 * @return  当前曝光时间值（1 ~ 600）
 */
uint16 camera_get_exposure(void);

/**
 * @brief   手动设置曝光时间
 *          调用后立即通过 SCCB 下发到摄像头。
 * @param   exp   曝光时间，范围 [CAMERA_EXP_MIN, CAMERA_EXP_MAX]
 * @return  0  成功
 *          1  失败
 */
uint8 camera_set_exposure(uint16 exp);

/**
 * @brief   以图像中心 ROI 为测光区域执行一次自动曝光计算
 *          每采集到一帧新图像后调用一次（在主循环中判断 mt9v03x_finish_flag）。
 *          函数内部读取 ROI 均值，与目标亮度比较后按比例调整曝光时间，
 *          并通过 mt9v03x_set_exposure_time() 下发到摄像头。
 *
 * @note    调用前须确保摄像头已初始化且当前帧采集完毕
 *          (即 mt9v03x_finish_flag == 1)。
 */
void camera_auto_exposure(void);

/**
 * @brief   计算以图像中心为焦点的 ROI 区域平均灰度
 *          可供外部模块读取当前帧的中心亮度信息。
 * @return  ROI 区域像素平均值（0-255）
 */
uint8 camera_get_center_brightness(void);

/**
 * @brief   判断摄像头新帧是否就绪
 *          等价于检查并清除 mt9v03x_finish_flag，
 *          方便主循环以统一接口轮询。
 * @return  1  新帧就绪（已自动清除标志位）
 *          0  尚无新帧
 */
uint8 camera_frame_ready(void);

#endif /* __CAMERA_H__ */

