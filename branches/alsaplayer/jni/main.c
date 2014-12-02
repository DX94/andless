#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <pthread.h>
#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <sys/prctl.h>
#ifdef ANDROID
#include <android/log.h>
#else
#define _BSD_SOURCE     1
#include <features.h>
#endif
#include <jni_sub.h>
#include "flac/decoder.h"
#include "main.h"

/* Stream parameters must be set by decoder before this call */
int audio_start(playback_ctx *ctx)
{
    int k, ret;

	if(!ctx) {
	    log_err("no context to start");
	    return -1;
	}
	log_info("starting context %p, state %d", ctx, ctx->state);
	pthread_mutex_lock(&ctx->mutex);
	if(ctx->state != STATE_STOPPED) {
	    log_info("context live, stopping");
	    pthread_mutex_unlock(&ctx->mutex);	
	    audio_stop(ctx,1); 
	    pthread_mutex_lock(&ctx->mutex);
	    log_info("live context stopped");	
	}

	ret = alsa_start(ctx);
	if(ret != 0) goto err_init;
	/* period_size is known after alsa_start() */
	k = (ctx->period_size > ctx->block_max) ? ctx->period_size : ctx->block_max;
	/* say, (32k * 8 * 32/8 * 8) = 8M max, reasonable (much less normally) */
	ctx->buff = buffer_create(k * ctx->channels * (ctx->format->phys_bits/8) * 8);
	if(!ctx->buff) {
	    log_err("cannot create buffer");
	    goto err_init;	
        }
	ctx->audio_thread = 0;
	if(pthread_create(&ctx->audio_thread, 0, alsa_write_thread, ctx) != 0) {
	    log_err("cannot create thread");
	    goto err_init;
	}
	ctx->state = STATE_PLAYING;
	ctx->written = 0;
	ctx->stopped = 0;
	ctx->alsa_error = 0;
	pthread_mutex_unlock(&ctx->mutex);
	log_info("playback started");
	return 0;

    err_init:
	if(ctx->buff) {
	    buffer_destroy(ctx->buff);
	    ctx->buff = 0;	
	}	
	pthread_mutex_unlock(&ctx->mutex);
	return LIBLOSSLESS_ERR_INIT;	
}

/* If this function is called on eof from xxx_play (now=0),  wait for playback to complete.
   If it's called from java through audio_init, audio_exit or audio_stop_exp (now=1), stop 
   the stream and wait for xxx_play to exit (which sets stopped=1). 
   This should work no matter the order threads are executing. */

int audio_stop(playback_ctx *ctx, int now) 
{
    if(!ctx) {
	log_err("no context to stop");
	return -1;
    }	
    pthread_mutex_lock(&ctx->mutex);

    log_info("stopping context %p, state %d, now %d", ctx, ctx->state, now);
    if(ctx->state == STATE_STOPPED && (now || ctx->stopped)) {
	log_info("stopped already");
    	pthread_mutex_unlock(&ctx->mutex);
	return -1;
    }
    if(ctx->state == STATE_PAUSED) {
    	ctx->state = STATE_STOPPED;	
	log_info("context was paused");
	pthread_cond_broadcast(&ctx->cond_resumed);
	alsa_resume(ctx);	/* need to set controls, close(ctx->pcm_fd) blocks for 5s otherwise */
    } else if(now) ctx->state = STATE_STOPPED;

    if(ctx->buff) buffer_stop(ctx->buff, now);
    
    if(now) {
	pthread_mutex_lock(&ctx->stop_mutex);
	if(ctx->stopped) log_info("audio_play exited already");
	else {
	    pthread_mutex_unlock(&ctx->mutex);
	    if(ctx->audio_thread) {	
		/* helps with msm hangups */
		log_info("terminating audio_thread");
		pthread_kill(ctx->audio_thread, SIGUSR1);
		log_info("audio_thread terminated");	
	    }
	    log_info("waiting for audio_play to exit");	
	    pthread_cond_wait(&ctx->cond_stopped, &ctx->stop_mutex);
	    log_info("audio_play exited");
	    pthread_mutex_lock(&ctx->mutex);
	}
	pthread_mutex_unlock(&ctx->stop_mutex);
    } else {
	pthread_mutex_unlock(&ctx->mutex);
	if(ctx->audio_thread) {
	    log_info("waiting for audio_thread");
	    pthread_join(ctx->audio_thread, 0);
	    log_info("audio_thread exited");
	}
	pthread_mutex_lock(&ctx->stop_mutex);
	log_info("signalling on completion");
	ctx->stopped = 1;
	pthread_cond_signal(&ctx->cond_stopped);
	pthread_mutex_unlock(&ctx->stop_mutex);
	pthread_mutex_lock(&ctx->mutex);	
    }
    if(ctx->buff) buffer_destroy(ctx->buff);
    ctx->buff = 0;
    alsa_stop(ctx);
    if(ctx->state != STATE_STOPPED) ctx->state = STATE_STOPPED;	/* normal playback exit */

    pthread_mutex_unlock(&ctx->mutex);

    ctx->track_time = 0;	
    return 0;
}

/* Returns 0 in paused state, -1 if stopped.
   Blocks in paused state. */

int check_state(playback_ctx *ctx, const char *func) {
//  pthread_mutex_lock(&ctx->mutex);	
    if(ctx->state != STATE_PLAYING) {	
	if(ctx->state == STATE_PAUSED) {
	    log_info("%s: pause: blocking", func);
	    pthread_mutex_lock(&ctx->mutex);	
	    pthread_cond_wait(&ctx->cond_resumed, &ctx->mutex); /* block until PAUSED */	
	    pthread_mutex_unlock(&ctx->mutex);	
	    log_info("%s: resuming after pause", func);	
	}
	if(ctx->state == STATE_STOPPED) {
	    log_info("%s: stopped from outside", func);	
//	    pthread_mutex_unlock(&ctx->mutex);	
	    return -1;	/* must stop */
	}						
    }
//  pthread_mutex_unlock(&ctx->mutex);	
    return 0;
}

int audio_write(playback_ctx *ctx, void *buff, int size) 
{
    int i = check_state(ctx, __func__);	
	if(i < 0) return -1;
	i = buffer_put(ctx->buff, buff, size);
    return (i == size) ? 0 : -1;
}

static jboolean audio_pause(JNIEnv *env, jobject obj, playback_ctx *ctx) 
{
    bool ret;
    if(!ctx) {
	log_err("no context to pause");
	return false;
    }	
    pthread_mutex_lock(&ctx->mutex);
    if(ctx->state != STATE_PLAYING) {
    	pthread_mutex_unlock(&ctx->mutex);
	log_info("not in playing state");
	return false;
    }	
    ret = alsa_pause(ctx);
    if(ret) {	
	ctx->state = STATE_PAUSED;
	log_info("paused");
    } else log_err("alsa pause failed, locking skipped");	
    pthread_mutex_unlock(&ctx->mutex);
    return ret;		
}

static jboolean audio_resume(JNIEnv *env, jobject obj, playback_ctx *ctx) 
{
    bool ret;
    if(!ctx) {
	log_err("no context to resume");
	return false;
    }	
    pthread_mutex_lock(&ctx->mutex);
    if(ctx->state != STATE_PAUSED) {
    	pthread_mutex_unlock(&ctx->mutex);
	log_info("not in paused state");
	return false;
    } 	
    ret = alsa_resume(ctx);	
    if(!ret) log_err("resume failed, proceeding anyway");
    ctx->state = STATE_PLAYING;	
    pthread_cond_broadcast(&ctx->cond_resumed);
    log_info("resumed");	
    pthread_mutex_unlock(&ctx->mutex);
    return ret;	
}

static jint audio_get_duration(JNIEnv *env, jobject obj, playback_ctx *ctx) 
{
   if(!ctx || (ctx->state != STATE_PLAYING && ctx->state != STATE_PAUSED)) return 0;	
   return ctx->track_time;
}

/* in seconds */
static jint audio_get_cur_position(JNIEnv *env, jobject obj, playback_ctx *ctx) 
{
   if(!ctx || (ctx->state != STATE_PLAYING && ctx->state != STATE_PAUSED)) return 0;
   return ctx->written/ctx->samplerate;
}

#ifdef ANDROID
static 
#endif
jint audio_init(JNIEnv *env, jobject obj, playback_ctx *prev_ctx, jint card, jint device) 
{
    playback_ctx *ctx;

    log_info("audio_init: prev_ctx=%p", prev_ctx);
    if(prev_ctx) {
	audio_stop(prev_ctx, 1);
	if(alsa_select_device(prev_ctx, card, device) != 0) { 
	    return 0;
	}
	ctx = prev_ctx;
    } else {
	ctx = (playback_ctx *) calloc(1, sizeof(playback_ctx));
	if(!ctx) return 0;
	if(alsa_select_device(ctx,card,device) != 0) {
	    free(ctx);	
	    return 0;	
	}
	pthread_mutex_init(&ctx->mutex,0);
	pthread_mutex_init(&ctx->stop_mutex,0);
	pthread_cond_init(&ctx->cond_stopped,0);
	pthread_cond_init(&ctx->cond_resumed,0);
    }
    ctx->state = STATE_STOPPED;
    ctx->track_time = 0;
    log_info("audio_init: return ctx=%p",ctx);
    return (jint) (long) ctx;	
}

#ifdef ANDROID
static
#endif
jboolean audio_exit(JNIEnv *env, jobject obj, playback_ctx *ctx) 
{
    if(!ctx) {
	log_err("zero context");
	return false;
    }	
    log_info("audio_exit: ctx=%p",ctx);
    audio_stop(ctx, 1);
    alsa_close(ctx);
    pthread_mutex_destroy(&ctx->mutex);
    pthread_mutex_destroy(&ctx->stop_mutex);
    pthread_cond_destroy(&ctx->cond_stopped);	
    pthread_cond_destroy(&ctx->cond_resumed);	
    if(ctx->alsa_priv) free(ctx->alsa_priv);	
    free(ctx);	
    return true;
}

static jboolean audio_set_volume(JNIEnv *env, jobject obj, playback_ctx *ctx, jint vol) 
{
    if(!ctx) {
	log_err("no context");
	return false;
    }
    pthread_mutex_lock(&ctx->mutex);
    alsa_set_volume(ctx, vol, 0);	
    pthread_mutex_unlock(&ctx->mutex);
    return true;
}

extern jint audio_play(JNIEnv *env, jobject obj, playback_ctx* ctx, jstring jfile, jint format, jint start) {

    int ret = 0;

	if(!ctx) {
	    log_err("no context");      
	    return LIBLOSSLESS_ERR_NOCTX;
	}

	if(format == FORMAT_FLAC)
	    ret = flac_play(env, obj, ctx, jfile, start);
	else if(format == FORMAT_APE)
	    ret = ape_play(env, obj, ctx, jfile, start);	

	if(ret) log_err("exiting on error %d", ret);
	else log_info("Playback complete.");

	return ret;

}

////////////////////////////////////////////////
////////////////////////////////////////////////
////////////////////////////////////////////////

#ifdef ANDROID

#ifndef CLASS_NAME
#error "CLASS_NAME not set in Android.mk"
#endif

static jboolean libinit(JNIEnv *env, jobject obj, jint sdk) 
{
    return true;
}

static jboolean libexit(JNIEnv *env, jobject obj) 
{
    return true;
}

static JavaVM *gvm;
static jobject giface; 

void update_track_time(JNIEnv *env, jobject obj, int time) 
{
     jclass cls;
     jmethodID mid;
     bool attached = false;
     JNIEnv *envy;

	if((*gvm)->GetEnv(gvm, (void **)&envy, JNI_VERSION_1_4) != JNI_OK) {
            log_err("GetEnv FAILED");
	     if((*gvm)->AttachCurrentThread(gvm, &envy, NULL) != JNI_OK) {
            	log_err("AttachCurrentThread FAILED");
		     return;
	     }	
	     attached = true;	
	}
	cls = (*envy)->GetObjectClass(envy,giface);
	if(!cls) {
          log_err("failed to get class iface");
	  return;  	
	}
        mid = (*env)->GetStaticMethodID(envy, cls, "updateTrackLen", "(I)V");
        if(mid == NULL) {
	  log_err("Cannot find java callback to update time");
         return; 
        }
	(*envy)->CallStaticVoidMethod(envy,cls,mid,time);
	if(attached) (*gvm)->DetachCurrentThread(gvm);
}

static jboolean audio_stop_exp(JNIEnv *env, jobject obj, playback_ctx *ctx) 
{
    return (audio_stop(ctx, 1) == 0);    	
}


static JNINativeMethod methods[] = {
 { "audioInit", "(III)I", (void *) audio_init },
 { "audioExit", "(I)Z", (void *) audio_exit },
 { "audioStop", "(I)Z", (void *) audio_stop_exp },
 { "audioPause", "(I)Z", (void *) audio_pause },
 { "audioResume", "(I)Z", (void *) audio_resume },
 { "audioGetDuration", "(I)I", (void *) audio_get_duration },
 { "audioGetCurPosition", "(I)I", (void *) audio_get_cur_position },
 { "audioSetVolume", "(II)Z", (void *) audio_set_volume },
 { "audioPlay", "(ILjava/lang/String;II)I", (void *) audio_play },
 { "extractFlacCUE", "(Ljava/lang/String;)[I", (void *) extract_flac_cue },
 { "libInit", "(I)Z", (void *) libinit },
 { "libExit", "()Z", (void *) libexit },
};

jint JNI_OnLoad(JavaVM* vm, void* reserved) 
{
    jclass clazz = NULL;
    JNIEnv* env = NULL;
    jmethodID constr = NULL;
    jobject obj = NULL;

      log_info("JNI_OnLoad");
      gvm = vm;	

      if((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_4) != JNI_OK) {
        log_err("GetEnv FAILED");
        return -1;
      }
      prctl(PR_SET_DUMPABLE, 1);
      clazz = (*env)->FindClass(env, CLASS_NAME);
      if(!clazz) {
        log_err("Registration unable to find class '%s'", CLASS_NAME);
        return -1;
      }
      constr = (*env)->GetMethodID(env, clazz, "<init>", "()V");
      if(!constr) {
        log_err("Failed to get constructor");
	return -1;
      }
      obj = (*env)->NewObject(env, clazz, constr);
      if(!obj) {
        log_err("Failed to create an interface object");
	return -1;
      }
      giface = (*env)->NewGlobalRef(env,obj);

      if((*env)->RegisterNatives(env, clazz, methods, sizeof(methods) / sizeof(methods[0])) < 0) {
        log_err("Registration failed for '%s'", CLASS_NAME);
        return -1;
      }
    
   return JNI_VERSION_1_4;
}
#else
void update_track_time(JNIEnv *env, jobject obj, int time) { }
#endif



