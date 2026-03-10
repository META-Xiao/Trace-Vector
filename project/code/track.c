/*********************************************************************************************************************
 * 文件名称          track.c
 * 功能描述          基于对比度算法的摄像头循迹模块（跳行跳列优化版）
 * 团队              Trace Vector
 * 创建日期          2026-03-10
 *
 * 算法说明
 *   三步对比度搜线（均采用跳行跳列优化，综合耗时约 2～7ms）：
 *     Step 1  参考白点：近端 ROI 间隔采样求灰度均值，作为判白基准。
 *     Step 2  最长白列：从近端向远端按 TRACK_SCAN_ROW_STEP/COL_STEP 跳格遍历，
 *             找到最远的满足白色条件的列，记录为搜线起点。
 *     Step 3  左右边界：从起点列向左右按 TRACK_EDGE_STEP 跳点扩展，
 *             用差比和（对比度）判断黑白边界。
 *
 *   对比度公式（Q8 整数近似）：
 *     contrast = |a - b| * 128 / ((a + b) / 2 + 1)
 *     contrast > TRACK_CONTRAST_THRESH  =>  此处为边界
 *
 *   PID 接口：track_error() 返回中线偏差，正值赛道偏右，负值偏左。
 *
 * 修改记录
 * 日期              作者           备注
 * 2026-03-10        Trace Vector   first version
 * 2026-03-10        Trace Vector   精简函数名与变量名
 ********************************************************************************************************************/

#include "zf_common_headfile.h"
#include "track.h"

/* ------------------------------------------------- 模块私有变量 ------------------------------------------------- */

static track_t s_track;   // 最近一帧搜线结果

/* ------------------------------------------------- 私有工具函数 -------------------------------------------------- */

/*--------------------------------------------------------------------------------------------------------------------
 * 函数简介     对比度计算（差比和，Q8 整数近似，避免浮点和32位除法）
 * 参数说明     a, b  两个像素灰度值
 * 返回参数     uint8  对比度值 0~255，越大说明差异越明显
 *------------------------------------------------------------------------------------------------------------------*/
static uint8 _contrast(uint8 a, uint8 b)
{
    uint8 diff;
    uint8 avg;
    if (a >= b)
    {
        diff = a - b;
        avg  = (uint8)((a + b) >> 1);
    }
    else
    {
        diff = b - a;
        avg  = (uint8)((a + b) >> 1);
    }
    if (avg == 0) return 0;
    return (uint8)((uint16)diff * 128u / avg);
}

/*--------------------------------------------------------------------------------------------------------------------
 * 函数简介     Step 1：参考白点均值 —— 近端 ROI 间隔采样
 * 返回参数     uint8  参考白点灰度均值
 *------------------------------------------------------------------------------------------------------------------*/
static uint8 _ref_avg(void)
{
    uint16 sum   = 0;
    uint8  count = 0;
    uint8  r, c;

    for (r = (uint8)TRACK_REF_ROW0; r < (uint8)TRACK_REF_ROW1; r += TRACK_REF_STEP)
    {
        for (c = (uint8)TRACK_REF_COL0; c < (uint8)TRACK_REF_COL1; c += TRACK_REF_STEP)
        {
            sum += mt9v03x_image[r][c];
            count++;
        }
    }
    if (count == 0) return 128u;
    return (uint8)(sum / count);
}

/*--------------------------------------------------------------------------------------------------------------------
 * 函数简介     Step 2：最长白列 —— 从近端向远端跳格遍历，找最远白列作为搜线起点
 * 参数说明     ref     参考白点均值（判白阈值 = ref * 3 / 4）
 *             p_row   输出：起点行
 *             p_col   输出：起点列
 * 返回参数     uint8  1=找到  0=未找到
 *------------------------------------------------------------------------------------------------------------------*/
static uint8 _find_start(uint8 ref, uint8 *p_row, uint8 *p_col)
{
    uint8 thresh   = (uint8)((uint16)ref * 3u / 4u);
    uint8 best_row = (uint8)TRACK_ROW_NEAR;
    uint8 best_col = (uint8)TRACK_MID_COL;
    uint8 found    = 0;
    uint8 r, c;

    r = (uint8)TRACK_ROW_NEAR;
    while (r >= (uint8)TRACK_ROW_FAR)
    {
        for (c = TRACK_SCAN_COL_STEP;
             c < (uint8)(MT9V03X_W - TRACK_SCAN_COL_STEP);
             c += TRACK_SCAN_COL_STEP)
        {
            if (mt9v03x_image[r][c] >= thresh)
            {
                if (!found || r < best_row)
                {
                    best_row = r;
                    best_col = c;
                    found    = 1;
                }
                else if (r == best_row)
                {
                    /* 同一行优先取更靠近中线的列 */
                    uint8 d_cur  = (c > TRACK_MID_COL)
                                 ? (uint8)(c - TRACK_MID_COL)
                                 : (uint8)(TRACK_MID_COL - c);
                    uint8 d_best = (best_col > TRACK_MID_COL)
                                 ? (uint8)(best_col - TRACK_MID_COL)
                                 : (uint8)(TRACK_MID_COL - best_col);
                    if (d_cur < d_best)
                        best_col = c;
                }
            }
        }
        if (r < TRACK_SCAN_ROW_STEP) break;
        r -= TRACK_SCAN_ROW_STEP;
    }

    *p_row = best_row;
    *p_col = best_col;
    return found;
}

/*--------------------------------------------------------------------------------------------------------------------
 * 函数简介     Step 3：单行左右边界搜索（跳点 + 对比度判断）
 * 参数说明     row       目标行号
 *             src_col   起始列（最长白列位置）
 *             p_left    输出：左边界列坐标
 *             p_right   输出：右边界列坐标
 * 返回参数     uint8  1=双边界均找到  0=至少一侧无效
 *------------------------------------------------------------------------------------------------------------------*/
static uint8 _scan_edge(uint8 row, uint8 src_col, uint8 *p_left, uint8 *p_right)
{
    int16 c;
    uint8 left        = 0;
    uint8 right       = (uint8)(MT9V03X_W - 1);
    uint8 left_found  = 0;
    uint8 right_found = 0;

    /* 向左搜索 */
    for (c = (int16)src_col - TRACK_EDGE_STEP; c >= TRACK_EDGE_STEP; c -= TRACK_EDGE_STEP)
    {
        if (_contrast(mt9v03x_image[row][(uint8)c],
                      mt9v03x_image[row][(uint8)(c - TRACK_EDGE_STEP)]) > TRACK_CONTRAST_THRESH)
        {
            left       = (uint8)c;
            left_found = 1;
            break;
        }
    }

    /* 向右搜索 */
    for (c = (int16)src_col + TRACK_EDGE_STEP;
         c <= (int16)(MT9V03X_W - 1 - TRACK_EDGE_STEP);
         c += TRACK_EDGE_STEP)
    {
        if (_contrast(mt9v03x_image[row][(uint8)c],
                      mt9v03x_image[row][(uint8)(c + TRACK_EDGE_STEP)]) > TRACK_CONTRAST_THRESH)
        {
            right       = (uint8)c;
            right_found = 1;
            break;
        }
    }

    *p_left  = left_found  ? left  : 0;
    *p_right = right_found ? right : (uint8)(MT9V03X_W - 1);
    return (uint8)(left_found && right_found);
}

/* ------------------------------------------------- 对外接口实现 -------------------------------------------------- */

/*--------------------------------------------------------------------------------------------------------------------
 * 函数简介     循迹模块初始化
 *------------------------------------------------------------------------------------------------------------------*/
void track_init(void)
{
    uint8 i;
    s_track.ref     = 0;
    s_track.src_row = 0;
    s_track.src_col = (uint8)TRACK_MID_COL;
    s_track.err     = 0;
    s_track.n_valid = 0;
    s_track.width   = 0;
    for (i = 0; i < MT9V03X_H; i++)
    {
        s_track.lines[i].valid = 0;
        s_track.lines[i].left  = 0;
        s_track.lines[i].right = (uint8)(MT9V03X_W - 1);
        s_track.lines[i].mid   = (uint8)TRACK_MID_COL;
    }
    printf("[Track] init ok\r\n");
}

/*--------------------------------------------------------------------------------------------------------------------
 * 函数简介     执行一帧搜线（三步对比度算法，跳行跳列优化）
 * 使用示例     if (camera_frame_ready()) { camera_auto_exposure(); track_process(); }
 *------------------------------------------------------------------------------------------------------------------*/
void track_process(void)
{
    uint8  ref;
    uint8  src_row, src_col;
    uint8  found;
    int16  r;
    uint8  left, right, valid;
    int32  mid_sum   = 0;
    uint16 width_sum = 0;
    uint8  n_valid   = 0;

    /* Step 1：参考白点 */
    ref = _ref_avg();
    s_track.ref = ref;

    /* Step 2：最长白列（搜线起点） */
    found = _find_start(ref, &src_row, &src_col);
    s_track.src_row = src_row;
    s_track.src_col = src_col;

    if (!found)
    {
        s_track.n_valid = 0;
        printf("[Track] no white area\r\n");
        return;
    }

    /* Step 3：从起点向近端逐行搜边界 */
    for (r = (int16)src_row; r < (int16)MT9V03X_H; r += TRACK_SCAN_ROW_STEP)
    {
        valid = _scan_edge((uint8)r, src_col, &left, &right);

        s_track.lines[(uint8)r].valid = valid;
        s_track.lines[(uint8)r].left  = left;
        s_track.lines[(uint8)r].right = right;
        s_track.lines[(uint8)r].mid   = (uint8)((left + right) >> 1);

        if (valid)
        {
            mid_sum   += (int16)s_track.lines[(uint8)r].mid;
            width_sum += (uint16)(right - left);
            n_valid++;
        }
    }

    /* 汇总 PID 接口数据 */
    s_track.n_valid = n_valid;
    if (n_valid > 0)
    {
        s_track.err   = (int16)(mid_sum / (int16)n_valid) - (int16)TRACK_MID_COL;
        s_track.width = (uint8)(width_sum / n_valid);
    }
    else
    {
        s_track.err   = 0;
        s_track.width = 0;
    }

    printf("[Track] ref=%u src=(%u,%u) valid=%u err=%d width=%u\r\n",
           (unsigned int)ref,
           (unsigned int)src_row, (unsigned int)src_col,
           (unsigned int)n_valid,
           (int)s_track.err,
           (unsigned int)s_track.width);
}

/*--------------------------------------------------------------------------------------------------------------------
 * 函数简介     获取最近一帧搜线结果指针（只读，永不为NULL）
 *------------------------------------------------------------------------------------------------------------------*/
const track_t* track_result(void)
{
    return &s_track;
}

/*--------------------------------------------------------------------------------------------------------------------
 * 函数简介     获取中线偏差，正值=赛道偏右，负值=赛道偏左，直接供 PID 使用
 *------------------------------------------------------------------------------------------------------------------*/
int16 track_error(void)
{
    return s_track.err;
}
