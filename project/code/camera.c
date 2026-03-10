/*********************************************************************************************************************
 * 文件名称          camera.c
 * 功能描述          MT9V03X 摄像头初始化、参数配置、以图像中心为焦点的自动曝光（AE）模块实现
 * 团队              Trace Vector
 * 创建日期          2026-03-10
 *
 * 算法说明
 *   自动曝光（AE）采用「中心加权测光 + 比例调节」策略：
 *     1. 以图像中心 1/3 × 1/3 矩形区域（ROI）为测光焦点，计算区域像素均值 avg。
 *     2. 若 |avg - TARGET| <= TOLERANCE，则不调整（防止频繁抖动）。
 *     3. 否则按误差大小等比例缩放曝光时间，并限幅在 [EXP_MIN, EXP_MAX]。
 *     4. 通过 mt9v03x_set_exposure_time() 将新曝光值写入摄像头寄存器。
 *
 * 修改记录
 * 日期              作者           备注
 * 2026-03-10        Trace Vector   first version
 ********************************************************************************************************************/

#include "zf_common_headfile.h"
#include "camera.h"

/* ------------------------------------------------- 模块私有变量 ------------------------------------------------- */

/** 当前生效的曝光时间 */
static uint16 s_exposure = MT9V03X_EXP_TIME_DEF;

/* ------------------------------------------------- 私有函数 ------------------------------------------------------ */

/**
 * @brief  对 ROI 区域内的所有像素求和，返回平均灰度值
 *         ROI 以图像中心为焦点，尺寸由 camera.h 中宏定义决定。
 */
static uint8 _calc_roi_average(void)
{
    uint32 sum   = 0;
    uint16 count = (uint16)CAMERA_AE_ROI_W * (uint16)CAMERA_AE_ROI_H;
    uint8  row, col;

    for (row = CAMERA_AE_ROI_Y; row < (CAMERA_AE_ROI_Y + CAMERA_AE_ROI_H); row++)
    {
        for (col = CAMERA_AE_ROI_X; col < (CAMERA_AE_ROI_X + CAMERA_AE_ROI_W); col++)
        {
            sum += mt9v03x_image[row][col];
        }
    }

    if (count == 0) return 0;
    return (uint8)(sum / count);
}

/* ------------------------------------------------- 对外接口实现 -------------------------------------------------- */

/*--------------------------------------------------------------------------------------------------------------------
 * 函数简介     摄像头初始化
 * 参数说明     void
 * 返回参数     uint8   0-成功  非0-失败
 * 使用示例     camera_init();
 * 备注信息     必须在 clock_init() 完成后调用
 *------------------------------------------------------------------------------------------------------------------*/
uint8 camera_init(void)
{
    uint8 ret = mt9v03x_init();
    if (ret == 0)
    {
        s_exposure = MT9V03X_EXP_TIME_DEF;
    }
    return ret;
}

/*--------------------------------------------------------------------------------------------------------------------
 * 函数简介     获取当前摄像头曝光时间
 * 参数说明     void
 * 返回参数     uint16  当前曝光时间
 * 使用示例     uint16 exp = camera_get_exposure();
 *------------------------------------------------------------------------------------------------------------------*/
uint16 camera_get_exposure(void)
{
    return s_exposure;
}

/*--------------------------------------------------------------------------------------------------------------------
 * 函数简介     手动设置曝光时间
 * 参数说明     exp   曝光时间 [CAMERA_EXP_MIN, CAMERA_EXP_MAX]
 * 返回参数     uint8   0-成功  1-失败
 * 使用示例     camera_set_exposure(300);
 *------------------------------------------------------------------------------------------------------------------*/
uint8 camera_set_exposure(uint16 exp)
{
    if (exp < CAMERA_EXP_MIN) exp = CAMERA_EXP_MIN;
    if (exp > CAMERA_EXP_MAX) exp = CAMERA_EXP_MAX;

    uint8 ret = mt9v03x_set_exposure_time(exp);
    if (ret == 0)
    {
        s_exposure = exp;
    }
    return ret;
}

/*--------------------------------------------------------------------------------------------------------------------
 * 函数简介     以图像中心 ROI 为测光区域执行一次自动曝光调节
 * 参数说明     void
 * 返回参数     void
 * 使用示例     在主循环中，每帧图像采集完成后调用一次：
 *              if (camera_frame_ready()) { camera_auto_exposure(); }
 * 备注信息     内部使用比例调节，调整量限幅为 CAMERA_AE_STEP
 *------------------------------------------------------------------------------------------------------------------*/
void camera_auto_exposure(void)
{
    uint8  avg   = _calc_roi_average();
    int16  error = (int16)avg - (int16)CAMERA_AE_TARGET;
    int16  delta;
    int32  new_exp;

    /* 在容差范围内不做调整，避免频繁抖动 */
    if (error > -(int16)CAMERA_AE_TOLERANCE && error < (int16)CAMERA_AE_TOLERANCE)
    {
        return;
    }

    /*自动曝光逻辑*/
    delta = -error / 4;   /* 粗粒度：每4灰阶误差调整1单位曝光 */
    if (delta >  (int16)CAMERA_AE_STEP) delta =  (int16)CAMERA_AE_STEP;
    if (delta < -(int16)CAMERA_AE_STEP) delta = -(int16)CAMERA_AE_STEP;

    new_exp = (int32)s_exposure + delta;

    /* 限幅 */
    if (new_exp < CAMERA_EXP_MIN) new_exp = CAMERA_EXP_MIN;
    if (new_exp > CAMERA_EXP_MAX) new_exp = CAMERA_EXP_MAX;

    camera_set_exposure((uint16)new_exp);
}

/*--------------------------------------------------------------------------------------------------------------------
 * 函数简介     获取图像中心 ROI 区域的平均灰度
 * 参数说明     void
 * 返回参数     uint8   平均灰度值 (0-255)
 * 使用示例     uint8 bright = camera_get_center_brightness();
 *------------------------------------------------------------------------------------------------------------------*/
uint8 camera_get_center_brightness(void)
{
    return _calc_roi_average();
}

/*--------------------------------------------------------------------------------------------------------------------
 * 函数简介     判断摄像头新帧是否就绪，就绪时自动清除采集完成标志
 * 参数说明     void
 * 返回参数     uint8   1-新帧就绪  0-尚无新帧
 * 使用示例     if (camera_frame_ready()) { ... }
 *------------------------------------------------------------------------------------------------------------------*/
uint8 camera_frame_ready(void)
{
    if (mt9v03x_finish_flag)
    {
        mt9v03x_finish_flag = 0;
        return 1;
    }
    return 0;
}

