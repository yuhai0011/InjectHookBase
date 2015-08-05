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

void hookBroadcastMethod() {
    LOGI("skywalker hookBroadcastMethod enter");
    const char *cls = "com/android/server/am/BroadcastQueue";
    const char *fun = "scheduleBroadcastsLocked";
    const char *funsig = "()V";

    JNIEnv* env = android::AndroidRuntime::getJNIEnv();
    HookInfo *info = (HookInfo *) malloc(sizeof(HookInfo));

    info->classDesc = cls;
    info->methodName = fun;
    info->methodSig = funsig;
    info->isStaticMethod = false;

    java_method_hook(env, info);
}

void hookStartServiceMethod() {
    LOGI("skywalker hookStartServiceMethod enter");
    const char *cls = "com/android/server/am/ActiveServices";
    const char *fun = "retrieveServiceLocked";
    const char *funsig =
            "(Landroid/content/Intent;Ljava/lang/String;IIIZZ)Lcom/android/server/am/ActiveServices/ServiceLookupResult;";

    JNIEnv* env = android::AndroidRuntime::getJNIEnv();
    HookInfo *info = (HookInfo *) malloc(sizeof(HookInfo));

    info->classDesc = cls;
    info->methodName = fun;
    info->methodSig = funsig;
    info->isStaticMethod = false;
    //java_method_hook(env, info);
}

void hookMethodNative() {
    LOGI("skywalker hookMethodNative enter");
    hookBroadcastMethod();
    hookStartServiceMethod();
}

void InjectEntry(char** args) {
    LOGI("skywalker: I am in InjectEntry");
    add_System_Service();
    hookMethodNative();
}
