#ifndef WORLDGEN_CORE_H
#define WORLDGEN_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

/* 地图生成核心(直接内嵌,不依赖外部 worldgen 进程)
 * 参数:种子,故障次数(0=自动),水占比,离散度(0=大陆,100=群岛),宽,高,线宽,网格,切片数,分层设色,输出路径
 * 返回:0 成功 */
int worldgen_run(int seed, int faults, int water, int dispersion,
                 int w, int h, int line_width,
                 int graticule, int slices, int fill, const char *out);

/* 设置日志回调(NULL=输出到 stderr);供 GUI 内嵌时把核心消息回显到界面 */
void worldgen_set_log(void (*log_cb)(const char *msg));

#ifdef __cplusplus
}
#endif

#endif /* WORLDGEN_CORE_H */
