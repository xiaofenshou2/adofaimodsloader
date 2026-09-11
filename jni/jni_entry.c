/*
 * JNI 入口：供 Java 侧 com.adofai.mod.ModHost 通过 native 方法调用
 *
 * 对应 Java：
 *   package com.adofai.mod;
 *   public class ModHost {
 *       static { System.loadLibrary("adofai_mod"); }
 *       public static native void onFileSelected(String path);
 *   }
 */

#include <jni.h>
#include <android/log.h>
#include "../core/mod_loader.h"

#define TAG "ADOFAI_JNI"

static const char *const k_class = "com/adofai/mod/ModHost";

/* Java: native void onFileSelected(String) */
static void JNICALL Native_onFileSelected(JNIEnv *env, jclass clazz, jstring path) {
    const char *cstr = (*env)->GetStringUTFChars(env, path, NULL);
    if (cstr) {
        mod_on_file_selected(cstr);
        (*env)->ReleaseStringUTFChars(env, path, cstr);
    }
}

static const JNINativeMethod g_methods[] = {
    {"onFileSelected", "(Ljava/lang/String;)V", (void *)Native_onFileSelected},
};

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    JNIEnv *env;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK)
        return JNI_ERR;

    jclass clazz = (*env)->FindClass(env, k_class);
    if (clazz) {
        /* 创建 ModHost 单例，传入加载器 */
        jmethodID ctor = (*env)->GetMethodID(env, clazz, "<init>", "()V");
        jobject host   = (*env)->NewObject(env, clazz, ctor);
        mod_init(vm, host);

        (*env)->RegisterNatives(env, clazz, g_methods,
                                sizeof(g_methods) / sizeof(g_methods[0]));
        __android_log_print(ANDROID_LOG_DEBUG, TAG, "native methods registered");
    }
    return JNI_VERSION_1_6;
}
