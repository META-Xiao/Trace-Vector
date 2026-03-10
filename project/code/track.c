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
 *     contrast = |a - b| * 256 / (a + b + 1)
 *     contrast > TRACK_CONTRAST_THRESH  =>  此处为边界
 *
 *   PID 接口：track_get_error() 返回中线偏差，正值赛道偏右，负值偏左。
 *
 * 修改记录
 * 日期              作者           备注
 * 2026-03-10        META   first version
 ********************************************************************************************************************/

#include "zf_common_headfile.h"
#include "track.h"

/* ------------------------------------------------- 模块私有变量 ------------------------------------------------- */

static track_result_t s_result;   // 最近一帧搜线结果

/* ------------------------------------------------- 私有工具函数 -------------------------------------------------- */

/*--------------------------------------------------------------------------------------------------------------------
 * 函数简介     对比度计算（差比和，Q8 整数近似，避免浮点和32位除法）
 * 参数说明     a, b  两个像素灰度值
 * 返回参数     uint8  对比度值 0-255，越大说明差异越明显
 *------------------------------------------------------------------------------------------------------------------*/
static uint8 _contrast(uint8 a, uint8 b)
{
    uint8 diff;
    uint8 sum;
    if (a >= b)
    {
        diff = a - b;
        sum  = (uint8)((a + b) >> 1);   // 防溢出：取均值代替和，结果等比缩放
    }
    else
    {
        diff = b - a;
        sum  = (uint8)((a + b) >> 1);
    }
    if (sum == 0) return 0;
    /* diff / sum * 128，全部用 uint8 运算，避免32位整型 */
    return (uint8)((uint16)diff * 128u / sum);
}

/*--------------------------------------------------------------------------------------------------------------------
 * 函数简介     Step 1：参考白点 —— 近端 ROI 间隔采样求均值
 * 返回参数     uint8  参考白点灰度均值
 *------------------------------------------------------------------------------------------------------------------*/
static uint8 _calc_ref_white(void)
{
    uint16 sum   = 0;
    uint8  count = 0;
    uint8  r, c;

    for (r = (uint8)TRACK_REF_ROW_START; r < (uint8)TRACK_REF_ROW_END; r += TRACK_REF_STEP)
    {
        for (c = (uint8)TRACK_REF_COL_START; c < (uint8)TRACK_REF_COL_END; c += TRACK_REF_STEP)
        {
            sum += mt9v03x_image[r][c];
            count++;
        }
    }
    if (count == 0) return 128u;
    return (uint8)(sum / count);
}

/*--------------------------------------------------------------------------------------------------------------------
 * 函数简介     Step 2：最长白列 —— 从近端向远端跳格遍历，找最远白色列
 * 参数说明     ref_white   参考白点灰度均值（判白阈值 = ref_white * 3 / 4）
 *             out_row     输出：最远白色列所在行
 *             out_col     输出：最远白色列所在列
 * 返回参数     uint8  1=找到  0=未找到
 * 备注信息
 *   按 TRACK_SCAN_ROW_STEP 行步、TRACK_SCAN_COL_STEP 列步跳格，
 *   运算量约为全遍历的 1/(ROW_STEP*COL_STEP)，默认为 1/9。
 *------------------------------------------------------------------------------------------------------------------*/
static uint8 _find_longest_white_col(
    uint8 ref_white,
    uint8 *out_row,
    uint8 *out_col)
{
    /* 判白阈值：参考白点的 3/4 */
    uint8 white_thresh = (uint8)((uint16)ref_white * 3u / 4u);
    uint8 best_row     = (uint8)TRACK_SCAN_ROW_NEAR;
    uint8 best_col     = (uint8)TRACK_IMG_CENTER_COL;
    uint8 found        = 0;
    uint8 r, c;

    /* 从近端行（大行号）向远端行（小行号）遍历 */
    r = (uint8)TRACK_SCAN_ROW_NEAR;
    while (r >= (uint8)TRACK_SCAN_ROW_FAR)
    {
        for (c = TRACK_SCAN_COL_STEP;
             c < (uint8)(MT9V03X_W - TRACK_SCAN_COL_STEP);
             c += TRACK_SCAN_COL_STEP)
        {
            if (mt9v03x_image[r][c] >= white_thresh)
            {
                /* 找到更远（更小行号）的白点则更新 */
                if (!found || r < best_row)
                {
                    best_row = r;
                    best_col = c;
                    found    = 1;
                }
                /* 同一行找到更靠中间的白列优先 */
                else if (r == best_row)
                {
                    uint8 dist_cur  = (c > TRACK_IMG_CENTER_COL)
                                    ? (uint8)(c - TRACK_IMG_CENTER_COL)
                                    : (uint8)(TRACK_IMG_CENTER_COL - c);
                    uint8 dist_best = (best_col > TRACK_IMG_CENTER_COL)
                                    ? (uint8)(best_col - TRACK_IMG_CENTER_COL)
                                    : (uint8)(TRACK_IMG_CENTER_COL - best_col);
                    if (dist_cur < dist_best)
                    {
                        best_col = c;
                    }
                }
            }
        }
        /* 防止 uint8 下溢（r 到 0 后再减会回绕到 255） */
        if (r < TRACK_SCAN_ROW_STEP) break;
        r -= TRACK_SCAN_ROW_STEP;
    }

    *out_row = best_row;
    *out_col = best_col;
    return found;
}

/*--------------------------------------------------------------------------------------------------------------------
 * 函数简介     Step 3：单行左右边界搜索（跳点 + 对比度判断）
 * 参数说明     row         目标行号
 *             start_col   起始列（最长白列位置）
 *             out_left    输出：左边界列坐标
 *             out_right   输出：右边界列坐标
 * 返回参数     uint8  1=找到有效边界  0=无效
 *------------------------------------------------------------------------------------------------------------------*/
static uint8 _search_row_edge(
    uint8  row,
    uint8  start_col,
    uint8 *out_left,
    uint8 *out_right)
{
    int16 c;
    uint8 left  = 0;
    uint8 right = (uint8)(MT9V03X_W - 1);
    uint8 left_found  = 0;
    uint8 right_found = 0;

    /* 向左搜索边界 */
    for (c = (int16)start_col - TRACK_EDGE_STEP; c >= TRACK_EDGE_STEP; c -= TRACK_EDGE_STEP)
    {
        uint8 pix_a = mt9v03x_image[row][(uint8)c];
        uint8 pix_b = mt9v03x_image[row][(uint8)(c - TRACK_EDGE_STEP)];
        if (_contrast(pix_a, pix_b) > TRACK_CONTRAST_THRESH)
        {
            left       = (uint8)c;
            left_found = 1;
            break;
        }
    }

    /* 向右搜索边界 */
    for (c = (int16)start_col + TRACK_EDGE_STEP;
         c <= (int16)(MT9V03X_W - 1 - TRACK_EDGE_STEP);
         c += TRACK_EDGE_STEP)
    {
        uint8 pix_a = mt9v03x_image[row][(uint8)c];
        uint8 pix_b = mt9v03x_image[row][(uint8)(c + TRACK_EDGE_STEP)];
        if (_contrast(pix_a, pix_b) > TRACK_CONTRAST_THRESH)
        {
            right       = (uint8)c;
            right_found = 1;
            break;
        }
    }

    if (!left_found)  left  = 0;
    if (!right_found) right = (uint8)(MT9V03X_W - 1);

    *out_left  = left;
    *out_right = right;
    return (uint8)(left_found && right_found);
}

/* ------------------------------------------------- 对外接口实现 -------------------------------------------------- */

/*--------------------------------------------------------------------------------------------------------------------
 * 函数简介     循迹模块初始化
 * 返回参数     void
 *------------------------------------------------------------------------------------------------------------------*/
void track_init(void)
{
    /* 清零结果缓冲区 */
    uint8 i;
    s_result.ref_white    = 0;
    s_result.start_row    = 0;
    s_result.start_col    = (uint8)TRACK_IMG_CENTER_COL;
    s_result.center_error = 0;
    s_result.valid_rows   = 0;
    s_result.road_width   = 0;
    for (i = 0; i < MT9V03X_H; i++)
    {
        s_result.rows[i].valid  = 0;
        s_result.rows[i].left   = 0;
        s_result.rows[i].right  = (uint8)(MT9V03X_W - 1);
        s_result.rows[i].center = (uint8)TRACK_IMG_CENTER_COL;
    }
    printf("[Track] init ok\r\n");
}

/*--------------------------------------------------------------------------------------------------------------------
 * 函数简介     执行一帧搜线（三步对比度算法，跳行跳列优化）
 * 返回参数     void
 * 使用示例     if (camera_frame_ready()) { camera_auto_exposure(); track_process(); }
 *------------------------------------------------------------------------------------------------------------------*/
void track_process(void)
{
    uint8  ref_white;
    uint8  start_row, start_col;
    uint8  found;
    int32  center_sum  = 0;
    uint16 width_sum   = 0;
    uint8  valid_count = 0;
    int16  r;
    uint8  left, right;
    uint8  row_valid;

    /* Step 1：参考白点 */
    ref_white = _calc_ref_white();
    s_result.ref_white = ref_white;

    /* Step 2：最长白列 */
    found = _find_longest_white_col(ref_white, &start_row, &start_col);
    s_result.start_row = start_row;
    s_result.start_col = start_col;

    if (!found)
    {
        /* 未找到白区，保留上次结果，只清 valid_rows */
        s_result.valid_rows = 0;
        printf("[Track] no white area found\r\n");
        return;
    }

    /* Step 3：从 start_row 向近端逐行搜索左右边界
     * 每行均跳 TRACK_EDGE_STEP 列，减少遍历次数
     */
    for (r = (int16)start_row; r < (int16)MT9V03X_H; r += TRACK_SCAN_ROW_STEP)
    {
        if (r >= MT9V03X_H) break;

        row_valid = _search_row_edge(
            (uint8)r, start_col, &left, &right);

        s_result.rows[(uint8)r].valid  = row_valid;
        s_result.rows[(uint8)r].left   = left;
        s_result.rows[(uint8)r].right  = right;
        s_result.rows[(uint8)r].center = (uint8)((left + right) >> 1);

        if (row_valid)
        {
            center_sum  += (int16)s_result.rows[(uint8)r].center;
            width_sum   += (uint16)(right - left);
            valid_count++;
        }
    }

    /* 汇总 PID 接口数据 */
    s_result.valid_rows = valid_count;
    if (valid_count > 0)
    {
        int16 avg_center = (int16)(center_sum / (int16)valid_count);
        s_result.center_error = avg_center - (int16)TRACK_IMG_CENTER_COL;
        s_result.road_width   = (uint8)(width_sum / valid_count);
    }
    else
    {
        s_result.center_error = 0;
        s_result.road_width   = 0;
    }

    printf("[Track] ref=%u, start=(%u,%u), valid=%u, err=%d, width=%u\r\n",
           (unsigned int)ref_white,
           (unsigned int)start_row,
           (unsigned int)start_col,
           (unsigned int)valid_count,
           (int)s_result.center_error,
           (unsigned int)s_result.road_width);
}

/*--------------------------------------------------------------------------------------------------------------------
 * 函数简介     获取最近一帧搜线结果指针（只读）
 * 返回参数     const track_result_t*  永不为 NULL
 *------------------------------------------------------------------------------------------------------------------*/
const track_result_t* track_get_result(void)
{
    return &s_result;
}

/*--------------------------------------------------------------------------------------------------------------------
 * 函数简介     获取中线偏差（PID 快捷接口）
 * 返回参数     int16  正值=赛道偏右  负值=赛道偏左
 *------------------------------------------------------------------------------------------------------------------*/
int16 track_get_error(void)
{
    return s_result.center_error;
}

