/*
 * CLI 入口 - 解析命令行参数并调用统一的 worldgen_run() 核心 API
 *
 * 用法:
 *   ./worldgen <种子> <故障次数:0=自动> <水占比> [宽度] [高度] [线宽] [输出.png] [-g] [-c N] [-d N] [-fill]
 *   例: ./worldgen 42 0 60 2560 1440 3 图片/地图_生成.png
 *   例: ./worldgen 7 250 65 2560 1440 1 图片/地图_生成.png -c 8
 *   例: ./worldgen 7 250 65 2560 1440 1 图片/地图_分层设色.png -fill
 *   例: ./worldgen 7 0 60 2560 1440 3 图片/群岛.png -d 80
 *
 * 说明:
 *   - 故障次数传 0 时按宽度自动选取(约为 宽度/10)
 *   - 地图横向(x=经度)环绕,纵向(y=纬度)0..高度-1,比例为 2:1 时最自然
 *   - 水占比 0..100,越大海洋越多(岛屿越多)
 *   - 离散度(-d N) 0..100,0=大片大陆,100=分散群岛,默认 0
 *   - 线宽为海岸线线条的像素宽度
 *   - 加 -g 会叠加半透明的经纬网格线
 *   - 加 -c N 会额外输出 N 张"等高线切片":不同高度阈值的等值线,
 *     1px 黑色线条,透明背景,命名 <输出基名>_切片_01.png ...
 *   - 加 -fill 会输出 "分层设色地图":按相对海平面的高度给每个像素
 *     填充颜色(海洋:深蓝->青蓝;陆地:绿->黄绿->棕->白),不透明 PNG
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "worldgen_core.h"

int main(int argc, char **argv)
{
    int seed, faults, water, w, h, line_width;
    int graticule = 0;
    int slices = 0;
    int fill = 0;
    int dispersion = 0;
    const char *out;
    int i;

    seed       = (argc > 1) ? atoi(argv[1]) : (int)time(NULL);
    faults     = (argc > 2) ? atoi(argv[2]) : 0;      /* 0=自动 */
    water      = (argc > 3) ? atoi(argv[3]) : 60;
    w          = (argc > 4) ? atoi(argv[4]) : 2560;
    h          = (argc > 5) ? atoi(argv[5]) : 1440;
    line_width = (argc > 6) ? atoi(argv[6]) : 3;
    out        = (argc > 7) ? argv[7] : "地图_生成.png";
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-g")) graticule = 1;
        else if (!strcmp(argv[i], "-c") && i + 1 < argc) slices = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-d") && i + 1 < argc) dispersion = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-fill") || !strcmp(argv[i], "-color")) fill = 1;
    }

    return worldgen_run(seed, faults, water, dispersion, w, h, line_width,
                        graticule, slices, fill, out);
}
