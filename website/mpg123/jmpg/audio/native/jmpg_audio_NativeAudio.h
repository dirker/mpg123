/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class jmpg_audio_NativeAudio */

#ifndef _Included_jmpg_audio_NativeAudio
#define _Included_jmpg_audio_NativeAudio
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     jmpg_audio_NativeAudio
 * Method:    hplay
 * Signature: (I[BII)I
 */
JNIEXPORT jint JNICALL Java_jmpg_audio_NativeAudio_hplay
  (JNIEnv *, jobject, jint, jbyteArray, jint, jint);

/*
 * Class:     jmpg_audio_NativeAudio
 * Method:    hopen
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_jmpg_audio_NativeAudio_hopen
  (JNIEnv *, jobject);

/*
 * Class:     jmpg_audio_NativeAudio
 * Method:    hclose
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_jmpg_audio_NativeAudio_hclose
  (JNIEnv *, jobject, jint);

#ifdef __cplusplus
}
#endif
#endif