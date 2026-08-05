/*
 * worldgen.c - 海陆轮廓地图生成器(黑色线条)
 *
 * 基于 donjon.bin.sh 的 Fractal Worldmap Generator (worldgen-2.2a.c,
 * John Olsson) 的"故障线"(fault line)地形生成算法,改造为:
 *   - 直接输出海陆轮廓(海岸线)的黑色线条地图(PNG,透明背景)
 *
 * 算法流程:
 *   1. 用随机大圆的故障线叠加生成球面高度图(利用对称性只算半球再镜像)
 *   2. 按水占比(percent_water)经直方图确定海平面
 *   3. 提取陆地/海洋边界像素(海岸线),标记为黑色线条
 *   4. 写入 RGBA PNG(黑色线条 + 透明背景)
 *
 * 相对原版的精度改进:
 *   1. 分辨率提升(默认 2560x1440)
 *   2. 故障次数随分辨率自动缩放,海岸线细节更丰富
 *      (默认约为 宽度/10,例如 2560 宽约 256 次)
 *   3. 对高度图做平滑(5x5 盒式模糊),海岸线更圆润不毛糙
 *   4. 海岸线按线宽参数加粗(默认 3px)
 *
 * 编译: gcc -O3 worldgen.c -lm -lz -o worldgen
 * 用法:
 *   ./worldgen <种子> <故障次数:0=自动> <水占比> [宽度] [高度] [线宽] [输出.png] [-g] [-c N] [-fill]
 *   例: ./worldgen 42 0 60 2560 1440 3 图片/地图_生成.png
 *   例: ./worldgen 7 250 65 2560 1440 1 图片/地图_生成.png -c 8
 *   例: ./worldgen 7 250 65 2560 1440 1 图片/地图_分层设色.png -fill
 *
 * 说明:
 *   - 故障次数传 0 时按宽度自动选取(约为 宽度/10)
 *   - 地图横向(x=经度)环绕,纵向(y=纬度)0..高度-1,比例为 2:1 时最自然
 *   - 水占比 0..100,越大海洋越多(岛屿越多)
 *   - 线宽为海岸线线条的像素宽度
 *   - 加 -g 会叠加半透明的经纬网格线
 *   - 加 -c N 会额外输出 N 张"等高线切片":不同高度阈值的等值线,
 *     1px 黑色线条,透明背景,命名 <输出基名>_切片_01.png ... ,
 *     供后期逐层合成等高线地图
 *   - 加 -fill 会输出 "分层设色地图":按相对海平面的高度给每个像素
 *     填充颜色(海洋:深蓝->青蓝;陆地:绿->黄绿->棕->白),不透明 PNG
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <zlib.h>
#include <time.h>
#include "worldgen_core.h"

#define PI 3.14159265358979323846
#define NEG_INF (-2147483647)

static int X, Y;              /* 地图尺寸(像素) */
static int YDiv2;
static float YDivPI;
static float *SinTable;       /* 预计算的 sin */
static int *Height;           /* 高度图,索引 x*Y+y (x=经度, y=纬度) */
static unsigned char *Land;   /* 0=海, 1=陆 */

/* ---------- 日志回调(供 GUI 内嵌时回显到界面) ---------- */

static void (*g_log)(const char *msg) = NULL;

void worldgen_set_log(void (*cb)(const char *msg)) { g_log = cb; }

static void log_msg(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (g_log) g_log(buf);
    else fputs(buf, stderr);
}

/* ---------- 高度图: 故障线生成 ---------- */

static void gen_fault(void)
{
    float Alpha, Beta, TanB;
    int Phi, Xsi, row, Theta;
    int flag = rand() & 1;

    Alpha = (((float)rand()) / RAND_MAX - 0.5f) * PI;
    Beta  = (((float)rand()) / RAND_MAX - 0.5f) * PI;
    TanB  = tanf(acosf(cosf(Alpha) * cosf(Beta)));

    row = 0;
    Xsi = (int)(X / 2 - (X / PI) * Beta);
    for (Phi = 0; Phi < X; Phi++) {              /* 全宽度,不再只用半球 */
        int idx = (Xsi - Phi + 2 * X) % X;
        Theta = (int)(YDivPI * atanf(SinTable[idx] * TanB)) + YDiv2;
        if (Theta < 0) Theta = 0;
        if (Theta >= Y) Theta = Y - 1;
        if (flag) {
            if (Height[row + Theta] != NEG_INF) Height[row + Theta]--;
            else Height[row + Theta] = -1;
        } else {
            if (Height[row + Theta] != NEG_INF) Height[row + Theta]++;
            else Height[row + Theta] = 1;
        }
        row += Y;
    }
}

static void gen_map(int faults)
{
    int j, i, row, Color, Cur;

    /* 初始化:每列纬度0为0, 其余为 NEG_INF */
    for (j = 0, row = 0; j < X; j++) {
        Height[row] = 0;
        for (i = 1; i < Y; i++) Height[row + i] = NEG_INF;
        row += Y;
    }

    for (i = 0; i < faults; i++) gen_fault();

    /* 沿纬度方向累积重建高度(全宽度独立生成,无镜像) */
    for (j = 0, row = 0; j < X; j++) {
        Color = Height[row];
        for (i = 1; i < Y; i++) {
            Cur = Height[row + i];
            if (Cur != NEG_INF) Color += Cur;
            Height[row + i] = Color;
        }
        row += Y;
    }
}

/* 高度图平滑: 5x5 盒式模糊,经度环绕,极区行不参与 */
static void smooth_height(int passes)
{
    int p, x, y, dx, dy;
    int *tmp = (int*)malloc((size_t)X * Y * sizeof(int));
    for (p = 0; p < passes; p++) {
        for (y = 1; y < Y - 1; y++) {
            for (x = 0; x < X; x++) {
                long sum = 0;
                int cnt = 0;
                for (dy = -2; dy <= 2; dy++) {
                    int yy = y + dy;
                    if (yy < 0 || yy >= Y) continue;
                    for (dx = -2; dx <= 2; dx++) {
                        int xx = (x + dx + X) % X;
                        sum += Height[xx * Y + yy];
                        cnt++;
                    }
                }
                tmp[x * Y + y] = (int)(sum / cnt);
            }
        }
        for (y = 1; y < Y - 1; y++)
            for (x = 0; x < X; x++)
                Height[x * Y + y] = tmp[x * Y + y];
    }
    free(tmp);
}

/* 离散度变换: 向高度图注入中频噪声,打碎大陆形成分散群岛
 * dispersion: 0(大陆) ~ 100(群岛)
 * 原理: 生成低分辨率噪声网格,双线性插值到全分辨率后叠加到高度图 */
static void apply_dispersion(int dispersion)
{
    int MinZ = 1, MaxZ = -1, i, x, y;
    int noiseW, noiseH, amp;
    float *noise, strength, range;

    if (dispersion <= 0) return;
    strength = dispersion / 100.0f;

    for (i = 0; i < X * Y; i++) {
        if (Height[i] > MaxZ) MaxZ = Height[i];
        if (Height[i] < MinZ) MinZ = Height[i];
    }
    range = (float)(MaxZ - MinZ);
    if (range <= 0) return;

    /* 噪声分辨率: 约为地图的 1/16,保证中频特征 */
    noiseW = X / 16; if (noiseW < 4) noiseW = 4;
    noiseH = Y / 16; if (noiseH < 4) noiseH = 4;

    noise = (float*)malloc((size_t)noiseW * noiseH * sizeof(float));
    for (i = 0; i < noiseW * noiseH; i++)
        noise[i] = (float)(rand() / (double)RAND_MAX * 2.0 - 1.0);  /* -1 ~ +1 */

    amp = (int)(range * strength * 0.5f);  /* 噪声振幅随离散度增大 */
    if (amp < 1) amp = 1;

    for (y = 0; y < Y; y++) {
        float fy = (float)y / Y * noiseH;
        int ny0 = (int)fy % noiseH;
        float ty = fy - (int)fy;
        int ny1 = (ny0 + 1) % noiseH;
        for (x = 0; x < X; x++) {
            float fx = (float)x / X * noiseW;
            int nx0 = (int)fx % noiseW;
            float tx = fx - (int)fx;
            int nx1 = (nx0 + 1) % noiseW;

            float n00 = noise[ny0 * noiseW + nx0];
            float n01 = noise[ny0 * noiseW + nx1];
            float n10 = noise[ny1 * noiseW + nx0];
            float n11 = noise[ny1 * noiseW + nx1];
            float n0 = n00 * (1.0f - tx) + n01 * tx;
            float n1 = n10 * (1.0f - tx) + n11 * tx;
            float n  = n0 * (1.0f - ty) + n1 * ty;

            Height[x * Y + y] += (int)(n * amp);
        }
    }
    free(noise);
}

/* ---------- 海平面与海陆分类 ---------- */

static int compute_sealevel(int percent_water)
{
    int hist[256] = {0};
    int i, Color, MinZ = 1, MaxZ = -1;
    int Threshold, Count, j;

    for (i = 0; i < X * Y; i++) {
        if (Height[i] > MaxZ) MaxZ = Height[i];
        if (Height[i] < MinZ) MinZ = Height[i];
    }
    for (i = 0; i < X * Y; i++) {
        Color = (int)(((float)(Height[i] - MinZ + 1) / (float)(MaxZ - MinZ + 1)) * 30) + 1;
        if (Color < 1) Color = 1;
        if (Color > 30) Color = 30;
        hist[Color]++;
    }
    Threshold = percent_water * X * Y / 100;
    for (j = 0, Count = 0; j < 256; j++) {
        Count += hist[j];
        if (Count > Threshold) break;
    }
    return j * (MaxZ - MinZ + 1) / 30 + MinZ;
}

static void classify(int sealevel)
{
    int i;
    Land = (unsigned char*)malloc((size_t)X * Y);
    for (i = 0; i < X * Y; i++)
        Land[i] = (Height[i] >= sealevel) ? 1 : 0;
}

/* ---------- 海岸线(黑色线条) ---------- */

/* 提取 1px 海岸线掩码(陆地像素在4邻域遇到海) */
static unsigned char *coast_mask(void)
{
    unsigned char *mask = (unsigned char*)calloc((size_t)X * Y, 1);
    int x, y;
    for (y = 0; y < Y; y++) {
        for (x = 0; x < X; x++) {
            int idx = x * Y + y;
            if (!Land[idx]) continue;
            {
                int xl = (x + X - 1) % X;
                int xr = (x + 1) % X;
                if (y > 0 && !Land[idx - 1]) { mask[idx] = 1; continue; }
                if (y < Y - 1 && !Land[idx + 1]) { mask[idx] = 1; continue; }
                if (!Land[xl * Y + y]) { mask[idx] = 1; continue; }
                if (!Land[xr * Y + y]) { mask[idx] = 1; continue; }
            }
        }
    }
    return mask;
}

/* 对掩码做 3x3 膨胀(经度环绕),使线条加粗 */
static void dilate_mask(unsigned char *mask, int radius)
{
    int iter, x, y, dx, dy;
    unsigned char *tmp = (unsigned char*)malloc((size_t)X * Y);
    for (iter = 0; iter < radius; iter++) {
        memcpy(tmp, mask, (size_t)X * Y);
        for (y = 0; y < Y; y++) {
            for (x = 0; x < X; x++) {
                if (mask[x * Y + y]) continue;
                for (dy = -1; dy <= 1 && !tmp[x * Y + y]; dy++) {
                    int yy = y + dy;
                    if (yy < 0 || yy >= Y) continue;
                    for (dx = -1; dx <= 1; dx++) {
                        int xx = (x + dx + X) % X;
                        if (mask[xx * Y + yy]) { tmp[x * Y + y] = 1; break; }
                    }
                }
            }
        }
        memcpy(mask, tmp, (size_t)X * Y);
    }
    free(tmp);
}

static void trace_coast(unsigned char *rgba, int line_width, int graticule)
{
    int x, y, i;
    unsigned char *mask;

    memset(rgba, 0, (size_t)X * Y * 4);  /* 全透明背景 */

    mask = coast_mask();
    if (line_width > 1) dilate_mask(mask, line_width - 1);
    for (y = 0; y < Y; y++)
        for (x = 0; x < X; x++)
            if (mask[x * Y + y]) {
                unsigned char *p = rgba + (y * X + x) * 4;
                p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 255;
            }
    free(mask);

    /* 可选: 半透明经纬网格 */
    if (graticule) {
        int nlat = Y / 8, nlon = X / 8;
        if (nlat < 1) nlat = 1;
        if (nlon < 1) nlon = 1;
        for (i = 1; i < Y; i++) {
            if (i % nlat != 0) continue;
            for (x = 0; x < X; x++) {
                unsigned char *p = rgba + (i * X + x) * 4;
                p[0] = p[1] = p[2] = 0; p[3] = 128;
            }
        }
        for (i = 1; i < X; i++) {
            if (i % nlon != 0) continue;
            for (y = 0; y < Y; y++) {
                unsigned char *p = rgba + (y * X + i) * 4;
                p[0] = p[1] = p[2] = 0; p[3] = 128;
            }
        }
    }
}

/* 按高度阈值提取等高线: 边界(>=level 与 <level)描黑,1px,供合成等高线地图 */
static void trace_contour(unsigned char *rgba, int level)
{
    unsigned char *m = (unsigned char*)malloc((size_t)X * Y);
    int x, y;
    for (y = 0; y < Y; y++)
        for (x = 0; x < X; x++)
            m[x * Y + y] = (Height[x * Y + y] >= level) ? 1 : 0;

    memset(rgba, 0, (size_t)X * Y * 4);
    for (y = 0; y < Y; y++) {
        for (x = 0; x < X; x++) {
            int idx = x * Y + y;
            int xl, xr;
            if (!m[idx]) continue;
            xl = (x + X - 1) % X;
            xr = (x + 1) % X;
            if (y > 0 && !m[idx - 1]) goto hit;
            if (y < Y - 1 && !m[idx + 1]) goto hit;
            if (!m[xl * Y + y]) goto hit;
            if (!m[xr * Y + y]) goto hit;
            continue;
        hit:
            rgba[(y * X + x) * 4 + 3] = 255;
        }
    }
    free(m);
}

/* 线性插值辅助(用于分层设色) */
static void lerp(unsigned char *p, const float *c1, const float *c2, float t)
{
    int k;
    for (k = 0; k < 3; k++) {
        float v = c1[k] + (c2[k] - c1[k]) * t;
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        p[k] = (unsigned char)v;
    }
}

/* 分层设色: 依高度相对海平面填充颜色(不透明 PNG) */
static void render_color(unsigned char *rgba, int sealevel)
{
    int MinZ = 1, MaxZ = -1, x, y, i;
    for (i = 0; i < X * Y; i++) {
        if (Height[i] > MaxZ) MaxZ = Height[i];
        if (Height[i] < MinZ) MinZ = Height[i];
    }
    if (MaxZ <= MinZ) MaxZ = MinZ + 1;

    for (y = 0; y < Y; y++) {
        for (x = 0; x < X; x++) {
            int h = Height[x * Y + y];
            unsigned char *p = rgba + (y * X + x) * 4;
            if (h < sealevel) {
                /* 海洋: 深度 0(海面)~1(最深) */
                int denom = sealevel - MinZ;
                float t = (denom > 0) ? (float)(sealevel - h) / denom : 1.0f;
                if (t < 0.5f)
                    lerp(p, (float[]){150, 200, 220}, (float[]){40, 90, 160}, t / 0.5f);
                else
                    lerp(p, (float[]){40, 90, 160}, (float[]){8, 16, 70}, (t - 0.5f) / 0.5f);
            } else {
                /* 陆地: 海拔 0(海面)~1(峰顶) */
                int denom = MaxZ - sealevel;
                float t = (denom > 0) ? (float)(h - sealevel) / denom : 0.0f;
                if (t < 0.35f)
                    lerp(p, (float[]){70, 160, 80},  (float[]){150, 190, 70},  t / 0.35f);
                else if (t < 0.60f)
                    lerp(p, (float[]){150, 190, 70}, (float[]){200, 170, 90}, (t - 0.35f) / 0.25f);
                else if (t < 0.85f)
                    lerp(p, (float[]){200, 170, 90}, (float[]){180, 120, 80}, (t - 0.60f) / 0.25f);
                else
                    lerp(p, (float[]){180, 120, 80}, (float[]){245, 240, 235}, (t - 0.85f) / 0.15f);
            }
            p[3] = 255;
        }
    }
}

/* ---------- PNG 输出 ---------- */

static void put_be32(unsigned char *b, unsigned long v)
{
    b[0] = (v >> 24) & 0xff; b[1] = (v >> 16) & 0xff;
    b[2] = (v >> 8) & 0xff;  b[3] = v & 0xff;
}

static void write_chunk(FILE *f, const char *type, const unsigned char *data, unsigned long len)
{
    unsigned char hdr[8];
    unsigned long crc;
    put_be32(hdr, len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    crc = crc32(0L, (const Bytef *)type, 4);
    if (len) crc = crc32(crc, data, len);
    put_be32(hdr, crc);
    fwrite(hdr, 1, 4, f);
}

static int write_png(const char *path, const unsigned char *rgba, int w, int h)
{
    FILE *f;
    unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    unsigned char ihdr[13];
    unsigned long raw_len = (unsigned long)w * 4 + 1;
    unsigned char *raw = (unsigned char*)malloc(raw_len * h);
    uLongf comp_len;
    unsigned char *comp;
    int y;

    f = fopen(path, "wb");
    if (!f) { log_msg("无法写入 %s\n", path); return -1; }
    fwrite(sig, 1, 8, f);

    put_be32(ihdr, (unsigned long)w);
    put_be32(ihdr + 4, (unsigned long)h);
    ihdr[8] = 8;       /* bit depth */
    ihdr[9] = 6;       /* color type: RGBA */
    ihdr[10] = 0;      /* compression */
    ihdr[11] = 0;      /* filter */
    ihdr[12] = 0;      /* interlace */
    write_chunk(f, "IHDR", ihdr, 13);

    for (y = 0; y < h; y++) {
        raw[y * raw_len] = 0; /* filter: none */
        memcpy(raw + y * raw_len + 1, rgba + (unsigned long)y * w * 4, (size_t)w * 4);
    }

    comp_len = compressBound(raw_len * h);
    comp = (unsigned char*)malloc(comp_len);
    if (compress2(comp, &comp_len, raw, raw_len * h, 9) != Z_OK) {
        log_msg("zlib 压缩失败\n");
        fclose(f); free(raw); free(comp); return -1;
    }
    write_chunk(f, "IDAT", comp, comp_len);
    write_chunk(f, "IEND", NULL, 0);

    fclose(f);
    free(raw); free(comp);
    return 0;
}

/* ---------- 旧版兼容 API(默认 PNG) ---------- */
int worldgen_run_png(int seed, int faults, int water, int dispersion,
                     int w, int h, int line_width,
                     int graticule, int slices, int fill, const char *out)
{
    return worldgen_run(seed, faults, water, dispersion, w, h, line_width,
                        graticule, slices, fill, out, WORLDGEN_FMT_PNG);
}

/* ---------- SVG 输出:矢量格式,基于海陆多边形路径 ---------- */

/* SVG 写一个矩形像素(用于像素级描边的精简表示) */
static void svg_pixel_rect(FILE *f, int x, int y, const char *color, float opacity, int sz)
{
    fprintf(f, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"%s\" opacity=\"%.2f\"/>\n",
            x, y, sz, sz, color, opacity);
}

/* ---------- SVG 海岸线(基于行游程 RLE 的闭合路径,体积远小于逐像素 edge-walk) ---------- */

/* 构建每行每个 mask=1 的水平连续段(start_x, end_x_exclusive),
 * 然后把每个段的顶边和底边写成 SVG path 的 H/V 段。
 * 对描边模式(-fill=0 海岸线):我们输出海陆边界像素(即 mask 海岸线膨胀后)的 <rect x,y width=1 height=1 fill=black/>。
 * 这比边游走更稳定,不会死循环。
 */
static void svg_trace_contours(FILE *f, const unsigned char *mask,
                               const char *color, float stroke_width, int is_closed)
{
    int x, y, px, py;
    unsigned char *dilated = NULL;
    int radius = (int)(stroke_width + 0.5f) - 1;
    if (radius < 0) radius = 0;
    if (radius > 0) {
        dilated = (unsigned char*)calloc((size_t)X * Y, 1);
        int r = radius;
        for (py = 0; py < Y; py++)
            for (px = 0; px < X; px++) {
                int dx, dy;
                if (!mask[px * Y + py]) continue;
                for (dy = -r; dy <= r; dy++)
                    for (dx = -r; dx <= r; dx++) {
                        int yy = py + dy;
                        if (yy < 0 || yy >= Y) continue;
                        int xx = (px + dx + X) % X;
                        dilated[xx * Y + yy] = 1;
                    }
            }
        mask = dilated;
    }
    (void)is_closed;

    /* 输出每一个海陆边界(4 邻域检测,与 coast_mask 逻辑一致)像素为 1x1 rect。
     * 为了压缩体积,每行先做游程(RLE):对连续海岸边界像素段,合并写一个 width=N 的 rect。*/
    for (y = 0; y < Y; y++) {
        int run_x = -1, run_len = 0;
        for (x = 0; x <= X; x++) {
            int is_coast = 0;
            if (x < X && mask[x * Y + y]) {
                int idx = x * Y + y;
                int xl = (x + X - 1) % X;
                int xr = (x + 1) % X;
                if (y > 0 && !mask[idx - 1]) is_coast = 1;
                else if (y < Y - 1 && !mask[idx + 1]) is_coast = 1;
                else if (!mask[xl * Y + y]) is_coast = 1;
                else if (!mask[xr * Y + y]) is_coast = 1;
            }
            if (is_coast) {
                if (run_len == 0) { run_x = x; run_len = 1; }
                else if (x - run_x == run_len) run_len++;  /* 延续 */
                else {
                    fprintf(f, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"1\" fill=\"%s\"/>\n",
                            run_x, y, run_len, color);
                    run_x = x; run_len = 1;
                }
            } else {
                if (run_len > 0) {
                    fprintf(f, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"1\" fill=\"%s\"/>\n",
                            run_x, y, run_len, color);
                    run_len = 0;
                }
            }
        }
    }
    free(dilated);
}

/* 分层设色 SVG:按高度分段给像素方块填色。对于大尺寸直接逐像素输出 <rect> 会过大,
 * 故用 <path> 把同色连续区域合并(按行游程编码,用 H/V 构造路径)。这里使用简单方案:
 *   每个高度等级做区域连通域,对每个域输出一个简化路径(按 bbox + 方块)。
 * 为避免 SVG 过大,使用按行 RLE 的路径构造:对每行相同颜色的水平连续段写矩形。
 */
static void svg_render_color(FILE *f, int sealevel)
{
    int MinZ = 1, MaxZ = -1, x, y, i;
    for (i = 0; i < X * Y; i++) {
        if (Height[i] > MaxZ) MaxZ = Height[i];
        if (Height[i] < MinZ) MinZ = Height[i];
    }
    if (MaxZ <= MinZ) MaxZ = MinZ + 1;

    /* 用 6 段色阶(2 海洋 + 4 陆地)合并类别,减少输出 */
    for (y = 0; y < Y; y++) {
        int run_x = 0;
        int run_color_hex = 0;
        int run_len = 0;
        for (x = 0; x < X; x++) {
            int h = Height[x * Y + y];
            unsigned char rgb[3];
            float c1[3], c2[3], t;
            int denom;
            if (h < sealevel) {
                denom = sealevel - MinZ;
                t = (denom > 0) ? (float)(sealevel - h) / denom : 1.0f;
                if (t < 0.5f) {
                    c1[0]=150; c1[1]=200; c1[2]=220;
                    c2[0]=40;  c2[1]=90;  c2[2]=160;
                    t /= 0.5f;
                } else {
                    c1[0]=40;  c1[1]=90;  c2[2]=160;
                    c2[0]=8;   c2[1]=16;  c2[2]=70;
                    t = (t - 0.5f) / 0.5f;
                }
            } else {
                denom = MaxZ - sealevel;
                t = (denom > 0) ? (float)(h - sealevel) / denom : 0.0f;
                if (t < 0.35f) {
                    c1[0]=70;  c1[1]=160; c1[2]=80;
                    c2[0]=150; c2[1]=190; c2[2]=70;
                    t /= 0.35f;
                } else if (t < 0.60f) {
                    c1[0]=150; c1[1]=190; c1[2]=70;
                    c2[0]=200; c2[1]=170; c2[2]=90;
                    t = (t - 0.35f) / 0.25f;
                } else if (t < 0.85f) {
                    c1[0]=200; c1[1]=170; c1[2]=90;
                    c2[0]=180; c2[1]=120; c2[2]=80;
                    t = (t - 0.60f) / 0.25f;
                } else {
                    c1[0]=180; c1[1]=120; c1[2]=80;
                    c2[0]=245; c2[1]=240; c2[2]=235;
                    t = (t - 0.85f) / 0.15f;
                }
            }
            for (int k = 0; k < 3; k++) {
                float v = c1[k] + (c2[k] - c1[k]) * t;
                if (v < 0) v = 0; if (v > 255) v = 255;
                rgb[k] = (unsigned char)v;
            }
            int hex = (rgb[0] << 16) | (rgb[1] << 8) | rgb[2];
            if (run_len > 0 && hex == run_color_hex && x - run_x == run_len) {
                /* 延续游程 */
                run_len++;
            } else {
                if (run_len > 0) {
                    fprintf(f, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"1\" fill=\"#%06X\"/>\n",
                            run_x, y, run_len, run_color_hex);
                }
                run_x = x; run_len = 1; run_color_hex = hex;
            }
        }
        if (run_len > 0) {
            fprintf(f, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"1\" fill=\"#%06X\"/>\n",
                    run_x, y, run_len, run_color_hex);
        }
    }
}

/* 主 SVG 输出入口 */
static int write_svg(const char *path, int line_width, int graticule, int fill, int sealevel,
                     int slices, const char *out_base)
{
    FILE *f = fopen(path, "wb");
    if (!f) { log_msg("无法写入 SVG %s\n", path); return -1; }

    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(f, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %d %d\" width=\"%d\" height=\"%d\" shape-rendering=\"crispEdges\">\n",
            X, Y, X, Y);
    fprintf(f, "<style>\n"
               " line,path { shape-rendering: geometricPrecision; }\n"
               " .g { stroke:black;stroke-opacity:0.5;stroke-width:0.5; }\n"
               "</style>\n");
    /* 背景:透明;若分层设色可放一层浅海底色降低 rect 数量压力 */
    if (fill) {
        /* 先做分层设色 */
        svg_render_color(f, sealevel);
    }

    /* 海岸线(海陆边界):黑色矢量描边 */
    {
        float sw = (float)line_width;
        const char *color = fill ? "#1a1a1a" : "#000000";
        svg_trace_contours(f, Land, color, sw, 1);
    }

    /* 经纬网格 */
    if (graticule) {
        int nlat = Y / 8, nlon = X / 8;
        int i;
        if (nlat < 1) nlat = 1;
        if (nlon < 1) nlon = 1;
        fprintf(f, "<g class=\"g\">\n");
        for (i = 1; i < Y; i++) {
            if (i % nlat != 0) continue;
            fprintf(f, "<line x1=\"0\" y1=\"%d\" x2=\"%d\" y2=\"%d\"/>\n", i, X, i);
        }
        for (i = 1; i < X; i++) {
            if (i % nlon != 0) continue;
            fprintf(f, "<line x1=\"%d\" y1=\"0\" x2=\"%d\" y2=\"%d\"/>\n", i, i, Y);
        }
        fprintf(f, "</g>\n");
    }

    fprintf(f, "</svg>\n");
    fclose(f);
    log_msg("SVG 完成:%s (%dx%d, 矢量海岸线)\n", path, X, Y);

    /* 切片:SVG 等高线(每个切片一个文件) */
    if (slices > 0) {
        int MinZ = 1, MaxZ = -1, k, prev, i;
        for (i = 0; i < X * Y; i++) {
            if (Height[i] > MaxZ) MaxZ = Height[i];
            if (Height[i] < MinZ) MinZ = Height[i];
        }
        if (MaxZ <= MinZ) MaxZ = MinZ + 1;
        prev = -1;
        for (k = 0; k < slices; k++) {
            int level = MinZ + (int)((k + 1) * (double)(MaxZ - MinZ) / (slices + 1));
            if (level == prev) continue;
            prev = level;
            /* 二值化:>=level 为前景 */
            unsigned char *m = (unsigned char*)malloc((size_t)X * Y);
            for (i = 0; i < X * Y; i++)
                m[i] = (Height[i] >= level) ? 1 : 0;
            /* 输出 SVG 切片 */
            char sname[600];
            snprintf(sname, sizeof(sname), "%s_切片_%02d.svg", out_base, k + 1);
            FILE *sf = fopen(sname, "wb");
            if (!sf) { log_msg("切片 %02d 写入失败:%s\n", k + 1, sname); free(m); continue; }
            fprintf(sf, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
            fprintf(sf, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %d %d\" width=\"%d\" height=\"%d\" shape-rendering=\"crispEdges\">\n",
                    X, Y, X, Y);
            svg_trace_contours(sf, m, "#000", 0.8f, 1);
            fprintf(sf, "</svg>\n");
            fclose(sf);
            log_msg("切片(SVG) %02d:高度 %d -> %s\n", k + 1, level, sname);
            free(m);
        }
    }

    return 0;
}

/* ---------- 主程序:接受 fmt 参数(PNG/SVG) ---------- */

/* 供 GUI 调用的入口:显式参数,替代原命令行 main() */
int worldgen_run(int seed, int faults, int water, int dispersion,
                 int w, int h, int line_width,
                 int graticule, int slices, int fill, const char *out,
                 int fmt)
{
    int smpass;
    int sealevel;
    unsigned char *rgba = NULL;
    int i;

    if (faults <= 0) faults = (w / 10 < 60) ? 60 : w / 10;
    /* 离散度越低,平滑次数越多(融合大陆);越高越少(保留碎片) */
    smpass = 2;
    if (dispersion < 50) smpass += (50 - dispersion) / 13;  /* 2→5 */
    if (dispersion > 50) smpass -= (dispersion - 50) / 50;  /* 2→1 */
    if (smpass < 1) smpass = 1;

    if (w < 2 || h < 2) { log_msg("尺寸过小\n"); return 1; }
    if (line_width < 1) line_width = 1;
    if (fmt != WORLDGEN_FMT_SVG && fmt != WORLDGEN_FMT_PNG) fmt = WORLDGEN_FMT_PNG;

    X = w; Y = h;
    YDiv2 = Y / 2;
    YDivPI = Y / PI;

    srand((unsigned)seed);

    SinTable = (float*)malloc((size_t)(2 * X) * sizeof(float));
    for (i = 0; i < X; i++)
        SinTable[i] = SinTable[i + X] = sinf(i * 2 * PI / X);

    Height = (int*)malloc((size_t)X * Y * sizeof(int));
    if (!Height) { log_msg("内存不足\n"); return 1; }

    gen_map(faults);
    smooth_height(smpass);
    apply_dispersion(dispersion);
    /* 噪声注入后再做一次轻平滑,避免海岸线锯齿 */
    if (dispersion > 0) smooth_height(1);
    sealevel = compute_sealevel(water);
    classify(sealevel);

    if (fmt == WORLDGEN_FMT_PNG) {
        rgba = (unsigned char*)malloc((size_t)X * Y * 4);
        if (fill)
            render_color(rgba, sealevel);          /* 分层设色地图 */
        else
            trace_coast(rgba, line_width, graticule); /* 黑色线条海岸线 */

        if (write_png(out, rgba, X, Y) != 0) {
            log_msg("输出地图失败:%s\n", out);
            free(rgba); free(Land); free(Height); free(SinTable);
            return 1;
        }
        log_msg("完成:%s (%dx%d, 种子=%d, 故障=%d, 水=%d%%, 离散=%d, 线宽=%d, 海平面=%d)%s PNG\n",
                out, X, Y, seed, faults, water, dispersion, line_width, sealevel,
                fill ? " [分层设色]" : "");
    } else {
        /* SVG: 从 out 路径去除扩展名再重新拼 .svg,保证切片基名正确 */
        char base[512], svgpath[600];
        snprintf(base, sizeof(base), "%s", out);
        {
            size_t n = strlen(base);
            if (n > 4 && !strcmp(base + n - 4, ".png")) {
                strncpy(base + n - 4, ".svg", 4);
                base[n] = '\0';
            } else if (n > 4 && !strcmp(base + n - 4, ".svg")) {
                /* 已是 .svg,保留 */
            } else {
                strncat(base, ".svg", sizeof(base) - n - 1);
            }
        }
        snprintf(svgpath, sizeof(svgpath), "%s", base);
        /* 切片基名:去掉 .svg 后缀(write_svg 会拼 _切片_N.svg) */
        {
            size_t n = strlen(base);
            if (n > 4 && !strcmp(base + n - 4, ".svg")) base[n - 4] = '\0';
        }
        if (write_svg(svgpath, line_width, graticule, fill, sealevel, slices, base) != 0) {
            log_msg("输出 SVG 失败:%s\n", svgpath);
            free(Land); free(Height); free(SinTable);
            return 1;
        }
        log_msg("完成:%s (%dx%d, 种子=%d, 故障=%d, 水=%d%%, 离散=%d, 线宽=%d, 海平面=%d)%s SVG\n",
                svgpath, X, Y, seed, faults, water, dispersion, line_width, sealevel,
                fill ? " [分层设色]" : "");
    }

    /* 等高线切片: PNG 模式才在主流程输出,SVG 模式由 write_svg 内部处理切片 */
    if (fmt == WORLDGEN_FMT_PNG && slices > 0 && rgba != NULL) {
        int MinZ = 1, MaxZ = -1, k, prev;
        char base[512], sname[600];

        for (i = 0; i < X * Y; i++) {
            if (Height[i] > MaxZ) MaxZ = Height[i];
            if (Height[i] < MinZ) MinZ = Height[i];
        }
        if (MaxZ <= MinZ) MaxZ = MinZ + 1;

        snprintf(base, sizeof(base), "%s", out);
        {
            size_t n = strlen(base);
            if (n > 4 && !strcmp(base + n - 4, ".png")) base[n - 4] = '\0';
        }
        prev = -1;
        for (k = 0; k < slices; k++) {
            int level = MinZ + (int)((k + 1) * (double)(MaxZ - MinZ) / (slices + 1));
            if (level == prev) continue;   /* 高度范围太窄时跳过重复 */
            prev = level;
            trace_contour(rgba, level);
            snprintf(sname, sizeof(sname), "%s_切片_%02d.png", base, k + 1);
            if (write_png(sname, rgba, X, Y) == 0)
                log_msg("切片 %02d:高度 %d -> %s\n", k + 1, level, sname);
            else
                log_msg("切片 %02d 写入失败:%s\n", k + 1, sname);
        }
    }

    if (rgba) free(rgba);
    free(Land); free(Height); free(SinTable);
    return 0;
}

