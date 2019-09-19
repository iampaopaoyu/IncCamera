#include <jni.h>
#include <string>
#include "track/FaceTrack.h"
#include "player/macro.h"
#include "player/IncFFmpeg.h"
#include <android/native_window_jni.h>

using namespace std;

IncFFmpeg *ffmpeg = 0;
JavaVM *javaVm = 0;
ANativeWindow *window = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
JavaCallHelper *helper = 0;



extern "C" {
#include <libavutil/imgutils.h>

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    javaVm = vm;
    return JNI_VERSION_1_6;
}

//画画
void render(uint8_t *data, int lineszie, int w, int h) {
    pthread_mutex_lock(&mutex);
    if (!window) {
        pthread_mutex_unlock(&mutex);
        return;
    }
    //设置窗口属性
    ANativeWindow_setBuffersGeometry(window, w,
                                     h,
                                     WINDOW_FORMAT_RGBA_8888);

    ANativeWindow_Buffer window_buffer;
    if (ANativeWindow_lock(window, &window_buffer, 0)) {
        ANativeWindow_release(window);
        window = 0;
        pthread_mutex_unlock(&mutex);
        return;
    }
    //填充rgb数据给dst_data
    uint8_t *dst_data = static_cast<uint8_t *>(window_buffer.bits);
    // stride：一行多少个数据（RGBA） *4
    int dst_linesize = window_buffer.stride * 4;
    //一行一行的拷贝
    for (int i = 0; i < window_buffer.height; ++i) {
        //memcpy(dst_data , data, dst_linesize);
        memcpy(dst_data + i * dst_linesize, data + i * lineszie, dst_linesize);
    }
    ANativeWindow_unlockAndPost(window);
    pthread_mutex_unlock(&mutex);
}

// -------------------------------------------------------------------视频播放--------------------------------------------------------------------

JNIEXPORT void JNICALL
Java_com_dramascript_inccamera_player_IncPlayer_native_1prepare(JNIEnv *env, jobject instance,
                                                                jstring dataSource_) {
    const char *dataSource = env->GetStringUTFChars(dataSource_, 0);
    //创建播放器
    helper = new JavaCallHelper(javaVm, env, instance);
    ffmpeg = new IncFFmpeg(helper, dataSource);
    ffmpeg->setRenderFrameCallback(render);
    ffmpeg->prepare();
    env->ReleaseStringUTFChars(dataSource_, dataSource);
}

JNIEXPORT void JNICALL
Java_com_dramascript_inccamera_player_IncPlayer_native_1start(JNIEnv *env, jobject thiz) {
    if (ffmpeg) {
        ffmpeg->start();
    }
}

JNIEXPORT void JNICALL
Java_com_dramascript_inccamera_player_IncPlayer_native_1setSurface(JNIEnv *env, jobject thiz,
                                                                   jobject surface) {
    pthread_mutex_lock(&mutex);
    //先释放之前的显示窗口
    if (window) {
        ANativeWindow_release(window);
        window = 0;
    }
    //创建新的窗口用于视频显示
    window = ANativeWindow_fromSurface(env, surface);
    pthread_mutex_unlock(&mutex);
}

JNIEXPORT void JNICALL
Java_com_dramascript_inccamera_player_IncPlayer_native_1stop(JNIEnv *env, jobject thiz) {
//    if (ffmpeg) {
//        ffmpeg->stop();
//    }
//    DELETE(helper);
}

JNIEXPORT void JNICALL
Java_com_dramascript_inccamera_player_IncPlayer_native_1release(JNIEnv *env, jobject thiz) {
    pthread_mutex_lock(&mutex);
    if (window) {
        ANativeWindow_release(window);
        window = 0;
    }
    pthread_mutex_unlock(&mutex);
}
}

// -------------------------------------------------------------------人脸检测--------------------------------------------------------------------

extern "C"
JNIEXPORT jlong JNICALL
Java_com_dramascript_inccamera_filter_face_FaceTrack_native_1create(JNIEnv *env, jobject thiz,
                                                                    jstring model_,
                                                                    jstring seeta_) {
    const char *model = env->GetStringUTFChars(model_, 0);
    const char *seeta = env->GetStringUTFChars(seeta_, 0);

    FaceTrack *faceTrack = new FaceTrack(model, seeta);

    env->ReleaseStringUTFChars(model_, model);
    env->ReleaseStringUTFChars(seeta_, seeta);
    // 返回指针
    return reinterpret_cast<jlong>(faceTrack);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_dramascript_inccamera_filter_face_FaceTrack_native_1start(JNIEnv *env, jobject thiz,
                                                                   jlong self) {
    if (self == 0) {
        return;
    }
    FaceTrack *me = (FaceTrack *) self;
    me->startTracking();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_dramascript_inccamera_filter_face_FaceTrack_native_1stop(JNIEnv *env, jobject thiz,
                                                                  jlong self) {
    if (self == 0) {
        return;
    }
    FaceTrack *me = (FaceTrack *) self;
    me->stopTracking();
    delete me;
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_dramascript_inccamera_filter_face_FaceTrack_native_1detector(JNIEnv *env, jobject thiz,
                                                                      jlong self, jbyteArray data_,
                                                                      jint cameraId, jint width,
                                                                      jint height) {
    if (self == 0) {
        return NULL;
    }
    jbyte *data = env->GetByteArrayElements(data_, NULL);
    FaceTrack *me = (FaceTrack *) self;
    Mat src(height + height / 2, width, CV_8UC1, data);
    //opencv 将yuv转成rgba
    cvtColor(src, src, CV_YUV2RGBA_NV21);
    if (cameraId == 1) {
        //前置
        //逆时针90度
        rotate(src, src, ROTATE_90_COUNTERCLOCKWISE);
        // y 翻转
        flip(src, src, 1);
    } else {
        //后置
        rotate(src, src, ROTATE_90_CLOCKWISE);
    }

    // 灰度化
    cvtColor(src, src, COLOR_RGBA2GRAY);
    //直方图均衡化 增强对比效果
    equalizeHist(src, src);

    vector<Rect2f> rects;
    //送去定位
    me->detector(src, rects);
    env->ReleaseByteArrayElements(data_, data, 0);

    int w = src.cols;
    int h = src.rows;
    src.release();
    int ret = rects.size();
    if (ret) {
        jclass clazz = env->FindClass("com/dramascript/inccamera/filter/face/Face");
        jmethodID costruct = env->GetMethodID(clazz, "<init>", "(IIII[F)V");
        int size = ret * 2;
        //创建java 的float 数组
        jfloatArray floatArray = env->NewFloatArray(size);
        for (int i = 0, j = 0; i < size; j++) {
            float f[2] = {rects[j].x, rects[j].y};
            env->SetFloatArrayRegion(floatArray, i, 2, f);
            i += 2;
        }
        Rect2f faceRect = rects[0];
        int width = faceRect.width;
        int height = faceRect.height;
        jobject face = env->NewObject(clazz, costruct, width, height, w, h,
                                      floatArray);
        return face;
    }
    return NULL;
}

