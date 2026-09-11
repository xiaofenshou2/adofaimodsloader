#ifndef ADOFAI_MOD_LOADER_H
#define ADOFAI_MOD_LOADER_H

#if defined(ADOFAI_NO_JNI)
#include "jni_stub.h"   /* 主机测试用桩 */
#else
#include <jni.h>         /* Android NDK 提供 */
#endif

/* 错误码 */
#define MOD_OK           0
#define MOD_ERR_FORMAT   1   /* 不支持的格式 */
#define MOD_ERR_ZIP      2   /* zip 损坏 */
#define MOD_ERR_ROOT_DIR 3   /* 根目录含文件夹 */
#define MOD_ERR_LOAD     4   /* dll/so 加载失败 */

void   mod_init(JavaVM *vm, void *mod_host);
void   mod_set_java(JavaVM *vm, void *mod_host);
void   mod_on_file_selected(const char *path);
int    mod_check_extension(const char *path);
int    mod_check_root_clean(const char *zip_path);
int    mod_get_loaded_count(void);

#endif /* ADOFAI_MOD_LOADER_H */
