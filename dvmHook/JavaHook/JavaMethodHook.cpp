#include <cutils/properties.h>

#include <android_runtime/AndroidRuntime.h>

#include "common.h"
#include "JavaMethodHook.h"

extern int dalvik_method_hook_broadcast(JNIEnv*, HookInfo *);
extern int dalvik_method_hook_service(JNIEnv*, HookInfo *);
extern int dalvik_method_hook_provider(JNIEnv*, HookInfo *);
extern void dalvik_add_system_service();

static bool isArt() {
    char value[PROPERTY_VALUE_MAX];
    property_get("persist.sys.dalvik.vm.lib", value, "");
    LOGI("skywalker  [+] persist.sys.dalvik.vm.lib = %s", value);
    return strncmp(value, "libart.so", strlen("libart.so")) == 0;
}

int hook_android_broadcast() {
    LOGI("skywalker hook_android_broadcast enter");
    char *cls = "com/android/server/am/BroadcastQueue";
    char *fun = "scheduleBroadcastsLocked";
    char *funsig = "()V";

    JNIEnv* env = android::AndroidRuntime::getJNIEnv();
    HookInfo *info = (HookInfo *) malloc(sizeof(HookInfo));

    info->classDesc = cls;
    info->methodName = fun;
    info->methodSig = funsig;
    info->isStaticMethod = false;

    return dalvik_method_hook_broadcast(env, info);
}

int hook_android_service() {
    LOGI("skywalker hook_android_service enter");
    char *cls = "com/android/server/am/ActiveServices";
    char *fun = "retrieveServiceLocked";
    char *funsig1 =
            "(Landroid/content/Intent;Ljava/lang/String;IIIZ)Lcom/android/server/am/ActiveServices$ServiceLookupResult;";
    char *funsig2 =
            "(Landroid/content/Intent;Ljava/lang/String;IIIZZ)Lcom/android/server/am/ActiveServices$ServiceLookupResult;";

    JNIEnv* env = android::AndroidRuntime::getJNIEnv();
    HookInfo *info = (HookInfo *) malloc(sizeof(HookInfo));

    info->classDesc = cls;
    info->methodName = fun;
    info->methodSig = funsig1;
    info->isStaticMethod = false;
    int ret = dalvik_method_hook_service(env, info);
    if (ret == -1) {
        info->classDesc = cls;
        info->methodName = fun;
        info->methodSig = funsig2;
        info->isStaticMethod = false;
        return dalvik_method_hook_service(env, info);
    } else {
        return ret;
    }
}

int hook_android_provider() {
    LOGI("skywalker hook_android_provider enter");
    char *cls = "com/android/server/pm/PackageManagerService";
    char *fun = "resolveContentProvider";
    char *funsig = "(Ljava/lang/String;II)Landroid/content/pm/ProviderInfo;";

    JNIEnv* env = android::AndroidRuntime::getJNIEnv();
    HookInfo *info = (HookInfo *) malloc(sizeof(HookInfo));

    info->classDesc = cls;
    info->methodName = fun;
    info->methodSig = funsig;
    info->isStaticMethod = false;
    return dalvik_method_hook_provider(env, info);
}

int dalvik_method_hook() {
    LOGI("skywalker dalvik_method_hook enter");
    if (!isArt()) {
        hook_android_broadcast();
        hook_android_service();
        hook_android_provider();
    }
    return -1;
}

void add_System_Service() {
    if (!isArt()) {
        dalvik_add_system_service();
    }
}

