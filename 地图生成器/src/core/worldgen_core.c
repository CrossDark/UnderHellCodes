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

/* ---------- 主程序 ---------- */

/* 供 GUI 调用的入口:显式参数,替代原命令行 main() */
int worldgen_run(int seed, int faults, int water, int dispersion,
                 int w, int h, int line_width,
                 int graticule, int slices, int fill, const char *out)
{
    int smpass;
    int sealevel;
    unsigned char *rgba;
    int i;

    if (faults <= 0) faults = (w / 10 < 60) ? 60 : w / 10;
    /* 离散度越低,平滑次数越多(融合大陆);越高越少(保留碎片) */
    smpass = 2;
    if (dispersion < 50) smpass += (50 - dispersion) / 13;  /* 2→5 */
    if (dispersion > 50) smpass -= (dispersion - 50) / 50;  /* 2→1 */
    if (smpass < 1) smpass = 1;

    if (w < 2 || h < 2) { log_msg("尺寸过小\n"); return 1; }
    if (line_width < 1) line_width = 1;

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
    log_msg("完成:%s (%dx%d, 种子=%d, 故障=%d, 水=%d%%, 离散=%d, 线宽=%d, 海平面=%d)%s\n",
            out, X, Y, seed, faults, water, dispersion, line_width, sealevel,
            fill ? " [分层设色]" : "");

    /* 等高线切片: 输出 N 张不同高度的切片,供后期合成等高线地图 */
    if (slices > 0) {
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

    free(rgba); free(Land); free(Height); free(SinTable);
    return 0;
}
