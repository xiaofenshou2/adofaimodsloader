/*
 * 本地验证（主机 gcc，无需 NDK）：
 *   1. 后缀校验
 *   2. zip 根目录纯净度检查（合法 / 含文件夹 / 套文件夹 / 混合 / 不存在）
 *   3. 解压根目录条目
 *   4. 完整加载流程（非法格式不崩溃；合法走完流程）
 *
 * 用系统 `zip` 命令生成测试包，避免手写 zip 结构出错。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdint.h>

#define ADOFAI_NO_JNI 1
#include "../core/mod_loader.c"

int g_pass = 0, g_fail = 0;
#define CHECK(cond) do { if (cond) g_pass++; else { g_fail++; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#cond);} } while(0)

static void gen_zips(void) {
    system("rm -rf zt && mkdir -p zt/good zt/dirty1/folder zt/dirty2/inner zt/mixed/assets && "
           "echo x > zt/good/Mod.dll && echo x > zt/good/icon.png && "
           "echo x > zt/dirty1/Mod.dll && "
           "echo x > zt/dirty2/inner/Mod.dll && "
           "echo x > zt/mixed/Mod.dll && echo x > zt/mixed/assets/icon.png && "
           "rm -f good.zip dirty1.zip dirty2.zip mixed.zip && "
           "cd zt/good   && zip -X ../../good.zip   Mod.dll icon.png        >/dev/null 2>&1 && cd ../.. && "
           "cd zt/dirty1 && zip -rX ../../dirty1.zip .                      >/dev/null 2>&1 && cd ../.. && "
           "cd zt/dirty2 && zip -rX ../../dirty2.zip .                      >/dev/null 2>&1 && cd ../.. && "
           "cd zt/mixed  && zip -rX ../../mixed.zip  .                      >/dev/null 2>&1 && cd ../.. ");
    sleep(1);
}

int main(void) {
    gen_zips();

    /* 1. 后缀 */
    CHECK(mod_check_extension("a.zip") == MOD_OK);
    CHECK(mod_check_extension("A.ZIP") == MOD_OK);
    CHECK(mod_check_extension("a.rar") == MOD_ERR_FORMAT);
    CHECK(mod_check_extension("a.7z")  == MOD_ERR_FORMAT);
    CHECK(mod_check_extension(NULL)    == MOD_ERR_FORMAT);
    CHECK(mod_check_extension("noext") == MOD_ERR_FORMAT);
    printf("[1] 后缀校验 OK\n");

    /* 2. 根目录纯净度 */
    CHECK(mod_check_root_clean("good.zip")   == MOD_OK);
    CHECK(mod_check_root_clean("dirty1.zip") == MOD_ERR_ROOT_DIR);
    CHECK(mod_check_root_clean("dirty2.zip") == MOD_ERR_ROOT_DIR);
    CHECK(mod_check_root_clean("mixed.zip")  == MOD_ERR_ROOT_DIR);
    CHECK(mod_check_root_clean("nope.zip")   == MOD_ERR_ZIP);
    printf("[2] 根目录纯净度 OK\n");

    /* 3. 解压（仅根目录条目，子目录文件被跳过） */
    mkdir("extract_out", 0755);
    int n = extract_root_entries("good.zip", "extract_out");
    CHECK(n == 2);   /* Mod.dll + icon.png */
    printf("[3] 解压根目录条目 = %d\n", n);

    /* 4. 完整加载流程 */
    mod_on_file_selected("evil.rar");   /* 非法后缀：仅提示，不加载 */
    mod_on_file_selected("good.zip");   /* 校验通过 -> 解压 -> 扫描 */
    printf("[4] loaded count = %d\n", mod_get_loaded_count());

    /* 清理 */
    system("rm -f good.zip dirty1.zip dirty2.zip mixed.zip; rm -rf zt extract_out");

    printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
