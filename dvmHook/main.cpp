#include <jni.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <android_runtime/AndroidRuntime.h>

#include "JavaHook/JavaMethodHook.h"
#include "ElfHook/elfutils.h"
#include "ElfHook/elfhook.h"
#include "common.h"

static inline void get_cstr_from_jstring(JNIEnv* env, jstring jstr, char **out) {
    jboolean iscopy = JNI_TRUE;
    const char *cstr = env->GetStringUTFChars(jstr, &iscopy);
    *out = strdup(cstr);
    env->ReleaseStringUTFChars(jstr, cstr);
}

extern "C" jint Java_com_futureagent_injecthooktrojan_HookUtils_hookMethodNative(JNIEnv *env,
        jobject thiz, jstring cls, jstring methodname, jstring methodsig, jboolean isstatic) {
    HookInfo *info = (HookInfo *) malloc(sizeof(HookInfo));

    get_cstr_from_jstring(env, cls, &info->classDesc);
    get_cstr_from_jstring(env, methodname, &info->methodName);
    get_cstr_from_jstring(env, methodsig, &info->methodSig);

    info->isStaticMethod = isstatic == JNI_TRUE;
    return java_method_hook(env, info);
}

typedef int (*strlen_fun)(const char *);
strlen_fun old_strlen = NULL;

size_t my_strlen(const char *str) {
    LOGI("strlen was called.");
    int len = old_strlen(str);
    return len * 2;
}

strlen_fun global_strlen1 = (strlen_fun) strlen;
strlen_fun global_strlen2 = (strlen_fun) strlen;

static const char *jarPath = "/data/data/com.futureagent.injecthookclient/trojan.jar";
static const char *jarEntCls = "com.futureagent.injecthooktrojan.HookUtils";
static const char *jarEntMtd = "hookTargetMethod";
static const char *jarEntMtdDesc = "()";

void invokeJavaMethod() {
    JNIEnv* env = android::AndroidRuntime::getJNIEnv();
    JavaMethodInfo *info = (JavaMethodInfo *) malloc(sizeof(HookInfo));

    info->jarPath = jarPath;
    info->classDesc = jarEntCls;
    info->methodName = jarEntMtd;
    info->methodSig = jarEntMtdDesc;

    info->isStaticMethod = false;

    invoke_java_method(env, info);
}

/*
static const char *hookCls = "com/example/injecthookbasetarget/InjectTarget";
static const char *hookMtd = "print";
static const char *hookMtdDesc = "()V";
*/

static const char *hookCls = "com/android/server/am/BroadcastQueue";
static const char *hookMtd = "scheduleBroadcastsLocked";
static const char *hookMtdDesc = "()V";

int hookMethodNative() {
    LOGI("skywalker hookMethodNative enter");
    JNIEnv* env = android::AndroidRuntime::getJNIEnv();
    HookInfo *info = (HookInfo *) malloc(sizeof(HookInfo));

    info->classDesc = hookCls;
    info->methodName = hookMtd;
    info->methodSig = hookMtdDesc;
    info->isStaticMethod = false;

    return java_method_hook(env, info);
}

void InjectEntry(char** args) {
    LOGI("skywalker: I am in InjectEntry");
    //invokeJavaMethod();
    hookMethodNative();
}
