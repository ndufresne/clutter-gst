/*
 * Clutter-GStreamer.
 *
 * GStreamer integration library for Clutter.
 *
 * clutter-gst-video-sink.c - Gstreamer Video Sink that renders to a
 *                            Clutter Texture.
 *
 * Authored by Jonathan Matthew  <jonathan@kaolin.wh9.net>,
 *             Chris Lord        <chris@openedhand.com>
 *             Damien Lespiau    <damien.lespiau@intel.com>
 *
 * Copyright (C) 2007,2008 OpenedHand
 * Copyright (C) 2009,2010 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:clutter-gst-video-sink
 * @short_description: GStreamer video sink
 *
 * #ClutterGstVideoSink is a GStreamer sink element that sends
 * data to a #ClutterTexture.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define COGL_ENABLE_EXPERIMENTAL_API

#include "clutter-gst-video-sink.h"
#include "clutter-gst-private.h"
#include "clutter-gst-shaders.h"
/* include assembly shaders */
#include "I420.h"
#include "YV12.h"

#include <gst/gst.h>
#include <gst/gstvalue.h>
#include <gst/video/video.h>
#include <gst/riff/riff-ids.h>

#include <glib.h>
#include <string.h>
#include <sys/types.h>

/* Flags to give to cogl_texture_new(). Since clutter 1.1.10 put NO_ATLAS to
 * be sure the frames don't end up in an atlas */
#if CLUTTER_CHECK_VERSION(1, 1, 10)
#define CLUTTER_GST_TEXTURE_FLAGS \
  (COGL_TEXTURE_NO_SLICING | COGL_TEXTURE_NO_ATLAS)
#else
#define CLUTTER_GST_TEXTURE_FLAGS  COGL_TEXTURE_NO_SLICING
#endif

static gchar *ayuv_to_rgba_shader = \
     FRAGMENT_SHADER_VARS
     "uniform sampler2D tex;"
     "void main () {"
     "  vec4 color = texture2D (tex, vec2(" TEX_COORD "));"
     "  float y = 1.1640625 * (color.g - 0.0625);"
     "  float v = color.a - 0.5;"
     "  color.a = color.r;"
     "  color.r = y + 1.59765625 * v;"
     "  color.g = y - 0.390625 * u - 0.8125 * v;"
     "  color.b = y + 2.015625 * u;"
     "  gl_FragColor = color;"
     FRAGMENT_SHADER_END
     "}";

static gchar *dummy_shader = \
     FRAGMENT_SHADER_VARS
     "void main () {"
     "}";

static gchar *yv12_to_rgba_shader = \
     FRAGMENT_SHADER_VARS
     "uniform sampler2D ytex;"
     "uniform sampler2D utex;"
     "uniform sampler2D vtex;"
     "void main () {"
     "  vec2 coord = vec2(" TEX_COORD ");"
     "  float y = 1.1640625 * (texture2D (ytex, coord).g - 0.0625);"
     "  float u = texture2D (utex, coord).g - 0.5;"
     "  float v = texture2D (vtex, coord).g - 0.5;"
     "  vec4 color;"
     "  color.r = y + 1.59765625 * v;"
     "  color.g = y - 0.390625 * u - 0.8125 * v;"
     "  color.b = y + 2.015625 * u;"
     "  color.a = 1.0;"
     "  gl_FragColor = color;"
     FRAGMENT_SHADER_END
     "}";

static GstStaticPadTemplate sinktemplate_all 
 = GST_STATIC_PAD_TEMPLATE ("sink",
                            GST_PAD_SINK,
                            GST_PAD_ALWAYS,
                            GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV("AYUV") ";" \
                                             GST_VIDEO_CAPS_YUV("YV12") ";" \
                                             GST_VIDEO_CAPS_YUV("I420") ";" \
                                             GST_VIDEO_CAPS_RGBA        ";" \
                                             GST_VIDEO_CAPS_BGRA        ";" \
                                             GST_VIDEO_CAPS_RGB         ";" \
                                             GST_VIDEO_CAPS_BGR));

GST_DEBUG_CATEGORY_STATIC (clutter_gst_video_sink_debug);
#define GST_CAT_DEFAULT clutter_gst_video_sink_debug

static GstElementDetails clutter_gst_video_sink_details =
  GST_ELEMENT_DETAILS ("Clutter video sink",
      "Sink/Video",
      "Sends video data from a GStreamer pipeline to a Clutter texture",
      "Jonathan Matthew <jonathan@kaolin.wh9.net>, "
      "Matthew Allum <mallum@o-hand.com, "
      "Chris Lord <chris@o-hand.com>");

enum
{
  PROP_0,
  PROP_TEXTURE,
  PROP_UPDATE_PRIORITY
};

typedef enum
{
  CLUTTER_GST_NOFORMAT,
  CLUTTER_GST_RGB32,
  CLUTTER_GST_RGB24,
  CLUTTER_GST_AYUV,
  CLUTTER_GST_YV12,
  CLUTTER_GST_I420,
} ClutterGstVideoFormat;

typedef void (APIENTRYP GLUNIFORM1IPROC)(GLint location, GLint value);
/* GL_ARB_fragment_program */
typedef void (APIENTRYP GLGENPROGRAMSPROC)(GLsizei n, GLuint *programs);
typedef void (APIENTRYP GLBINDPROGRAMPROC)(GLenum target, GLuint program);
typedef void (APIENTRYP GLPROGRAMSTRINGPROC)(GLenum target, GLenum format,
                                             GLsizei len, const void *string);
typedef struct _ClutterGstSymbols
{
  /* GL_ARB_fragment_program */
  GLGENPROGRAMSPROC   glGenProgramsARB;
  GLBINDPROGRAMPROC   glBindProgramARB;
  GLPROGRAMSTRINGPROC glProgramStringARB;
} ClutterGstSymbols;

/*
 * features: what does the underlaying GL implentation support ?
 */
typedef enum _ClutterGstFeatures
{
  CLUTTER_GST_FP             = 0x1, /* fragment programs (ARB fp1.0) */
  CLUTTER_GST_GLSL           = 0x2, /* GLSL */
  CLUTTER_GST_MULTI_TEXTURE  = 0x4, /* multi-texturing */
} ClutterGstFeatures;

/*
 * Custom GstBuffer containing additional information about the CoglBuffer
 * we are using.
 */
typedef struct _ClutterGstBuffer
{
  GstBuffer            buffer;

  ClutterGstVideoSink *sink;            /* a ref to the its sink */
  CoglHandle           pbo;             /* underlying CoglBuffer */
  guint                size;            /* size (in bytes); */
} ClutterGstBuffer;

/*
 * This structure is used to queue the buffer requests from buffer_alloc() to
 * the clutter thread.
 */
typedef struct _ClutterGstBufferRequest
{
  GCond *wait_for_buffer;
  guint size;
  ClutterGstBuffer *buffer;
  guint id;
} ClutterGstBufferRequest;

/*
 * Custom GSource to signal we have a new frame pending
 */

#define CLUTTER_GST_DEFAULT_PRIORITY    (G_PRIORITY_HIGH_IDLE)

typedef struct _ClutterGstSource
{
  GSource              source;

  ClutterGstVideoSink *sink;
  GMutex              *buffer_lock;   /* mutex for the buffer */
  ClutterGstBuffer    *buffer;
} ClutterGstSource;

/*
 * renderer: abstracts a backend to render a frame.
 */
typedef void (ClutterGstRendererPaint) (ClutterActor *, ClutterGstVideoSink *);
typedef void (ClutterGstRendererPostPaint) (ClutterActor *,
                                            ClutterGstVideoSink *);

typedef struct _ClutterGstRenderer
{
 const char            *name;     /* user friendly name */
 ClutterGstVideoFormat  format;   /* the format handled by this renderer */
 int                    flags;    /* ClutterGstFeatures ORed flags */
 GstStaticCaps          caps;     /* caps handled by the renderer */

 void (*init)       (ClutterGstVideoSink *sink);
 void (*deinit)     (ClutterGstVideoSink *sink);
 void (*upload)     (ClutterGstVideoSink *sink,
                     ClutterGstBuffer    *buffer);
} ClutterGstRenderer;

typedef enum _ClutterGstRendererState
{
  CLUTTER_GST_RENDERER_STOPPED,
  CLUTTER_GST_RENDERER_RUNNING,
  CLUTTER_GST_RENDERER_NEED_GC,
} ClutterGstRendererState;

struct _ClutterGstVideoSinkPrivate
{
  ClutterTexture          *texture;
  CoglHandle               u_tex;
  CoglHandle               v_tex;
  CoglHandle               program;
  CoglHandle               shader;
  GLuint                   fp;

  ClutterGstVideoFormat    format;
  GstCaps                 *current_caps;
  gboolean                 bgr;
  int                      width;
  int                      height;
  int                      fps_n, fps_d;
  int                      par_n, par_d;
  
  ClutterGstSymbols        syms;          /* extra OpenGL functions */

  GMainContext            *clutter_main_context;
  ClutterGstSource        *source;

  GMutex                  *pool_lock;
  GSList                  *buffer_pool;         /* available buffers */
  GSList                  *recycle_pool;        /* recyle those buffers at next
                                                   iteration of the GSource */
  GSList                  *purge_pool;          /* delete those buffers at next
                                                   iteration of the GSource */
  gboolean                 pool_in_flush;       /* wether we are handling a
                                                   FLUSH event */
  GQueue                  *buffer_requests;     /* buffers to create at next
                                                   iteration of the GSource */

  GSList                  *renderers;
  GstCaps                 *available_caps;
  ClutterGstRenderer      *renderer;
  ClutterGstRendererState  renderer_state;

  GArray                  *signal_handler_ids;
};


#define _do_init(bla) \
  GST_DEBUG_CATEGORY_INIT (clutter_gst_video_sink_debug, \
                                 "cluttersink", \
                                 0, \
                                 "clutter video sink")

GST_BOILERPLATE_FULL (ClutterGstVideoSink,
                          clutter_gst_video_sink,
                      GstBaseSink,
                      GST_TYPE_BASE_SINK,
                      _do_init);

/*
 * ClutterGstBuffer related functions
 */

#define CLUTTER_GST_TYPE_BUFFER         (clutter_gst_buffer_get_type ())
#define CLUTTER_GST_IS_BUFFER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CLUTTER_GST_TYPE_BUFFER))
#define CLUTTER_GST_BUFFER(obj)                         \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj),                   \
                               CLUTTER_GST_TYPE_BUFFER, \
                               ClutterGstBuffer))

static GstBufferClass *clutter_gst_buffer_parent_class = NULL;
static GType clutter_gst_buffer_get_type (void);

static void
clutter_gst_buffer_finalize (GstMiniObject *obj)
{
  ClutterGstBuffer *buffer = (ClutterGstBuffer *) obj;
  ClutterGstVideoSinkPrivate *priv = buffer->sink->priv;
  GstMiniObjectClass *mini_object_class;

  mini_object_class = GST_MINI_OBJECT_CLASS (clutter_gst_buffer_parent_class);

  if (buffer->size == 0xdeadbeef)
    {
      GST_LOG_OBJECT (buffer->sink, "freeing %p", obj);

      if (buffer->pbo != COGL_INVALID_HANDLE)
        {
          cogl_buffer_unmap (buffer->pbo);
          cogl_handle_unref (buffer->pbo);
          buffer->pbo = COGL_INVALID_HANDLE;
        }

      gst_object_unref (buffer->sink);
      buffer->size = 0;

      GST_BUFFER_DATA (buffer) = NULL;
      GST_BUFFER_SIZE (buffer) = 0;

      mini_object_class->finalize (obj);
    }
  else
    {
      GST_LOG_OBJECT (buffer->sink, "putting %p in the recycle queue", obj);

      /* need to take a ref if we want to recycle this one */
      gst_buffer_ref (GST_BUFFER_CAST (buffer));

      g_mutex_lock (priv->pool_lock);
      priv->recycle_pool = g_slist_prepend (priv->recycle_pool, buffer);
      g_mutex_unlock (priv->pool_lock);
    }
}

static void
clutter_gst_buffer_init (ClutterGstBuffer *buffer,
                         gpointer          g_class)
{
  /* nothing to do here, really, _new() will do everything for us, and one
   * should always create a new ClutterGstBuuffer with _new() */
}

static void
clutter_gst_buffer_class_init (gpointer g_class,
                               gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

  clutter_gst_buffer_parent_class = g_type_class_peek_parent (g_class);

  mini_object_class->finalize = clutter_gst_buffer_finalize;
}

static GType
clutter_gst_buffer_get_type (void)
{
  static GType clutter_gst_buffer_type;

  if (G_UNLIKELY (clutter_gst_buffer_type == 0))
    {
      static const GTypeInfo clutter_gst_buffer_info =
        {
          sizeof (GstBufferClass),
          NULL,
          NULL,
          clutter_gst_buffer_class_init,
          NULL,
          NULL,
          sizeof (ClutterGstBuffer),
          0,
          (GInstanceInitFunc) clutter_gst_buffer_init,
          NULL
        };
      clutter_gst_buffer_type =
        g_type_register_static (GST_TYPE_BUFFER,
                                "ClutterGstBuffer",
                                &clutter_gst_buffer_info,
                                0);
    }

  return clutter_gst_buffer_type;
}

static void
clutter_gst_buffer_destroy (ClutterGstBuffer *buffer)
{
  /* This buffer is literally dead beef, calling _free() on a buffer ensures
   * the buffer is discarded instead of being recycled */
  buffer->size = 0xdeadbeef;
  gst_buffer_unref (GST_BUFFER_CAST (buffer));
}

static ClutterGstBuffer *
clutter_gst_buffer_new (ClutterGstVideoSink *sink,
                        guint                size)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  ClutterGstBuffer *new_buffer;
  guchar *map;

  new_buffer =
    (ClutterGstBuffer *) gst_mini_object_new (CLUTTER_GST_TYPE_BUFFER);

  new_buffer->pbo = cogl_pixel_buffer_new (size);
  if (G_UNLIKELY (new_buffer->pbo == COGL_INVALID_HANDLE))
    goto hell;

  cogl_buffer_set_update_hint (new_buffer->pbo, COGL_BUFFER_UPDATE_HINT_STREAM);

  new_buffer->size = size;
  new_buffer->sink = gst_object_ref (sink);

  map = cogl_buffer_map (new_buffer->pbo, COGL_BUFFER_ACCESS_WRITE);
  if (G_UNLIKELY (map == NULL))
    goto hell;

  GST_BUFFER_DATA (new_buffer) = map;
  GST_BUFFER_SIZE (new_buffer) = size;
  gst_buffer_set_caps (GST_BUFFER_CAST (new_buffer), priv->current_caps);

  return new_buffer;

hell:
  clutter_gst_buffer_destroy (new_buffer);
  return NULL;
}

static void
clutter_gst_video_sink_recycle_buffer (ClutterGstVideoSink *sink,
                                       ClutterGstBuffer    *buffer)
{
  /* we destroy the pbo if it's still there as we don't want to map a pbo
   * while the texture is being used in the scene */
  if (buffer->pbo != COGL_INVALID_HANDLE)
    {
      cogl_buffer_unmap (buffer->pbo);
      cogl_handle_unref (buffer->pbo);
    }

  buffer->pbo = cogl_pixel_buffer_new (buffer->size);
  cogl_buffer_set_update_hint (buffer->pbo, COGL_BUFFER_UPDATE_HINT_STREAM);
  GST_BUFFER_DATA (buffer) = cogl_buffer_map (buffer->pbo,
                                              COGL_BUFFER_ACCESS_WRITE);

  /* make sure the GstBuffer is cleared of any previously used flags */
  GST_MINI_OBJECT_FLAGS (buffer) = 0;
}

/*
 * ClutterGstSource implementation
 */

static GSourceFuncs gst_source_funcs;

static ClutterGstSource *
clutter_gst_source_new (ClutterGstVideoSink *sink)
{
  GSource *source;
  ClutterGstSource *gst_source;

  source = g_source_new (&gst_source_funcs, sizeof (ClutterGstSource));
  gst_source = (ClutterGstSource *) source;

  g_source_set_can_recurse (source, TRUE);
  g_source_set_priority (source, CLUTTER_GST_DEFAULT_PRIORITY);

  gst_source->sink = sink;
  gst_source->buffer_lock = g_mutex_new ();
  gst_source->buffer = NULL;

  return gst_source;
}

static void
clutter_gst_source_finalize (GSource *source)
{
  ClutterGstSource *gst_source = (ClutterGstSource *) source;

  g_mutex_lock (gst_source->buffer_lock);
  if (gst_source->buffer)
    clutter_gst_buffer_destroy (gst_source->buffer);
  gst_source->buffer = NULL;
  g_mutex_unlock (gst_source->buffer_lock);
  g_mutex_free (gst_source->buffer_lock);
}

static void
clutter_gst_source_push (ClutterGstSource *gst_source,
                         ClutterGstBuffer *buffer)
{
  ClutterGstVideoSinkPrivate *priv = gst_source->sink->priv;

  g_mutex_lock (gst_source->buffer_lock);

  fprintf (stderr, "ppp pushing %p\n", buffer);
  if (buffer)
    {
      /* if we already have a buffer pending, recycle it, (unref it) */
      if (gst_source->buffer)
        {
          fprintf (stderr, "ppp %p has been pushed, discarding the old buffer "
                   "%p\n", buffer, gst_source->buffer);
          gst_buffer_unref (GST_BUFFER_CAST (gst_source->buffer));
        }
      gst_source->buffer =
        CLUTTER_GST_BUFFER (gst_buffer_ref (GST_BUFFER_CAST (buffer)));
    }

  g_mutex_unlock (gst_source->buffer_lock);

  g_main_context_wakeup (priv->clutter_main_context);
}

static gboolean
clutter_gst_source_prepare (GSource *source,
                            gint    *timeout)
{
  ClutterGstSource *gst_source = (ClutterGstSource *) source;

  *timeout = -1;

  return gst_source->buffer != NULL ||
         !g_queue_is_empty (gst_source->sink->priv->buffer_requests);
}

static gboolean
clutter_gst_source_check (GSource *source)
{
  ClutterGstSource *gst_source = (ClutterGstSource *) source;

  return gst_source->buffer != NULL ||
         !g_queue_is_empty (gst_source->sink->priv->buffer_requests);
}

static gboolean
clutter_gst_source_dispatch (GSource     *source,
                             GSourceFunc  callback,
                             gpointer     user_data)
{
  ClutterGstSource *gst_source = (ClutterGstSource *) source;
  ClutterGstVideoSinkPrivate *priv = gst_source->sink->priv;
  ClutterGstBuffer *buffer;

  fprintf (stderr, "=== dispatch start\n");

  /* The initialization / free functions of the renderers have to be called in
   * the clutter thread (OpenGL context) */
  if (G_UNLIKELY (priv->renderer_state == CLUTTER_GST_RENDERER_NEED_GC))
    {
      priv->renderer->deinit (gst_source->sink);
      priv->renderer_state = CLUTTER_GST_RENDERER_STOPPED;
    }
  if (G_UNLIKELY (priv->renderer_state == CLUTTER_GST_RENDERER_STOPPED))
    {
      priv->renderer->init (gst_source->sink);
      priv->renderer_state = CLUTTER_GST_RENDERER_RUNNING;
    }

  /* Push a buffer if we have one. Note that the ClutterGstSource can be waken
   * up to do buffer management too, so we might not have a buffer */
  g_mutex_lock (gst_source->buffer_lock);
  buffer = gst_source->buffer;
  gst_source->buffer = NULL;
  g_mutex_unlock (gst_source->buffer_lock);

  if (buffer)
    {
      if (!CLUTTER_GST_IS_BUFFER (buffer))
        {
          fprintf (stderr, "=== %p is not a ClutterGstBuffer\n", buffer);
          GST_WARNING_OBJECT (gst_source->sink, "%p not our buffer, "
                              "fuck off", buffer);
          goto memory_management;
        }

      fprintf (stderr, "=== upload %p\n", buffer);
      priv->renderer->upload (gst_source->sink, buffer);
      clutter_gst_video_sink_recycle_buffer (gst_source->sink, buffer);

      /* add the recycled buffer to the pool */
      g_mutex_lock (priv->pool_lock);
      priv->buffer_pool = g_slist_prepend (priv->buffer_pool, buffer);
      fprintf (stderr, "=== recycle %p, buffer_pool (len=%d)\n",
               buffer,
               g_slist_length (priv->buffer_pool));
      g_mutex_unlock (priv->pool_lock);
    }

  /*
   * memory pool management
   */
memory_management:
  g_mutex_lock (priv->pool_lock);

  /* purge buffers that do not have the required caps any more */
  while (G_UNLIKELY (priv->purge_pool))
    {
      ClutterGstBuffer *purge_me;

      purge_me = (ClutterGstBuffer *) priv->purge_pool->data;
      priv->purge_pool = g_slist_delete_link (priv->purge_pool,
                                              priv->purge_pool);

      fprintf (stderr, "=== purge %p\n", purge_me);
      clutter_gst_buffer_destroy (purge_me);
    }

  while (priv->recycle_pool)
    {
      ClutterGstBuffer *recycle_me;

      recycle_me = (ClutterGstBuffer *) priv->recycle_pool->data;
      priv->recycle_pool = g_slist_delete_link (priv->recycle_pool,
                                                priv->recycle_pool);

      clutter_gst_video_sink_recycle_buffer (gst_source->sink, recycle_me);

      /* add the recycled buffer to the buffer pool */
      priv->buffer_pool = g_slist_prepend (priv->buffer_pool, recycle_me);
      fprintf (stderr, "=== recycle %p, buffer_pool (len=%d)\n",
               recycle_me,
               g_slist_length (priv->buffer_pool));
    }

  /* it's time to answer the requests from buffer_alloc () */
  while (!g_queue_is_empty (priv->buffer_requests))
    {
      ClutterGstBufferRequest *request;
      ClutterGstBuffer *new_buffer;

      request = g_queue_pop_head (priv->buffer_requests);
      new_buffer = NULL;

      /* try do find a suitable buffer in buffer_pool */
      while (priv->buffer_pool)
        {
          new_buffer = (ClutterGstBuffer *) priv->buffer_pool->data;

          fprintf (stderr, "=== removing %p from the pool\n", new_buffer);

          priv->buffer_pool = g_slist_delete_link (priv->buffer_pool,
                                                   priv->buffer_pool);

          if (request->size == GST_BUFFER_SIZE (new_buffer))
            {
              /* found it! */
              break;
            }
          else
            {
              /* it was a free buffer from a time when different sized buffers
               * were needed */
              clutter_gst_buffer_destroy (new_buffer);
              new_buffer = NULL;
            }
        }

      /* could not find a suitable buffer, create a new one */
      if (new_buffer == NULL)
          new_buffer = clutter_gst_buffer_new (gst_source->sink, request->size);

      request->buffer = new_buffer;

      fprintf (stderr, "=== (req%d) answering request with %p\n", request->id,
               request->buffer);
      g_cond_signal (request->wait_for_buffer);
    }

  g_mutex_unlock (priv->pool_lock);

  fprintf (stderr, "=== dispatch end\n");

  return TRUE;
}

static GSourceFuncs gst_source_funcs = {
  clutter_gst_source_prepare,
  clutter_gst_source_check,
  clutter_gst_source_dispatch,
  clutter_gst_source_finalize
};

static void
clutter_gst_video_sink_set_priority (ClutterGstVideoSink *sink,
                                     int                  priority)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;

  GST_INFO ("GSource priority: %d", priority);

  g_source_set_priority ((GSource *) priv->source, priority);
}

/*
 * ClutterGstBufferRequest
 */

static ClutterGstBufferRequest *
clutter_gst_buffer_request_new (guint size)
{
  ClutterGstBufferRequest *request;
  static guint i = 0;

  request = g_slice_new (ClutterGstBufferRequest);
  request->wait_for_buffer = g_cond_new ();
  request->buffer = NULL;
  request->size = size;
  request->id = i++;

  return request;
}

static void
clutter_gst_buffer_request_free (ClutterGstBufferRequest *request)
{
  g_cond_free (request->wait_for_buffer);
  g_slice_free (ClutterGstBufferRequest, request);
}

/* to be called while holding priv->pool_lock */
static void
clutter_gst_video_sink_queue_buffer_request (ClutterGstVideoSink     *sink,
                                             ClutterGstBufferRequest *request)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;

  /* queue the request and wake the main thread up */
  g_queue_push_tail (priv->buffer_requests, request);
  clutter_gst_source_push (priv->source, NULL);
}

/*
 * Small helpers
 */

static void
_string_array_to_char_array (char	*dst,
                             const char *src[])
{
  int i, n;

  for (i = 0; src[i]; i++) {
      n = strlen (src[i]);
      memcpy (dst, src[i], n);
      dst += n;
  }
  *dst = '\0';
}

static void
_renderer_connect_signals(ClutterGstVideoSink        *sink,
                          ClutterGstRendererPaint     paint_func,
                          ClutterGstRendererPostPaint post_paint_func)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  gulong handler_id;

  handler_id =
    g_signal_connect (priv->texture, "paint", G_CALLBACK (paint_func), sink);
  g_array_append_val (priv->signal_handler_ids, handler_id);

  handler_id = g_signal_connect_after (priv->texture,
                                       "paint",
                                       G_CALLBACK (post_paint_func),
                                       sink);
  g_array_append_val (priv->signal_handler_ids, handler_id);
}

#ifdef CLUTTER_COGL_HAS_GL
static void
clutter_gst_video_sink_set_fp_shader (ClutterGstVideoSink *sink,
                                      const gchar         *shader_src,
                                      const int            size)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;

  /* FIXME: implement freeing the shader */
  if (!shader_src)
    return;

  priv->syms.glGenProgramsARB (1, &priv->fp);
  priv->syms.glBindProgramARB (GL_FRAGMENT_PROGRAM_ARB, priv->fp);
  priv->syms.glProgramStringARB (GL_FRAGMENT_PROGRAM_ARB,
                                  GL_PROGRAM_FORMAT_ASCII_ARB,
                                  size,
                                  (const GLbyte *)shader_src);
}
#endif

static void
clutter_gst_video_sink_set_glsl_shader (ClutterGstVideoSink *sink,
                                        const gchar         *shader_src)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  
  if (priv->texture)
    clutter_actor_set_shader (CLUTTER_ACTOR (priv->texture), NULL);

  if (priv->program)
    {
      cogl_program_unref (priv->program);
      priv->program = NULL;
    }
  
  if (priv->shader)
    {
      cogl_handle_unref (priv->shader);
      priv->shader = NULL;
    }
  
  if (shader_src)
    {
      ClutterShader *shader;

      /* Set a dummy shader so we don't interfere with the shader stack */
      shader = clutter_shader_new ();
      clutter_shader_set_fragment_source (shader, dummy_shader, -1);
      clutter_actor_set_shader (CLUTTER_ACTOR (priv->texture), shader);
      g_object_unref (shader);

      /* Create shader through COGL - necessary as we need to be able to set
       * integer uniform variables for multi-texturing.
       */
      priv->shader = cogl_create_shader (COGL_SHADER_TYPE_FRAGMENT);
      cogl_shader_source (priv->shader, shader_src);
      cogl_shader_compile (priv->shader);
      
      priv->program = cogl_create_program ();
      cogl_program_attach_shader (priv->program, priv->shader);
      cogl_program_link (priv->program);
    }
}

/* some renderers don't need all the ClutterGstRenderer vtable */
static void
clutter_gst_dummy_init (ClutterGstVideoSink *sink)
{
}

static void
clutter_gst_dummy_deinit (ClutterGstVideoSink *sink)
{
}

/*
 * RGB 24 / BGR 24
 *
 * 3 bytes per pixel, stride % 4 = 0.
 */

static void
clutter_gst_rgb24_upload (ClutterGstVideoSink *sink,
                          ClutterGstBuffer    *buffer)
{
#if 0
  ClutterGstVideoSinkPrivate *priv= sink->priv;
  CoglPixelFormat format;
  CoglHandle tex;

  cogl_buffer_unmap (buffer->pbo);
  GST_BUFFER_DATA (buffer) = NULL;

  format = priv->bgr ? COGL_PIXEL_FORMAT_BGR_888 : COGL_PIXEL_FORMAT_RGB_888;

  tex = cogl_texture_new_from_buffer (buffer->pbo,
                                      priv->width,
                                      priv->height,
                                      COGL_TEXTURE_NO_SLICING,
                                      format,
                                      format,
                                      GST_ROUND_UP_4 (3 * priv->width),
                                      0);

  clutter_texture_set_cogl_texture (priv->texture, tex);
  cogl_handle_unref (tex);
#endif
}

static ClutterGstRenderer rgb24_renderer =
{
  "RGB 24",
  CLUTTER_GST_RGB24,
  0,
  GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB ";" GST_VIDEO_CAPS_BGR),
  clutter_gst_dummy_init,
  clutter_gst_dummy_deinit,
  clutter_gst_rgb24_upload,
};

/*
 * RGBA / BGRA 8888
 */

static void
clutter_gst_rgb32_upload (ClutterGstVideoSink *sink,
                          ClutterGstBuffer    *buffer)
{
  ClutterGstVideoSinkPrivate *priv= sink->priv;
  CoglPixelFormat format;
  CoglHandle tex;

  cogl_buffer_unmap (buffer->pbo);
  GST_BUFFER_DATA (buffer) = NULL;

  format = priv->bgr ? COGL_PIXEL_FORMAT_BGRA_8888 :
                       COGL_PIXEL_FORMAT_RGBA_8888;

  tex = cogl_texture_new_from_buffer (buffer->pbo,
                                      priv->width,
                                      priv->height,
                                      COGL_TEXTURE_NO_SLICING,
                                      format,
                                      format,
                                      priv->width,
                                      0);

  clutter_texture_set_cogl_texture (priv->texture, tex);
  cogl_handle_unref (tex);
}

static ClutterGstRenderer rgb32_renderer =
{
  "RGB 32",
  CLUTTER_GST_RGB32,
  0,
  GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBA ";" GST_VIDEO_CAPS_BGRA),
  clutter_gst_dummy_init,
  clutter_gst_dummy_deinit,
  clutter_gst_rgb32_upload,
};

/*
 * YV12
 *
 * 8 bit Y plane followed by 8 bit 2x2 subsampled V and U planes.
 */

static void
clutter_gst_yv12_upload (ClutterGstVideoSink *sink,
                         ClutterGstBuffer    *buffer)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  gint y_row_stride  = GST_ROUND_UP_4 (priv->width);
  gint uv_row_stride = GST_ROUND_UP_4 (priv->width / 2);

  cogl_buffer_unmap (buffer->pbo);
  GST_BUFFER_DATA (buffer) = NULL;

  CoglHandle y_tex = cogl_texture_new_from_buffer (buffer->pbo,
                                                   priv->width,
                                                   priv->height,
                                                   COGL_TEXTURE_NO_SLICING |
                                                   COGL_TEXTURE_NO_AUTO_MIPMAP, 
                                                   COGL_PIXEL_FORMAT_G_8,
                                                   COGL_PIXEL_FORMAT_G_8,
                                                   y_row_stride,
                                                   0);

  clutter_texture_set_cogl_texture (priv->texture, y_tex);
  cogl_handle_unref (y_tex);

  if (priv->u_tex)
    cogl_handle_unref (priv->u_tex);

  if (priv->v_tex)
    cogl_handle_unref (priv->v_tex);

  priv->v_tex = cogl_texture_new_from_buffer (buffer->pbo,
                                              priv->width / 2,
                                              priv->height / 2,
                                              COGL_TEXTURE_NO_SLICING,
                                              COGL_PIXEL_FORMAT_G_8,
                                              COGL_PIXEL_FORMAT_G_8,
                                              uv_row_stride,
                                              (y_row_stride * priv->height));

  priv->u_tex = 
    cogl_texture_new_from_buffer (buffer->pbo,
                                  priv->width / 2,
                                  priv->height / 2,
                                  COGL_TEXTURE_NO_SLICING,
                                  COGL_PIXEL_FORMAT_G_8,
                                  COGL_PIXEL_FORMAT_G_8,
                                  uv_row_stride,
                                  (y_row_stride * priv->height) +
                                  (uv_row_stride * priv->height / 2));
}

static void
clutter_gst_yv12_glsl_paint (ClutterActor        *actor,
                             ClutterGstVideoSink *sink)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  CoglHandle material;

  material = clutter_texture_get_cogl_material (CLUTTER_TEXTURE (actor));

  /* bind the shader */
  cogl_program_use (priv->program);

  /* Bind the U and V textures in layers 1 and 2 */
  if (priv->u_tex)
    cogl_material_set_layer (material, 1, priv->u_tex);
  if (priv->v_tex)
    cogl_material_set_layer (material, 2, priv->v_tex);
}

static void
clutter_gst_yv12_glsl_post_paint (ClutterActor        *actor,
                                  ClutterGstVideoSink *sink)
{
  CoglHandle material;

  /* Remove the extra layers */
  material = clutter_texture_get_cogl_material (CLUTTER_TEXTURE (actor));
  cogl_material_remove_layer (material, 1);
  cogl_material_remove_layer (material, 2);

  /* disable the shader */
  cogl_program_use (COGL_INVALID_HANDLE);
}

static void
clutter_gst_yv12_glsl_init (ClutterGstVideoSink *sink)
{
  ClutterGstVideoSinkPrivate *priv= sink->priv;
  GLint location;

  clutter_gst_video_sink_set_glsl_shader (sink, yv12_to_rgba_shader);

  cogl_program_use (priv->program);
  location = cogl_program_get_uniform_location (priv->program, "ytex");
  cogl_program_uniform_1i (location, 0);
  location = cogl_program_get_uniform_location (priv->program, "utex");
  cogl_program_uniform_1i (location, 1);
  location = cogl_program_get_uniform_location (priv->program, "vtex");
  cogl_program_uniform_1i (location, 2);

  cogl_program_use (COGL_INVALID_HANDLE);

  _renderer_connect_signals (sink,
                             clutter_gst_yv12_glsl_paint,
                             clutter_gst_yv12_glsl_post_paint);
}

static void
clutter_gst_yv12_glsl_deinit (ClutterGstVideoSink *sink)
{
  clutter_gst_video_sink_set_glsl_shader (sink, NULL);
}


static ClutterGstRenderer yv12_glsl_renderer =
{
  "YV12 glsl",
  CLUTTER_GST_YV12,
  CLUTTER_GST_GLSL | CLUTTER_GST_MULTI_TEXTURE,
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("YV12")),
  clutter_gst_yv12_glsl_init,
  clutter_gst_yv12_glsl_deinit,
  clutter_gst_yv12_upload,
};

/*
 * YV12 (fragment program version)
 *
 * 8 bit Y plane followed by 8 bit 2x2 subsampled V and U planes.
 */

#ifdef CLUTTER_COGL_HAS_GL
static void
clutter_gst_yv12_fp_paint (ClutterActor        *actor,
                             ClutterGstVideoSink *sink)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  CoglHandle material;

  material = clutter_texture_get_cogl_material (CLUTTER_TEXTURE (actor));

  /* Bind the U and V textures in layers 1 and 2 */
  if (priv->u_tex)
    cogl_material_set_layer (material, 1, priv->u_tex);
  if (priv->v_tex)
    cogl_material_set_layer (material, 2, priv->v_tex);

  /* Cogl doesn't support changing OpenGL state to modify how Cogl primitives
   * work, but it also doesn't support ARBfp which we currently depend on. For
   * now we at least ask Cogl to flush any batched primitives so we avoid
   * binding our shader across the wrong geometry, but there is a risk that
   * Cogl may start to use ARBfp internally which will conflict with us. */
  cogl_flush ();

  /* bind the shader */
  glEnable (GL_FRAGMENT_PROGRAM_ARB);
  priv->syms.glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, priv->fp);

}

static void
clutter_gst_yv12_fp_post_paint (ClutterActor        *actor,
                                ClutterGstVideoSink *sink)
{
  CoglHandle material;

  /* Cogl doesn't support changing OpenGL state to modify how Cogl primitives
   * work, but it also doesn't support ARBfp which we currently depend on. For
   * now we at least ask Cogl to flush any batched primitives so we avoid
   * binding our shader across the wrong geometry, but there is a risk that
   * Cogl may start to use ARBfp internally which will conflict with us. */
  cogl_flush ();

  /* Remove the extra layers */
  material = clutter_texture_get_cogl_material (CLUTTER_TEXTURE (actor));
  cogl_material_remove_layer (material, 1);
  cogl_material_remove_layer (material, 2);

  /* Disable the shader */
  glDisable (GL_FRAGMENT_PROGRAM_ARB);
}

static void
clutter_gst_yv12_fp_init (ClutterGstVideoSink *sink)
{
  gchar *shader;

  shader = g_malloc(YV12_FP_SZ + 1);
  _string_array_to_char_array (shader, YV12_fp);

  /* the size given to glProgramStringARB is without the trailing '\0', which
   * is precisely YV12_FP_SZ */
  clutter_gst_video_sink_set_fp_shader (sink, shader, YV12_FP_SZ);
  g_free(shader);

  _renderer_connect_signals (sink,
                             clutter_gst_yv12_fp_paint,
                             clutter_gst_yv12_fp_post_paint);
}

static void
clutter_gst_yv12_fp_deinit (ClutterGstVideoSink *sink)
{
  clutter_gst_video_sink_set_fp_shader (sink, NULL, 0);
}

static ClutterGstRenderer yv12_fp_renderer =
{
  "YV12 fp",
  CLUTTER_GST_YV12,
  CLUTTER_GST_FP | CLUTTER_GST_MULTI_TEXTURE,
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("YV12")),
  clutter_gst_yv12_fp_init,
  clutter_gst_yv12_fp_deinit,
  clutter_gst_yv12_upload,
};
#endif

/*
 * I420
 *
 * 8 bit Y plane followed by 8 bit 2x2 subsampled U and V planes.
 * Basically the same as YV12, but with the 2 chroma planes switched.
 */

static void
clutter_gst_i420_glsl_init (ClutterGstVideoSink *sink)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  GLint location;

  clutter_gst_video_sink_set_glsl_shader (sink, yv12_to_rgba_shader);

  cogl_program_use (priv->program);
  location = cogl_program_get_uniform_location (priv->program, "ytex");
  cogl_program_uniform_1i (location, 0);
  location = cogl_program_get_uniform_location (priv->program, "vtex");
  cogl_program_uniform_1i (location, 1);
  location = cogl_program_get_uniform_location (priv->program, "utex");
  cogl_program_uniform_1i (location, 2);
  cogl_program_use (COGL_INVALID_HANDLE);

  _renderer_connect_signals (sink,
                             clutter_gst_yv12_glsl_paint,
                             clutter_gst_yv12_glsl_post_paint);
}

static ClutterGstRenderer i420_glsl_renderer =
{
  "I420 glsl",
  CLUTTER_GST_I420,
  CLUTTER_GST_GLSL | CLUTTER_GST_MULTI_TEXTURE,
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420")),
  clutter_gst_i420_glsl_init,
  clutter_gst_yv12_glsl_deinit,
  clutter_gst_yv12_upload,
};

/*
 * I420 (fragment program version)
 *
 * 8 bit Y plane followed by 8 bit 2x2 subsampled U and V planes.
 * Basically the same as YV12, but with the 2 chroma planes switched.
 */

#ifdef CLUTTER_COGL_HAS_GL
static void
clutter_gst_i420_fp_init (ClutterGstVideoSink *sink)
{
  gchar *shader;

  shader = g_malloc(I420_FP_SZ + 1);
  _string_array_to_char_array (shader, I420_fp);

  /* the size given to glProgramStringARB is without the trailing '\0', which
   * is precisely I420_FP_SZ */
  clutter_gst_video_sink_set_fp_shader (sink, shader, I420_FP_SZ);
  g_free(shader);

  _renderer_connect_signals (sink,
                             clutter_gst_yv12_fp_paint,
                             clutter_gst_yv12_fp_post_paint);
}

static ClutterGstRenderer i420_fp_renderer =
{
  "I420 fp",
  CLUTTER_GST_I420,
  CLUTTER_GST_FP | CLUTTER_GST_MULTI_TEXTURE,
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420")),
  clutter_gst_i420_fp_init,
  clutter_gst_yv12_fp_deinit,
  clutter_gst_yv12_upload,
};
#endif

/*
 * AYUV
 *
 * This is a 4:4:4 YUV format with 8 bit samples for each component along
 * with an 8 bit alpha blend value per pixel. Component ordering is A Y U V
 * (as the name suggests).
 */

static void
clutter_gst_ayuv_glsl_init(ClutterGstVideoSink *sink)
{
  clutter_gst_video_sink_set_glsl_shader (sink, ayuv_to_rgba_shader);
}

static void
clutter_gst_ayuv_glsl_deinit(ClutterGstVideoSink *sink)
{
  clutter_gst_video_sink_set_glsl_shader (sink, NULL);
}

static void
clutter_gst_ayuv_upload (ClutterGstVideoSink *sink,
                         ClutterGstBuffer    *buffer)
{
  ClutterGstVideoSinkPrivate *priv= sink->priv;

  cogl_buffer_unmap (buffer->pbo);
  GST_BUFFER_DATA (buffer) = NULL;

  clutter_texture_set_from_rgb_data (priv->texture,
                                     GST_BUFFER_DATA (buffer),
                                     TRUE,
                                     priv->width,
                                     priv->height,
                                     GST_ROUND_UP_4 (4 * priv->width),
                                     4,
                                     0,
                                     NULL);
}

static ClutterGstRenderer ayuv_glsl_renderer =
{
  "AYUV glsl",
  CLUTTER_GST_AYUV,
  CLUTTER_GST_GLSL,
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("AYUV")),
  clutter_gst_ayuv_glsl_init,
  clutter_gst_ayuv_glsl_deinit,
  clutter_gst_ayuv_upload,
};

static GSList *
clutter_gst_build_renderers_list (ClutterGstSymbols *syms)
{
  GSList             *list = NULL;
  const gchar        *gl_extensions;
  GLint               nb_texture_units = 0;
  gint                features = 0, i;
  /* The order of the list of renderers is important. They will be prepended
   * to a GSList and we'll iterate over that list to choose the first matching
   * renderer. Thus if you want to use the fp renderer over the glsl one, the
   * fp renderer has to be put after the glsl one in this array */
  ClutterGstRenderer *renderers[] =
    {
      &rgb24_renderer,
      &rgb32_renderer,
      &yv12_glsl_renderer,
      &i420_glsl_renderer,
#ifdef CLUTTER_COGL_HAS_GL
      &yv12_fp_renderer,
      &i420_fp_renderer,
#endif
      &ayuv_glsl_renderer,
      NULL
    };

  /* get the features */
  gl_extensions = (const gchar*) glGetString (GL_EXTENSIONS);

  glGetIntegerv (GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &nb_texture_units);

  if (nb_texture_units >= 3)
    features |= CLUTTER_GST_MULTI_TEXTURE;

#ifdef CLUTTER_COGL_HAS_GL
  if (strstr (gl_extensions, "GL_ARB_fragment_program") != NULL)
    {
      /* the shaders we'll feed to the GPU are simple enough, we don't need
       * to check GL limits for GL_FRAGMENT_PROGRAM_ARB */

      syms->glGenProgramsARB = (GLGENPROGRAMSPROC)
        cogl_get_proc_address ("glGenProgramsARB");
      syms->glBindProgramARB = (GLBINDPROGRAMPROC)
        cogl_get_proc_address ("glBindProgramARB");
      syms->glProgramStringARB = (GLPROGRAMSTRINGPROC)
        cogl_get_proc_address ("glProgramStringARB");

      if (syms->glGenProgramsARB &&
          syms->glBindProgramARB &&
          syms->glProgramStringARB)
        {
          features |= CLUTTER_GST_FP;
        }
    }
#endif

  if (cogl_features_available (COGL_FEATURE_SHADERS_GLSL))
    features |= CLUTTER_GST_GLSL;

  GST_INFO ("GL features: 0x%08x", features);

  for (i = 0; renderers[i]; i++)
    {
      gint needed = renderers[i]->flags;

      if ((needed & features) == needed)
        list = g_slist_prepend (list, renderers[i]);
    }

  return list;
}

static void
append_cap (gpointer data, gpointer user_data)
{
  ClutterGstRenderer *renderer = (ClutterGstRenderer *)data;
  GstCaps *caps = (GstCaps *)user_data;
  GstCaps *writable_caps;

  writable_caps =
    gst_caps_make_writable (gst_static_caps_get (&renderer->caps));
  gst_caps_append (caps, writable_caps);
}

static GstCaps *
clutter_gst_build_caps (GSList *renderers)
{
  GstCaps *caps;

  caps = gst_caps_new_empty ();

  g_slist_foreach (renderers, append_cap, caps);

  return caps;
}

static ClutterGstRenderer *
clutter_gst_find_renderer_by_format (ClutterGstVideoSink  *sink,
                                     ClutterGstVideoFormat format)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  ClutterGstRenderer *renderer = NULL;
  GSList *element;

  for (element = priv->renderers; element; element = g_slist_next(element))
    {
      ClutterGstRenderer *candidate = (ClutterGstRenderer *)element->data;

      if (candidate->format == format)
        {
          renderer = candidate;
          break;
        }
    }

  return renderer;
}

static void
clutter_gst_video_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template
                     (element_class,
                      gst_static_pad_template_get (&sinktemplate_all));

  gst_element_class_set_details (element_class,
                                 &clutter_gst_video_sink_details);
}

static void
clutter_gst_video_sink_init (ClutterGstVideoSink      *sink,
                             ClutterGstVideoSinkClass *klass)
{
  ClutterGstVideoSinkPrivate *priv;

  sink->priv = priv =
    G_TYPE_INSTANCE_GET_PRIVATE (sink, CLUTTER_GST_TYPE_VIDEO_SINK,
                                 ClutterGstVideoSinkPrivate);

  /* We are saving the GMainContext of the caller thread (which has to be
   * the clutter thread)  */
  priv->clutter_main_context = g_main_context_default ();
  priv->source = clutter_gst_source_new (sink);
  g_source_attach ((GSource *) priv->source, priv->clutter_main_context);

  /* buffer pool */
  priv->buffer_requests = g_queue_new ();
  priv->pool_lock = g_mutex_new ();

  priv->renderers = clutter_gst_build_renderers_list (&priv->syms);
  priv->available_caps = clutter_gst_build_caps (priv->renderers);
  priv->renderer_state = CLUTTER_GST_RENDERER_STOPPED;

  priv->signal_handler_ids = g_array_new (FALSE, FALSE, sizeof (gulong));
}

static gboolean
clutter_gst_video_sink_find_renderer (ClutterGstVideoSink *sink,
                                      GstCaps             *caps)
{
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  GstStructure *structure;
  guint32 fourcc;
  int red_mask, blue_mask;
  gboolean ret;

  structure = gst_caps_get_structure (caps, 0);

  ret = gst_structure_get_fourcc (structure, "format", &fourcc);
  if (ret && (fourcc == GST_MAKE_FOURCC ('Y', 'V', '1', '2')))
    {
      priv->format = CLUTTER_GST_YV12;
    }
  else if (ret && (fourcc == GST_MAKE_FOURCC ('I', '4', '2', '0')))
    {
      priv->format = CLUTTER_GST_I420;
    }
  else if (ret && (fourcc == GST_MAKE_FOURCC ('A', 'Y', 'U', 'V')))
    {
      priv->format = CLUTTER_GST_AYUV;
      priv->bgr = FALSE;
    }
  else
    {
      guint32 mask;
      gst_structure_get_int (structure, "red_mask", &red_mask);
      gst_structure_get_int (structure, "blue_mask", &blue_mask);

      mask = red_mask | blue_mask;
      if (mask < 0x1000000)
        {
          priv->format = CLUTTER_GST_RGB24;
          priv->bgr = (red_mask == 0xff0000) ? FALSE : TRUE;
        }
      else
        {
          priv->format = CLUTTER_GST_RGB32;
          priv->bgr = (red_mask == 0xff000000) ? FALSE : TRUE;
        }
    }

  /* find a renderer that can display our format */
  priv->renderer = clutter_gst_find_renderer_by_format (sink, priv->format);
  if (G_UNLIKELY (priv->renderer == NULL))
    {
      GST_ERROR_OBJECT (sink, "could not find a suitable renderer");
      return FALSE;
    }

  GST_INFO_OBJECT (sink, "using the %s renderer", priv->renderer->name);

  return TRUE;
}

/*
 * Buffer management
 *
 * The buffer_alloc vfunc returns a new buffer with given caps. The caps we
 * receive should be the one set by set_caps() a bit earlier.
 *
 * When GStreamer requests a new buffer from us, we start by looking for a free
 * buffer in the buffer pool (which holds mapped CoglBuffers) and hand one if
 * found. If we can't find a free buffer, we have to signal that to the main
 * thread that will create a CoglBuffer, map it and insert it into the pool.
 * The synchronisation to wait for a new buffer from the main thread is done
 * by a GCond.
 *
 * We never try to do reverse negotiation as we can deal with all the sizes we
 * advertise in the caps (GL does the work for us)
 */
static gint _i = -1;
static GstFlowReturn
clutter_gst_video_sink_buffer_alloc (GstBaseSink  *bsink,
                                     guint64       offset,
                                     guint         size,
                                     GstCaps      *caps,
                                     GstBuffer   **buf)
{
  ClutterGstVideoSink *sink = CLUTTER_GST_VIDEO_SINK (bsink);
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  GstCaps *intersection;
  ClutterGstBuffer *new_buffer = NULL;
  gint i = ++_i;

  fprintf (stderr, "*** (%d) need buffer from thread %p\n", i,
           g_thread_self ());

  /* start by validating the caps against what we are currently doing */
  if (G_UNLIKELY (priv->current_caps == NULL ||
                  !gst_caps_is_equal (priv->current_caps, caps)))
    {
      intersection = gst_caps_intersect (priv->available_caps, caps);

      /* fixate (ensure we have fixed requirements, not intervals) */
      gst_caps_truncate (intersection);

      if (gst_caps_is_empty (intersection))
        {
          /* we don't do reverse nego */
          gst_caps_unref (intersection);
          goto incompatible_caps;
        }

      /* buffers use priv->current_caps as their caps. Let's try to make the
       * next gst_caps_is_equal() return TRUE after a pointer comparison
       * instead of expensive code */
      if (gst_caps_is_equal (intersection, caps))
        {
          GST_INFO_OBJECT (sink, "replacing our caps pointer by the one given"
                           " by upstream");
          gst_caps_replace (&priv->current_caps, caps);
        }
      else
        gst_caps_replace (&priv->current_caps, intersection);

      gst_caps_unref (intersection);

      GST_INFO_OBJECT (sink, "using %" GST_PTR_FORMAT " for the caps of our "
                       "buffers", priv->current_caps);

      if (!clutter_gst_video_sink_find_renderer (sink, priv->current_caps))
          goto incompatible_caps;
    }

  /* look for a free buffer int the pool */
  g_mutex_lock (priv->pool_lock);
  while (priv->buffer_pool)
    {
      /* remove from the pool */
      new_buffer = (ClutterGstBuffer *) priv->buffer_pool->data;
      priv->buffer_pool = g_slist_delete_link (priv->buffer_pool,
                                               priv->buffer_pool);

      /* append to garbage collection list if it's the wrong size (might
       * happen if caps change). Only the main thread can delete CoglBuffers */
      if (G_UNLIKELY (new_buffer->size != size))
        {
          priv->purge_pool = g_slist_prepend (priv->purge_pool,
                                              new_buffer);
        }
      else
        {
          /* found a suitable free buffer */
          break;
        }
    }
  g_mutex_unlock (priv->pool_lock);

  /* we did not find a free buffer, let's ask and wait for one */
  if (new_buffer == NULL)
    {
      ClutterGstBufferRequest *request;

      g_mutex_lock (priv->pool_lock);
      /* if we are flushing, don't even request a buffer now */
      if (priv->pool_in_flush)
        goto flushing;

      request = clutter_gst_buffer_request_new (size);
      clutter_gst_video_sink_queue_buffer_request (sink, request);

      fprintf (stderr, "*** (%d) waiting for new buffer\n", i);
      fprintf (stderr, "*** (%d) (req%d) waiting for new buffer\n",
               i, request->id);
      g_cond_wait (request->wait_for_buffer, priv->pool_lock);

      new_buffer = request->buffer;
      clutter_gst_buffer_request_free (request);

      /* we might have been woken up by a flush event */
      if (priv->pool_in_flush)
        {
          g_mutex_unlock (priv->pool_lock);
          goto flushing;
        }

      g_mutex_unlock (priv->pool_lock);
    }

  /* at this point we should really have one... */
  if (G_UNLIKELY (new_buffer == NULL || GST_BUFFER_DATA (new_buffer) == NULL))
    goto no_memory;

#if 0
  GST_LOG_OBJECT (sink, "let's use %p (%d bytes)", new_buffer, size);
#endif
  fprintf (stderr, "*** (%d) let's use %p\n", i, new_buffer);

  gst_buffer_set_caps (GST_BUFFER_CAST (new_buffer), caps);
  GST_MINI_OBJECT_CAST (new_buffer)->flags = 0;

  *buf = GST_BUFFER_CAST (new_buffer);

  return GST_FLOW_OK;

flushing:
  {
    GST_DEBUG_OBJECT (sink, "The pool is flushing");
    return GST_FLOW_WRONG_STATE;
  }
no_memory:
  {
    GST_ERROR_OBJECT (sink, "Could not create a buffer of size %d", size);
    fprintf (stderr, "Could not create a buffer of size %d\n", size);
    /* FIXME: post a message on the bus */
    return GST_FLOW_ERROR;
  }
incompatible_caps:
  {
    GST_ERROR_OBJECT (sink, "Could not create a buffer for caps %"
                      GST_PTR_FORMAT, intersection);
    fprintf (stderr, "Could not create a buffer for caps %p", intersection);
    gst_caps_unref (intersection);
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static GstFlowReturn
clutter_gst_video_sink_render (GstBaseSink *bsink,
                               GstBuffer   *buffer)
{
  ClutterGstVideoSink *sink = CLUTTER_GST_VIDEO_SINK (bsink);

  GST_DEBUG_OBJECT (sink, "pushing %p", buffer);
  clutter_gst_source_push (sink->priv->source, (ClutterGstBuffer *) buffer);

  return GST_FLOW_OK;
}

static GstCaps *
clutter_gst_video_sink_get_caps (GstBaseSink *bsink)
{
  ClutterGstVideoSink *sink;
  GstCaps *our_caps;

  sink = CLUTTER_GST_VIDEO_SINK (bsink);
  our_caps = gst_caps_ref (sink->priv->available_caps);

#if 0
  GST_LOG_OBJECT (sink, "we are being asked for our caps: %" GST_PTR_FORMAT,
                  our_caps);
#endif

  return our_caps;
}

static gboolean
clutter_gst_video_sink_set_caps (GstBaseSink *bsink,
                                 GstCaps     *caps)
{
  ClutterGstVideoSink        *sink;
  ClutterGstVideoSinkPrivate *priv;
  GstCaps                    *intersection;
  GstStructure               *structure;
  gboolean                    ret;
  const GValue               *fps;
  const GValue               *par;
  gint                        width, height;

  sink = CLUTTER_GST_VIDEO_SINK(bsink);
  priv = sink->priv;

  GST_INFO_OBJECT (sink, "we are being informed that the caps %"
                    GST_PTR_FORMAT " will be used", caps);

  intersection = gst_caps_intersect (priv->available_caps, caps);
  if (gst_caps_is_empty (intersection)) 
    return FALSE;

  gst_caps_unref (intersection);

  structure = gst_caps_get_structure (caps, 0);

  ret  = gst_structure_get_int (structure, "width", &width);
  ret &= gst_structure_get_int (structure, "height", &height);
  fps  = gst_structure_get_value (structure, "framerate");
  ret &= (fps != NULL);

  if (!ret)
    return FALSE;

  priv->width  = width;
  priv->height = height;
  priv->fps_n  = gst_value_get_fraction_numerator (fps);
  priv->fps_d  = gst_value_get_fraction_denominator (fps);

  par  = gst_structure_get_value (structure, "pixel-aspect-ratio");
  if (par) 
    {
      priv->par_n = gst_value_get_fraction_numerator (par);
      priv->par_d = gst_value_get_fraction_denominator (par);
    } 
  else 
    priv->par_n = priv->par_d = 1;

  return TRUE;
}

static void
clutter_gst_video_sink_dispose (GObject *object)
{
  ClutterGstVideoSink *self;
  ClutterGstVideoSinkPrivate *priv;

  self = CLUTTER_GST_VIDEO_SINK (object);
  priv = self->priv;

  if (priv->renderer_state == CLUTTER_GST_RENDERER_RUNNING ||
      priv->renderer_state == CLUTTER_GST_RENDERER_NEED_GC)
    {
      priv->renderer->deinit (self);
      priv->renderer_state = CLUTTER_GST_RENDERER_STOPPED;
    }

  if (priv->texture)
    {
      g_object_unref (priv->texture);
      priv->texture = NULL;
    }

  if (priv->available_caps)
    {
      gst_caps_unref (priv->available_caps);
      priv->available_caps = NULL;
    }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
clutter_gst_video_sink_finalize (GObject *object)
{
  ClutterGstVideoSink *self;
  ClutterGstVideoSinkPrivate *priv;

  self = CLUTTER_GST_VIDEO_SINK (object);
  priv = self->priv;

  g_slist_free (priv->renderers);

  g_array_free (priv->signal_handler_ids, TRUE);

  while (! g_queue_is_empty (priv->buffer_requests))
    {
      ClutterGstBufferRequest *request;

      request = g_queue_pop_head (priv->buffer_requests);
      clutter_gst_buffer_request_free (request);
    }
  g_queue_free (priv->buffer_requests);

  /* FIXME free buffer pools  */
  g_mutex_free (priv->pool_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
clutter_gst_video_sink_set_property (GObject *object,
                                     guint prop_id,
                                     const GValue *value,
                                     GParamSpec *pspec)
{
  ClutterGstVideoSink *sink = CLUTTER_GST_VIDEO_SINK (object);
  ClutterGstVideoSinkPrivate *priv = sink->priv;

  switch (prop_id) 
    {
    case PROP_TEXTURE:
      if (priv->texture)
        g_object_unref (priv->texture);

      priv->texture = CLUTTER_TEXTURE (g_value_dup_object (value));
      break;
    case PROP_UPDATE_PRIORITY:
      clutter_gst_video_sink_set_priority (sink, g_value_get_int (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_gst_video_sink_get_property (GObject *object,
                                     guint prop_id,
                                     GValue *value,
                                     GParamSpec *pspec)
{
  ClutterGstVideoSink *sink = CLUTTER_GST_VIDEO_SINK (object);
  ClutterGstVideoSinkPrivate *priv = sink->priv;

  switch (prop_id) 
    {
    case PROP_TEXTURE:
      g_value_set_object (value, priv->texture);
      break;
    case PROP_UPDATE_PRIORITY:
      g_value_set_int (value, g_source_get_priority ((GSource *) priv->source));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
clutter_gst_video_sink_start (GstBaseSink *base_sink)
{
  ClutterGstVideoSink        *sink = CLUTTER_GST_VIDEO_SINK (base_sink);
#if 0
  ClutterGstVideoSinkPrivate *priv = sink->priv;
#endif

  GST_INFO_OBJECT (sink, "starting");

  return TRUE;
}

static gboolean
clutter_gst_video_sink_stop (GstBaseSink *base_sink)
{
  ClutterGstVideoSink        *sink = CLUTTER_GST_VIDEO_SINK (base_sink);
  ClutterGstVideoSinkPrivate *priv = sink->priv;
  guint i;

  if (priv->source)
    {
      GSource *source = (GSource *) priv->source;

      g_source_destroy (source);
      g_source_unref (source);
      priv->source = NULL;
    }

  priv->renderer_state = CLUTTER_GST_RENDERER_STOPPED;

  for (i = 0; i < priv->signal_handler_ids->len; i++)
    {
      gulong id = g_array_index (priv->signal_handler_ids, gulong, i);

      g_signal_handler_disconnect (priv->texture, id);
    }
  g_array_set_size (priv->signal_handler_ids, 0);

  return TRUE;
}

static gboolean
clutter_gst_video_sink_event (GstBaseSink *bsink,
                              GstEvent    *event)
{
  ClutterGstVideoSink *sink = CLUTTER_GST_VIDEO_SINK (bsink);
  ClutterGstVideoSinkPrivate *priv = sink->priv;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      g_mutex_lock (priv->pool_lock);
      priv->pool_in_flush = TRUE;
      /* we might have some buffer_alloc() requests pending for _dispatch() to
       * answer. Cancel those requests in the case the flush makes the queue
       * pause the sink_pad task (and thus blocks the threads waiting in
       * buffer_alloc() as the main thread is blocked */
      while (!g_queue_is_empty (priv->buffer_requests))
        {
          ClutterGstBufferRequest *request;

          request = g_queue_pop_head (priv->buffer_requests);
          g_cond_signal (request->wait_for_buffer);
          /* buffer_alloc() will free the request */
        }
      g_mutex_unlock (priv->pool_lock);
      break;
    case GST_EVENT_FLUSH_STOP:
      g_mutex_lock (priv->pool_lock);
      priv->pool_in_flush = FALSE;
      g_mutex_unlock (priv->pool_lock);
    default:
      break;
  }

  if (GST_BASE_SINK_CLASS (parent_class)->event)
    return GST_BASE_SINK_CLASS (parent_class)->event (bsink, event);
  else
    return TRUE;
}

static void
clutter_gst_video_sink_class_init (ClutterGstVideoSinkClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseSinkClass *gstbase_sink_class = GST_BASE_SINK_CLASS (klass);
  GParamSpec *pspec;

  g_type_class_add_private (klass, sizeof (ClutterGstVideoSinkPrivate));

  gobject_class->set_property = clutter_gst_video_sink_set_property;
  gobject_class->get_property = clutter_gst_video_sink_get_property;

  gobject_class->dispose = clutter_gst_video_sink_dispose;
  gobject_class->finalize = clutter_gst_video_sink_finalize;

  gstbase_sink_class->buffer_alloc = clutter_gst_video_sink_buffer_alloc;
  gstbase_sink_class->render = clutter_gst_video_sink_render;
  gstbase_sink_class->preroll = clutter_gst_video_sink_render;
  gstbase_sink_class->start = clutter_gst_video_sink_start;
  gstbase_sink_class->stop = clutter_gst_video_sink_stop;
  gstbase_sink_class->set_caps = clutter_gst_video_sink_set_caps;
  gstbase_sink_class->get_caps = clutter_gst_video_sink_get_caps;
  gstbase_sink_class->event = clutter_gst_video_sink_event;

  /**
   * ClutterGstVideoSink:texture:
   *
   * This is the texture the video is decoded into. It can be any
   * #ClutterTexture, however Cluter-Gst has a handy subclass,
   * #ClutterGstVideoTexture, that implements the #ClutterMedia
   * interface.
   */
  pspec = g_param_spec_object ("texture",
                               "Texture",
                               "Texture the video will be decoded into",
                               CLUTTER_TYPE_TEXTURE,
                               CLUTTER_GST_PARAM_READWRITE);
  g_object_class_install_property (gobject_class, PROP_TEXTURE, pspec);

  /**
   * ClutterGstVideoSink:update-priority:
   *
   * Clutter-Gst installs a #GSource to signal that a new frame is ready to
   * the Clutter thread. This property allows to tweak the priority of the
   * source (Lower value is higher priority).
   *
   * Since 1.0
   */
  pspec = g_param_spec_int ("update-priority",
                            "Update Priority",
                            "Priority of video updates in the Clutter thread",
                            -G_MAXINT, G_MAXINT,
                            CLUTTER_GST_DEFAULT_PRIORITY,
                            CLUTTER_GST_PARAM_READWRITE);
  g_object_class_install_property (gobject_class, PROP_UPDATE_PRIORITY, pspec);
}

/**
 * clutter_gst_video_sink_new:
 * @texture: a #ClutterTexture
 *
 * Creates a new GStreamer video sink which uses @texture as the target
 * for sinking a video stream from GStreamer.
 *
 * <note>This function has to be called from Clutter's main thread. While
 * GStreamer will spawn threads to do its work, we want all the GL calls to
 * happen in the same thread. Clutter-gst knows which thread it is by
 * assuming this constructor is called from the Clutter thread.</note>
 * Return value: a #GstElement for the newly created video sink
 */
GstElement *
clutter_gst_video_sink_new (ClutterTexture *texture)
{
  return g_object_new (CLUTTER_GST_TYPE_VIDEO_SINK,
                       "texture", texture,
                       NULL);
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  gboolean ret = gst_element_register (plugin,
                                             "cluttersink",
                                       GST_RANK_PRIMARY,
                                       CLUTTER_GST_TYPE_VIDEO_SINK);
  return ret;
}

GST_PLUGIN_DEFINE_STATIC (GST_VERSION_MAJOR,
                          GST_VERSION_MINOR,
                          "cluttersink",
                          "Element to render to Clutter textures",
                          plugin_init,
                          VERSION,
                          "LGPL", /* license */
                          PACKAGE,
                          "http://www.clutter-project.org");
