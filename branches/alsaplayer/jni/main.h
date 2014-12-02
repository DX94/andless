#ifndef _MAIN_H_INCLUDED
#define _MAIN_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ANDROID
#ifndef log_info
#if 1
#define log_info(fmt, args...)  __android_log_print(ANDROID_LOG_INFO, "liblossless", "%d [%s] " fmt, gettid(), __func__, ##args)
#else
#define log_info(...)
#endif
#define log_err(fmt, args...)   __android_log_print(ANDROID_LOG_ERROR, "liblossless", "%d [%s] " fmt, gettid(),  __func__, ##args)
#endif
#else
#define log_info(fmt, args...)  printf("liblossless: [%s] " fmt "\n", __func__, ##args)
#define log_err(fmt, args...)  fprintf(stderr, "liblossless: [%s] " fmt "\n", __func__, ##args)
#endif

#ifndef timeradd
# define timeradd(a, b, result)                                               \
  do {                                                                        \
    (result)->tv_sec = (a)->tv_sec + (b)->tv_sec;                             \
    (result)->tv_usec = (a)->tv_usec + (b)->tv_usec;                          \
    if ((result)->tv_usec >= 1000000)                                         \
      {                                                                       \
        ++(result)->tv_sec;                                                   \
        (result)->tv_usec -= 1000000;                                         \
      }                                                                       \
  } while (0)
#endif


typedef int snd_pcm_format_t;

typedef struct pcm_buffer_t pcm_buffer;	/* private struct pcm_buffer_t is defined in buffer.c */

typedef struct _playback_format_t {
    snd_pcm_format_t fmt;
    int mask;
    int phys_bits;
    int strm_bits;
    const char *str;
} playback_format_t;

struct pcm_buffer_t;

typedef struct {
   enum _playback_state_t {
	STATE_STOPPED = 0,	/* init state */
	STATE_PLAYING,
	STATE_PAUSED
   } state;
   int  track_time;			/* set by decoder */
   int  channels, samplerate, bps;	/* set by decoder */
   const playback_format_t *format;	/* set by alsa */
   int  block_min, block_max;		/* set by decoder */
   int  written;			/* set by audio thread */	
   int  periods, period_size;		/* set by alsa */
   pthread_mutex_t mutex, stop_mutex;
   pthread_t audio_thread;
   pthread_cond_t cond_stopped;		/* audio_play() is about to exit */
   int   stopped; 			/* associated variable */
   pthread_cond_t cond_resumed;		/* thread will pause waiting for state change */
   void *alsa_priv;
   void *decoder_priv;
   struct pcm_buffer_t *buff; 
   int  volume;
   int  alsa_error;			/* set on error exit from alsa thread  */
} playback_ctx;

/* main.c */
extern int audio_start(playback_ctx *ctx);
extern int audio_stop(playback_ctx *ctx, int abort);
extern int audio_write(playback_ctx *ctx, void *buff, int size);
extern int check_state(playback_ctx *ctx, const char *func);
extern void update_track_time(JNIEnv *env, jobject obj, int time);
#define FORMAT_WAV	0
#define FORMAT_FLAC	1
#define FORMAT_APE	2
extern jint audio_play(JNIEnv *env, jobject obj, playback_ctx* ctx, jstring jfile, jint format, jint start);
#ifndef ANDROID
extern jint audio_init(JNIEnv *env, jobject obj, playback_ctx *prev_ctx, jint card, jint device);
extern jboolean audio_exit(JNIEnv *env, jobject obj, playback_ctx *ctx);
#endif

/* alsa.c */
extern int alsa_select_device(playback_ctx *ctx, int card, int device);
extern int alsa_start(playback_ctx *ctx);
extern void alsa_stop(playback_ctx *ctx);
extern ssize_t alsa_write(playback_ctx *ctx, size_t count);
extern bool alsa_pause(playback_ctx *ctx);
extern bool alsa_resume(playback_ctx *ctx);
extern bool alsa_set_volume(playback_ctx *ctx, int vol, int force_now);
extern void alsa_close(playback_ctx *ctx);
extern void *alsa_write_thread(void *ctx);

/* buffer.c */
extern pcm_buffer *buffer_create(int size);
extern int buffer_put(pcm_buffer *buff, void *src, int bytes);
extern int buffer_get(pcm_buffer *buff, void *dst, int bytes);
extern void buffer_stop(pcm_buffer *buff, int now);	/* Stop accepting new frames. If now == 1, stop providing new frames as well. */
extern void buffer_destroy(pcm_buffer *buff);

/* flac/main.c */
extern int flac_play(JNIEnv *env, jobject obj, playback_ctx *ctx, jstring jfile, int start);
extern JNIEXPORT jintArray JNICALL extract_flac_cue(JNIEnv *env, jobject obj, jstring jfile);

/* ape/main.c */
extern int ape_play(JNIEnv *env, jobject obj, playback_ctx *ctx, jstring jfile, int start);

#define LIBLOSSLESS_ERR_NOCTX		1
#define LIBLOSSLESS_ERR_INV_PARM	2
#define LIBLOSSLESS_ERR_NOFILE		3
#define LIBLOSSLESS_ERR_FORMAT		4
#define LIBLOSSLESS_ERR_AU_GETCONF 	5	
#define LIBLOSSLESS_ERR_AU_SETCONF	6
#define LIBLOSSLESS_ERR_AU_BUFF		7
#define LIBLOSSLESS_ERR_AU_SETUP	8
#define LIBLOSSLESS_ERR_AU_START	9
#define LIBLOSSLESS_ERR_IO_WRITE 	10	
#define LIBLOSSLESS_ERR_IO_READ		11
#define LIBLOSSLESS_ERR_DECODE		12 
#define LIBLOSSLESS_ERR_OFFSET		13
#define LIBLOSSLESS_ERR_NOMEM		14
#define LIBLOSSLESS_ERR_INIT		15

#ifdef __cplusplus
}
#endif

#endif
