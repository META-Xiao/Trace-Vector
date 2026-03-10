/*********************************************************************************************************************
 * 文件名称          camera.c
 * 功能描述          MT9V03X 摄像头初始化、参数配置、以图像中心为焦点的自动曝光(AE)模块实现
 * 团队              Trace-Vector
 * 创建日期          2026-03-10
 *
 * 算法说明
 *   自动曝光(AE)采用「中心 ROI 间隔采样测光 + 比例调节 + 帧降频」策略：
 *     1. 每 CAMERA_AE_FRAME_INTERVAL 帧才执行一次 AE，其余帧 CPU 留给边缘/位置处理。
 *     2. 以图像中心 ROI 为测光焦点，按 ROW_STEP/COL_STEP 间隔采样，
 *        采样点约为全采样的 1/4，耗时减少约 75%，均值精度基本不变。
 *     3. 奇偶行列错位，减少固定模式采样偏差。
 *     4. 若误差在 TOLERANCE 死区内则不调整（防抖）。
 *     5. 调整量按误差比例计算并限幅，通过 SCCB 写入摄像头。
 *
 * 修改记录
 * 日期              作者           备注
 * 2026-03-10        META   first version
 * 2026-03-10        META   间隔采样 + 帧降频优化
 * 2026-03-10        META   添加 printf log
 ********************************************************************************************************************/

#include "zf_common_headfile.h"
#include "camera.h"

/* ------------------------------------------------- 模块私有变量 ------------------------------------------------- */

static uint16 s_exposure     = MT9V03X_EXP_TIME_DEF;  // 当前生效的曝光时间
static uint8  s_ae_frame_cnt = 0;                      // AE 帧计数器

/* ------------------------------------------------- 私有函数 ------------------------------------------------------ */

/*--------------------------------------------------------------------------------------------------------------------
 * 函数简介     ROI 间隔采样求均值
 * 返回参数     uint8   平均灰度值(0-255)
 * 备注信息
 *   行步长 CAMERA_AE_ROW_STEP、列步长 CAMERA_AE_COL_STEP（默认均为2）
 *   采样点数约为全采样的 1/4，像素访问次数从 ~2480 降至 ~620
 *   奇偶行列错位（col起点偏移 row&1），减少固定采样偏差
 *------------------------------------------------------------------------------------------------------------------*/
static uint8 _calc_roi_average(void)
{
    uint32 sum       = 0;
    uint16 count     = 0;
    uint8  row, col;
    uint8  col_start;

    for (row = (uint8)CAMERA_AE_ROI_Y;
         row < (uint8)(CAMERA_AE_ROI_Y + CAMERA_AE_ROI_H);
         row += CAMERA_AE_ROW_STEP)
    {
        col_start = (uint8)(CAMERA_AE_ROI_X + (row & 1u));  // 奇偶行列错位

        for (col = col_start;
             col < (uint8)(CAMERA_AE_ROI_X + CAMERA_AE_ROI_W);
             col += CAMERA_AE_COL_STEP)
        {
            sum += mt9v03x_image[row][col];
            count++;
        }
    }

    if (count == 0) return 0;
    return (uint8)(sum / count);
}

/* ------------------------------------------------- 对外接口实现 -------------------------------------------------- */

/*--------------------------------------------------------------------------------------------------------------------
 * 函数简介     摄像头初始化
 * 返回参数     uint8   0-成功  非0-失败
 * 使用示例     camera_init();
 * 备注信息     必须在 clock_init() 完成后调用
 *------------------------------------------------------------------------------------------------------------------*/
uint8 camera_init(void)
{
    uint8 ret = mt9v03x_init();
    if (ret == 0)
    {
        s_exposure     = MT9V03X_EXP_TIME_DEF;
        s_ae_frame_cnt = 0;
        printf("[Camera] init ok, exposure=%u\r\n", (unsigned int)s_exposure);
    }
    else
    {
        printf("[Camera] init failed, ret=%u\r\n", (unsigned int)ret);
    }
    return ret;
}

/*--------------------------------------------------------------------------------------------------------------------
 * 函数简介     获取当前摄像头曝光时间
 * 返回参数     uint16  当前曝光时间
 *------------------------------------------------------------------------------------------------------------------*/
uint16 camera_get_exposure(void)
{
    return s_exposure;
}

/*--------------------------------------------------------------------------------------------------------------------
 * 函数简介     手动设置曝光时间
 * 参数说明     exp   [CAMERA_EXP_MIN, CAMERA_EXP_MAX]
 * 返回参数     uint8   0-成功  1-失败
 * 使用示例     camera_set_exposure(300);
 *------------------------------------------------------------------------------------------------------------------*/
uint8 camera_set_exposure(uint16 exp)
{
    uint8 ret;
    if (exp < CAMERA_EXP_MIN) exp = CAMERA_EXP_MIN;
    if (exp > CAMERA_EXP_MAX) exp = CAMERA_EXP_MAX;

    ret = mt9v03x_set_exposure_time(exp);
    if (ret == 0)
    {
        s_exposure = exp;
        printf("[Camera] set exposure=%u\r\n", (unsigned int)s_exposure);
    }
    else
    {
        printf("[Camera] set exposure failed, ret=%u\r\n", (unsigned int)ret);
    }
    return ret;
}

/*--------------------------------------------------------------------------------------------------------------------
 * 函数简介     自动曝光（含帧降频 + 间隔采样）
 * 返回参数     void
 * 使用示例     if (camera_frame_ready()) { camera_auto_exposure();  再做边缘/位置处理 }
 * 备注信息
 *   每 CAMERA_AE_FRAME_INTERVAL 帧执行一次真正的 AE 计算，
 *   其余帧直接返回，CPU 时间完全留给后续图像处理。
 *------------------------------------------------------------------------------------------------------------------*/
void camera_auto_exposure(void)
{
    int16  error;
    int16  delta;
    int32  new_exp;
    uint8  avg;

    /* 帧降频：未到执行间隔直接跳过 */
    s_ae_frame_cnt++;
    if (s_ae_frame_cnt < CAMERA_AE_FRAME_INTERVAL)
    {
        return;
    }
    s_ae_frame_cnt = 0;

    /* 间隔采样求 ROI 均值 */
    avg   = _calc_roi_average();
    error = (int16)avg - (int16)CAMERA_AE_TARGET;

    printf("[Camera] AE avg=%u, error=%d, exp=%u\r\n",
           (unsigned int)avg, (int)error, (unsigned int)s_exposure);

    /* 死区：误差在容差内不调整，防止频繁抖动 */
    if (error > -(int16)CAMERA_AE_TOLERANCE && error < (int16)CAMERA_AE_TOLERANCE)
    {
        printf("[Camera] AE within tolerance, no adjustment\r\n");
        return;
    }

    /* 比例调节：每 4 灰阶误差调整 1 单位曝光，限幅至 ±STEP */
    delta = -error / 4;
    if (delta >  (int16)CAMERA_AE_STEP) delta =  (int16)CAMERA_AE_STEP;
    if (delta < -(int16)CAMERA_AE_STEP) delta = -(int16)CAMERA_AE_STEP;

    new_exp = (int32)s_exposure + delta;
    if (new_exp < CAMERA_EXP_MIN) new_exp = CAMERA_EXP_MIN;
    if (new_exp > CAMERA_EXP_MAX) new_exp = CAMERA_EXP_MAX;

    printf("[Camera] AE adjust delta=%d, new_exp=%d\r\n", (int)delta, (int)new_exp);
    camera_set_exposure((uint16)new_exp);
}

/*--------------------------------------------------------------------------------------------------------------------
 * 函数简介     获取图像中心 ROI 区域的平均灰度
 * 返回参数     uint8   平均灰度值(0-255)
 * 使用示例     uint8 bright = camera_get_center_brightness();
 *------------------------------------------------------------------------------------------------------------------*/
uint8 camera_get_center_brightness(void)
{
    return _calc_roi_average();
}

/*--------------------------------------------------------------------------------------------------------------------
 * 函数简介     检测新帧是否就绪，就绪时自动清除采集完成标志
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
