/*
 * Clutter-GStreamer.
 *
 * GStreamer integration library for Clutter.
 *
 * clutter-gst-player.c - Wrap some convenience functions around playbin
 *
 * Authored By Damien Lespiau    <damien.lespiau@intel.com>
 *             Lionel Landwerlin <lionel.g.landwerlin@linux.intel.com>
 *             Matthew Allum     <mallum@openedhand.com>
 *             Emmanuele Bassi   <ebassi@linux.intel.com>
 *             Andre Moreira Magalhaes <andre.magalhaes@collabora.co.uk>
 *
 * Copyright (C) 2006 OpenedHand
 * Copyright (C) 2009-2013 Intel Corporation
 * Copyright (C) 2012 Collabora Ltd. <http://www.collabora.co.uk/>
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
 * SECTION:clutter-gst-playback
 * @short_description: A #ClutterGstPlayback to play media streams
 *
 * #ClutterGstPlayback implements #ClutterGstPlayer.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "clutter-gst-debug.h"
#include "clutter-gst-enum-types.h"
#include "clutter-gst-marshal.h"
#include "clutter-gst-playback.h"
#include "clutter-gst-player.h"
#include "clutter-gst-private.h"

#include <string.h>

#include <gio/gio.h>
#include <gst/video/video.h>
#include <gst/tag/tag.h>
#include <gst/audio/streamvolume.h>

#if defined (CLUTTER_WINDOWING_X11) && defined (HAVE_HW_DECODER_SUPPORT)
#define GST_USE_UNSTABLE_API 1
#include <gst/video/videocontext.h>
#include <clutter/x11/clutter-x11.h>
#endif

static void player_iface_init (ClutterGstPlayerIface *iface);

G_DEFINE_TYPE_WITH_CODE (ClutterGstPlayback, clutter_gst_playback, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (CLUTTER_GST_TYPE_PLAYER, player_iface_init))

#define GST_PLAYBACK_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), CLUTTER_GST_TYPE_PLAYBACK, ClutterGstPlaybackPrivate))

/* idle timeouts (in ms) */
#define TICK_TIMEOUT        500
#define BUFFERING_TIMEOUT   250

enum
{
  PROP_0,

  PROP_URI,
  PROP_PLAYING,
  PROP_PROGRESS,
  PROP_SUBTITLE_URI,
  PROP_SUBTITLE_FONT_NAME,
  PROP_AUDIO_VOLUME,
  PROP_CAN_SEEK,
  PROP_BUFFER_FILL,
  PROP_DURATION,
  PROP_IDLE,
  PROP_USER_AGENT,
  PROP_SEEK_FLAGS,
  PROP_AUDIO_STREAMS,
  PROP_AUDIO_STREAM,
  PROP_SUBTITLE_TRACKS,
  PROP_SUBTITLE_TRACK,
  PROP_IN_SEEK
};

enum
{
  SHOULD_BUFFER = 1,

  LAST_SIGNAL
};

/* Elements don't expose header files */
typedef enum {
  GST_PLAY_FLAG_VIDEO         = (1 << 0),
  GST_PLAY_FLAG_AUDIO         = (1 << 1),
  GST_PLAY_FLAG_TEXT          = (1 << 2),
  GST_PLAY_FLAG_VIS           = (1 << 3),
  GST_PLAY_FLAG_SOFT_VOLUME   = (1 << 4),
  GST_PLAY_FLAG_NATIVE_AUDIO  = (1 << 5),
  GST_PLAY_FLAG_NATIVE_VIDEO  = (1 << 6),
  GST_PLAY_FLAG_DOWNLOAD      = (1 << 7),
  GST_PLAY_FLAG_BUFFERING     = (1 << 8),
  GST_PLAY_FLAG_DEINTERLACE   = (1 << 9)
} GstPlayFlags;

struct _ClutterGstPlaybackPrivate
{
  GstElement *pipeline;
  GstBus *bus;
  ClutterGstVideoSink *video_sink;
  GArray *gst_pipe_sigs;
  GArray *gst_bus_sigs;

  ClutterGstFrame *current_frame;

  gchar *uri;

  guint is_idle : 1;
  guint is_live : 1;
  guint can_seek : 1;
  guint in_seek : 1;
  guint is_changing_uri : 1;
  guint in_error : 1;
  guint in_eos : 1;
  guint in_download_buffering : 1;

  gdouble stacked_progress;

  gdouble target_progress;
  GstState target_state;
  GstState force_state;

  guint tick_timeout_id;
  guint buffering_timeout_id;

  /* This is a cubic volume, suitable for use in a UI cf. StreamVolume doc */
  gdouble volume;

  gdouble buffer_fill;
  gdouble duration;
  gchar *font_name;
  gchar *user_agent;

  GstSeekFlags seek_flags;    /* flags for the seek in set_progress(); */

  GList *audio_streams;
  GList *subtitle_tracks;
};


static guint signals[LAST_SIGNAL] = { 0, };

static gboolean player_buffering_timeout (gpointer data);

/* Logic */

#ifdef CLUTTER_GST_ENABLE_DEBUG
static gchar *
get_stream_description (GstTagList *tags,
                        gint        track_num)
{
  gchar *description = NULL;

  if (tags)
    {

      gst_tag_list_get_string (tags, GST_TAG_LANGUAGE_CODE, &description);

      if (description)
        {
          const gchar *language = gst_tag_get_language_name (description);

          if (language)
            {
              g_free (description);
              description = g_strdup (language);
            }
        }

      if (!description)
        gst_tag_list_get_string (tags, GST_TAG_CODEC, &description);
    }

  if (!description)
    description = g_strdup_printf ("Track %d", track_num);

  return description;
}

gchar *
list_to_string (GList *list)
{
  GstTagList *tags;
  gchar *description;
  GString *string;
  GList *l;
  gint n, i;

  if (!list)
    return g_strdup ("<empty list>");

  string = g_string_new (NULL);
  n = g_list_length (list);
  for (i = 0, l = list; i < n - 1; i++, l = g_list_next (l))
    {
      tags = l->data;
      description = get_stream_description (tags, i);
      g_string_append_printf (string, "%s, ", description);
      g_free (description);
    }

  tags = l->data;
  description = get_stream_description (tags, i);
  g_string_append_printf (string, "%s", (gchar *) description);
  g_free (description);

  return g_string_free (string, FALSE);
}
#endif

static const gchar *
gst_state_to_string (GstState state)
{
  switch (state)
    {
    case GST_STATE_VOID_PENDING:
      return "pending";
    case GST_STATE_NULL:
      return "null";
    case GST_STATE_READY:
      return "ready";
    case GST_STATE_PAUSED:
      return "paused";
    case GST_STATE_PLAYING:
      return "playing";
    }

  return "Unknown state";
}

static void
free_tags_list (GList **listp)
{
  GList *l;

  l = *listp;
  while (l)
    {
      if (l->data)
        gst_tag_list_unref (l->data);
      l = g_list_delete_link (l, l);
    }

  *listp = NULL;
}

static GstStateChangeReturn
set_pipeline_target_state (ClutterGstPlayback *self, GstState state)
{
  ClutterGstPlaybackPrivate *priv = self->priv;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  priv->target_state = state;
  if (!priv->pipeline || !priv->uri)
    goto out;

  if (priv->force_state == GST_STATE_VOID_PENDING)
    ret = gst_element_set_state (priv->pipeline, state);

out:
  return ret;
}

static GstStateChangeReturn
force_pipeline_state (ClutterGstPlayback *self, GstState state)
{
  ClutterGstPlaybackPrivate *priv = self->priv;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  priv->force_state = state;
  if (!priv->pipeline)
    goto out;

  if (state == GST_STATE_VOID_PENDING)
    state = priv->target_state;

  ret = gst_element_set_state (priv->pipeline, state);

out:
  return ret;
}


static gboolean
tick_timeout (gpointer data)
{
  GObject *player = data;

  g_object_notify (player, "progress");

  return TRUE;
}

static void
player_set_user_agent (ClutterGstPlayback *self,
                       const gchar        *user_agent)
{
  ClutterGstPlaybackPrivate *priv = self->priv;
  GstElement *source;
  GParamSpec *pspec;

  if (user_agent == NULL)
    return;

  g_object_get (priv->pipeline, "source", &source, NULL);
  if (source == NULL)
    return;

  pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (source),
                                        "user-agent");
  if (pspec == NULL)
    return;

  CLUTTER_GST_NOTE (MEDIA, "setting user agent: %s", user_agent);

  g_object_set (source, "user-agent", user_agent, NULL);
}

static void
autoload_subtitle (ClutterGstPlayback *self,
                   const gchar        *uri)
{
  ClutterGstPlaybackPrivate *priv = self->priv;
  gchar *path, *dot, *subtitle_path;
  GFile *video;
  guint i;

  static const char subtitles_extensions[][4] =
    {
      "sub", "SUB",
      "srt", "SRT",
      "smi", "SMI",
      "ssa", "SSA",
      "ass", "ASS",
      "asc", "ASC"
    };

  /* do not try to look for subtitle files if the video file is not mounted
   * locally */
  if (!g_str_has_prefix (uri, "file://"))
    return;

  /* Retrieve the absolute path of the video file */
  video = g_file_new_for_uri (uri);
  path = g_file_get_path (video);
  g_object_unref (video);
  if (path == NULL)
    return;

  /* Put a '\0' after the dot of the extension */
  dot = strrchr (path, '.');
  if (dot == NULL) {
    g_free (path);
    return;
  }
  *++dot = '\0';

  /* we can't use path as the temporary buffer for the paths of the potential
   * subtitle files as we may not have enough room there */
  subtitle_path = g_malloc (strlen (path) + 1 + 4);
  strcpy (subtitle_path, path);

  /* reuse dot to point to the first byte of the extension of subtitle_path */
  dot = subtitle_path + (dot - path);

  for (i = 0; i < G_N_ELEMENTS (subtitles_extensions); i++)
    {
      GFile *candidate;

      memcpy (dot, subtitles_extensions[i], 4);
      candidate = g_file_new_for_path (subtitle_path);
      if (g_file_query_exists (candidate, NULL))
        {
          gchar *suburi;

          suburi = g_file_get_uri (candidate);

          CLUTTER_GST_NOTE (MEDIA, "found subtitle: %s", suburi);

          g_object_set (priv->pipeline, "suburi", suburi, NULL);
          g_free (suburi);

          g_object_unref (candidate);
          break;
        }

      g_object_unref (candidate);
    }

  g_free (path);
  g_free (subtitle_path);
}

static void
set_subtitle_uri (ClutterGstPlayback *self,
                  const gchar        *uri)
{
  ClutterGstPlaybackPrivate *priv = self->priv;
  GstPlayFlags flags;

  if (!priv->pipeline)
    return;

  CLUTTER_GST_NOTE (MEDIA, "setting subtitle URI: %s", uri);

  g_object_get (priv->pipeline, "flags", &flags, NULL);

  g_object_set (priv->pipeline, "suburi", uri, NULL);

  g_object_set (priv->pipeline, "flags", flags, NULL);
}

static void
player_configure_buffering_timeout (ClutterGstPlayback *self,
                                    guint               ms)
{
  ClutterGstPlaybackPrivate *priv = self->priv;

  if (priv->buffering_timeout_id)
    {
      g_source_remove (priv->buffering_timeout_id);
      priv->buffering_timeout_id = 0;
    }

  if (ms)
    {
      priv->buffering_timeout_id =
        g_timeout_add (ms, player_buffering_timeout, self);
      player_buffering_timeout (self);

    }
}

static void
player_clear_download_buffering (ClutterGstPlayback *self)
{
  ClutterGstPlaybackPrivate *priv = self->priv;

  player_configure_buffering_timeout (self, 0);

  priv->in_download_buffering = FALSE;
}

static gboolean
is_live_pipeline (GstElement *pipeline)
{
  GstState state, pending;
  GstStateChangeReturn state_change_res;
  gboolean is_live = FALSE;

  /* get pipeline current state, we need to change the pipeline state to PAUSED to
   * see if we are dealing with a live source and we want to restore the pipeline
   * state afterwards */
  gst_element_get_state (pipeline, &state, &pending, 0);

  /* a pipeline with live source should return NO_PREROLL in PAUSE */
  state_change_res = gst_element_set_state (pipeline, GST_STATE_PAUSED);
  is_live = (state_change_res == GST_STATE_CHANGE_NO_PREROLL);

  /* restore pipeline previous state */
  if (pending == GST_STATE_VOID_PENDING)
    gst_element_set_state (pipeline, state);
  else
    gst_element_set_state (pipeline, pending);

  return is_live;
}

static void
set_uri (ClutterGstPlayback *self,
         const gchar        *uri)
{
  ClutterGstPlaybackPrivate *priv = self->priv;

  CLUTTER_GST_NOTE (MEDIA, "setting uri %s", uri);

  if (!priv->pipeline)
    return;

  g_free (priv->uri);

  priv->in_eos = FALSE;
  priv->in_error = FALSE;

  if (uri)
    {
      priv->uri = g_strdup (uri);

      /* Ensure the tick timeout is installed.
       *
       * We also have it installed in PAUSED state, because
       * seeks etc may have a delayed effect on the position.
       */
      if (priv->tick_timeout_id == 0)
        {
          priv->tick_timeout_id =
            g_timeout_add (TICK_TIMEOUT, tick_timeout, self);
        }

      /* try to load subtitles based on the uri of the file */
      set_subtitle_uri (self, NULL);

      /* reset the states of download buffering */
      player_clear_download_buffering (self);
    }
  else
    {
      priv->uri = NULL;

      if (priv->tick_timeout_id)
	{
	  g_source_remove (priv->tick_timeout_id);
	  priv->tick_timeout_id = 0;
	}

      if (priv->buffering_timeout_id)
        {
          g_source_remove (priv->buffering_timeout_id);
          priv->buffering_timeout_id = 0;
        }
    }

  priv->can_seek = FALSE;
  priv->duration = 0.0;
  priv->stacked_progress = -1.0;
  priv->target_progress = 0.0;

  CLUTTER_GST_NOTE (MEDIA, "setting URI: %s", uri);

  if (uri)
    {
      /* Change uri, force the pipeline to NULL so the uri can be changed, then
       * set the pipeline to "unforced" mode so it will return to its current
       * target mode */
      force_pipeline_state (self, GST_STATE_NULL);

      g_object_set (priv->pipeline, "uri", uri, NULL);

      priv->is_live = is_live_pipeline (priv->pipeline);

      set_subtitle_uri (self, NULL);
      autoload_subtitle (self, uri);

      force_pipeline_state (self, GST_STATE_VOID_PENDING);

      priv->is_changing_uri = TRUE;
    }
  else
    {
      priv->is_idle = TRUE;
      priv->is_live = FALSE;
      set_subtitle_uri (self, NULL);
      gst_element_set_state (priv->pipeline, GST_STATE_NULL);
      g_object_notify (G_OBJECT (self), "idle");
    }

  /*
   * Emit notifications for all these to make sure UI is not showing
   * any properties of the old URI.
   */
  g_object_notify (G_OBJECT (self), "uri");
  g_object_notify (G_OBJECT (self), "can-seek");
  g_object_notify (G_OBJECT (self), "duration");
  g_object_notify (G_OBJECT (self), "progress");

  free_tags_list (&priv->audio_streams);
  CLUTTER_GST_NOTE (AUDIO_STREAM, "audio-streams changed");
  g_object_notify (G_OBJECT (self), "audio-streams");

  free_tags_list (&priv->subtitle_tracks);
  CLUTTER_GST_NOTE (SUBTITLES, "subtitle-tracks changed");
  g_object_notify (G_OBJECT (self), "subtitle-tracks");
}

static gdouble
get_audio_volume (ClutterGstPlayback *self)
{
  ClutterGstPlaybackPrivate *priv = self->priv;

  if (!priv->pipeline)
    return 0.0;

  CLUTTER_GST_NOTE (MEDIA, "get volume: %.02f", priv->volume);

  return priv->volume;
}

static void
set_audio_volume (ClutterGstPlayback *self,
                  gdouble             volume)
{
  ClutterGstPlaybackPrivate *priv = self->priv;

    if (!priv->pipeline)
      return;

  CLUTTER_GST_NOTE (MEDIA, "set volume: %.02f", volume);

  volume = CLAMP (volume, 0.0, 1.0);
  gst_stream_volume_set_volume (GST_STREAM_VOLUME (priv->pipeline),
				GST_STREAM_VOLUME_FORMAT_CUBIC,
				volume);
  g_object_notify (G_OBJECT (self), "audio-volume");
}

static void
set_in_seek (ClutterGstPlayback *self,
             gboolean            seeking)
{
  ClutterGstPlaybackPrivate *priv = self->priv;

  priv->in_seek = seeking;
  g_object_notify (G_OBJECT (self), "in-seek");
}


static void
set_playing (ClutterGstPlayback *self,
             gboolean            playing)
{
  ClutterGstPlaybackPrivate *priv = self->priv;

  if (!priv->pipeline)
    return;

  CLUTTER_GST_NOTE (MEDIA, "set playing: %d", playing);

  priv->in_error = FALSE;
  priv->in_eos = FALSE;

  if (!priv->uri && playing)
    {
      g_warning ("Unable to start playing: no URI is set");
      return;
    }

  set_pipeline_target_state (self,
    playing ? GST_STATE_PLAYING : GST_STATE_PAUSED);

  g_object_notify (G_OBJECT (self), "playing");
  g_object_notify (G_OBJECT (self), "progress");
}

static gboolean
get_playing (ClutterGstPlayback *player)
{
  ClutterGstPlaybackPrivate *priv = player->priv;
  gboolean playing;

  if (!priv->pipeline || !priv->uri)
    return FALSE;

  playing = priv->target_state == GST_STATE_PLAYING;

  CLUTTER_GST_NOTE (MEDIA, "get playing: %d", playing);

  return playing;
}

static void
set_progress (ClutterGstPlayback *self,
              gdouble             progress)
{
  ClutterGstPlaybackPrivate *priv = self->priv;
  GstQuery *duration_q;
  gint64 position;

  if (!priv->pipeline)
    return;

  CLUTTER_GST_NOTE (MEDIA, "set progress: %.02f", progress);

  priv->in_eos = FALSE;
  priv->target_progress = progress;

  if (priv->is_changing_uri || priv->in_seek)
    {
      /* We can't seek right now, let's save the position where we
         want to seek and do that later. */
      CLUTTER_GST_NOTE (MEDIA,
                        "already seeking. stacking progress point.");
      priv->stacked_progress = progress;
      return;
    }

  duration_q = gst_query_new_duration (GST_FORMAT_TIME);

  position = 0;
  if (gst_element_query (priv->pipeline, duration_q))
    {
      gint64 duration = 0;

      gst_query_parse_duration (duration_q, NULL, &duration);

      position = progress * duration;
    }
  else if (progress != 0.0)
    {
      /* Can't seek into the file if the duration is unknown */
      goto out;
    }

  gst_element_seek (priv->pipeline,
		    1.0,
		    GST_FORMAT_TIME,
		    GST_SEEK_FLAG_FLUSH | priv->seek_flags,
		    GST_SEEK_TYPE_SET,
		    position,
		    GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);

  set_in_seek (self, TRUE);
  CLUTTER_GST_NOTE (MEDIA, "set progress (seeked): %.02f", progress);
  /* If we seek we want to go and babysit the buffering again in case of
   * download buffering */
  if (!priv->is_live && clutter_gst_playback_get_buffering_mode (self) ==
      CLUTTER_GST_BUFFERING_MODE_DOWNLOAD)
    {
      force_pipeline_state (self, GST_STATE_PAUSED);
    }
  priv->stacked_progress = -1.0;

out:
  gst_query_unref (duration_q);
}

static gdouble
get_progress (ClutterGstPlayback *self)
{
  ClutterGstPlaybackPrivate *priv = self->priv;
  GstQuery *position_q, *duration_q;
  gdouble progress;

  if (!priv->pipeline)
    return 0.0;

  /* when hitting an error or after an EOS, playbin has some weird values when
   * querying the duration and progress. We default to 0.0 on error and 1.0 on
   * EOS */
  if (priv->in_error)
    {
      CLUTTER_GST_NOTE (MEDIA, "get progress (error): 0.0");
      return 0.0;
    }

  if (priv->in_eos)
    {
      CLUTTER_GST_NOTE (MEDIA, "get progress (eos): 1.0");
      return 1.0;
    }

  /* When seeking, the progress returned by playbin is 0.0. We want that to be
   * the last known position instead as returning 0.0 will have some ugly
   * effects, say on a progress bar getting updated from the progress tick. */
  if (priv->in_seek || priv->is_changing_uri)
    {
      CLUTTER_GST_NOTE (MEDIA, "get progress (target): %.02f",
                        priv->target_progress);
      return priv->target_progress;
    }

  position_q = gst_query_new_position (GST_FORMAT_TIME);
  duration_q = gst_query_new_duration (GST_FORMAT_TIME);

  if (gst_element_query (priv->pipeline, position_q) &&
      gst_element_query (priv->pipeline, duration_q))
    {
      gint64 position, duration;

      position = duration = 0;

      gst_query_parse_position (position_q, NULL, &position);
      gst_query_parse_duration (duration_q, NULL, &duration);

      progress = CLAMP ((gdouble) position / (gdouble) duration, 0.0, 1.0);
    }
  else
    progress = 0.0;

  gst_query_unref (position_q);
  gst_query_unref (duration_q);

  CLUTTER_GST_NOTE (MEDIA, "get progress (pipeline): %.02f", progress);

  return progress;
}

static void
set_subtitle_font_name (ClutterGstPlayback *self,
                        const gchar        *font_name)
{
  ClutterGstPlaybackPrivate *priv = self->priv;

  if (!priv->pipeline)
    return;

  CLUTTER_GST_NOTE (MEDIA, "setting subtitle font to %s", font_name);

  g_free (priv->font_name);
  priv->font_name = g_strdup (font_name);
  g_object_set (priv->pipeline, "subtitle-font-desc", font_name, NULL);
}

static gdouble
get_position (ClutterGstPlayback *self)
{
  ClutterGstPlaybackPrivate *priv = self->priv;
  gboolean success;
  GstFormat format = GST_FORMAT_TIME;
  gint64 position;

  success = gst_element_query_position (priv->pipeline, format, &position);
  if (G_UNLIKELY (success != TRUE))
    return 0.0;

  return (gdouble) position / GST_SECOND;
}

static gboolean
player_should_buffer (ClutterGstPlayback *self, GstQuery *query)
{
  ClutterGstPlaybackPrivate *priv = self->priv;
  gdouble position;
  gboolean ret = FALSE;
  gint64 left;
  gdouble time_left;
  gboolean busy;

  /* Use the estimated total duration left as estimated by queue2
   * based on the average incoming bitrate, we can stop buffering once
   * the remaining download takes less time then the remaining play
   * time (with a 10% safety margin). However regardless of that keep
   * buffering as long as queue2 indicates that buffering should
   * happen (based on its high water marks) */
  gst_query_parse_buffering_range (query, NULL, NULL, NULL, &left);
  gst_query_parse_buffering_percent (query, &busy, NULL);

  position = get_position (self);
  if (priv->duration)
    time_left = priv->duration - position;
  else
    time_left = 0;

  if (left == -1 || (!busy && (((gdouble)left * 1.1) / 1000) < time_left))
    {
      ret = FALSE;
    }
  else
    {
      ret = TRUE;
    }

  return ret;
}

static gboolean
player_buffering_timeout (gpointer data)
{
  ClutterGstPlayback *self = (ClutterGstPlayback *) data;
  ClutterGstPlaybackPrivate *priv = self->priv;
  GstQuery *query;
  gboolean res;
  gboolean busy;
  gboolean ret = TRUE;
  GstBufferingMode mode;
  gboolean should_buffer;


  /* currently seeking, wait until it's done to get consistent results */
  if (priv->in_seek)
    return TRUE;

  /* queue2 only knows about _PERCENT and _BYTES */
  query = gst_query_new_buffering (GST_FORMAT_BYTES);
  res = gst_element_query (priv->pipeline, query);

  if (res == FALSE)
    {
      CLUTTER_GST_NOTE (BUFFERING, "Buffer query failed");
      goto out;
    }

  gst_query_parse_buffering_stats (query, &mode, NULL, NULL, NULL);

  if (mode != GST_BUFFERING_DOWNLOAD)
    {
      CLUTTER_GST_NOTE (BUFFERING,
        "restoring the pipeline as we're not download buffering");
      if (!busy)
        force_pipeline_state (self, GST_STATE_VOID_PENDING);
      ret = FALSE;
      player_clear_download_buffering (self);
      goto out;
    }

  g_signal_emit (self, signals[SHOULD_BUFFER], 0, query, &should_buffer);
  if (should_buffer)
    {
      if (priv->buffer_fill != 0.0)
        {
          priv->buffer_fill = 0.0;
          g_object_notify (G_OBJECT (self), "buffer-fill");
        }
      /* Force to paused if we haven't yet */
      if (priv->force_state == GST_STATE_VOID_PENDING)
        {
          /* Starting buffering again */
          CLUTTER_GST_NOTE (BUFFERING,
            "pausing the pipeline for buffering: %d", busy);
          force_pipeline_state (self, GST_STATE_PAUSED);
        }
    }
  else
    {
      /* Done buffering signal and stop the timeouts */
      player_clear_download_buffering (self);
      force_pipeline_state (self, GST_STATE_VOID_PENDING);
      if (priv->buffer_fill != 1.0)
        {
          priv->buffer_fill = 1.0;
          g_object_notify (G_OBJECT (self), "buffer-fill");
        }
      ret = FALSE;
    }
out:
  gst_query_unref (query);
  return ret;
}

static void
bus_message_error_cb (GstBus             *bus,
                      GstMessage         *message,
                      ClutterGstPlayback *self)
{
  ClutterGstPlaybackPrivate *priv = self->priv;
  GError *error = NULL;

  gst_element_set_state (priv->pipeline, GST_STATE_NULL);

  gst_message_parse_error (message, &error, NULL);
  g_signal_emit_by_name (self, "error", error);
  g_error_free (error);

  priv->is_idle = TRUE;
  g_object_notify (G_OBJECT (self), "idle");
}

/*
 * This is what's intented in the EOS callback:
 *   - receive EOS from playbin
 *   - fire the EOS signal, the user can install a signal handler to loop the
 *     video for instance.
 *   - after having emitted the signal, check the state of the pipeline
 *   - if the pipeline has been set back to playing or pause, don't touch the
 *     idle state. This will avoid drawing a frame (or more) with the idle
 *     material when looping
 */
static void
bus_message_eos_cb (GstBus             *bus,
                    GstMessage         *message,
                    ClutterGstPlayback *self)
{
  ClutterGstPlaybackPrivate *priv = self->priv;
  GstState state, pending;

  priv->in_eos = TRUE;

  gst_element_set_state (priv->pipeline, GST_STATE_READY);

  g_signal_emit_by_name (self, "eos");
  g_object_notify (G_OBJECT (self), "progress");

  gst_element_get_state (priv->pipeline, &state, &pending, 0);
  if (pending)
    state = pending;

  if (!(state == GST_STATE_PLAYING || state == GST_STATE_PAUSED))
    {
      priv->is_idle = TRUE;
      g_object_notify (G_OBJECT (self), "idle");
    }
}

static void
bus_message_buffering_cb (GstBus             *bus,
                          GstMessage         *message,
                          ClutterGstPlayback *self)
{
  ClutterGstPlaybackPrivate *priv = self->priv;
  GstBufferingMode mode;
  GstState current_state;
  gint buffer_percent;

  gst_message_parse_buffering_stats (message, &mode, NULL, NULL, NULL);

  if (mode != GST_BUFFERING_DOWNLOAD)
    priv->in_download_buffering = FALSE;

  switch (mode)
    {
    case GST_BUFFERING_LIVE:
    case GST_BUFFERING_STREAM:
      gst_message_parse_buffering (message, &buffer_percent);
      priv->buffer_fill = CLAMP ((gdouble) buffer_percent / 100.0, 0.0, 1.0);

      CLUTTER_GST_NOTE (BUFFERING, "buffer-fill: %.02f", priv->buffer_fill);

      /* no state management needed for live pipelines */
      if (!priv->is_live)
        {
          /* The playbin documentation says that we need to pause the pipeline
           * when there's not enough data yet. We try to limit the calls to
           * gst_element_set_state() */
          gst_element_get_state (priv->pipeline, &current_state, NULL, 0);

          if (priv->buffer_fill < 1.0)
            {
              if (priv->force_state != GST_STATE_PAUSED)
                {
                  CLUTTER_GST_NOTE (BUFFERING, "pausing the pipeline");
                  force_pipeline_state (self, GST_STATE_PAUSED);
                }
            }
          else
            {
              if (priv->force_state != GST_STATE_VOID_PENDING)
                {
                  CLUTTER_GST_NOTE (BUFFERING, "restoring the pipeline");
                  force_pipeline_state (self, GST_STATE_VOID_PENDING);
                }
            }
        }

      g_object_notify (G_OBJECT (self), "buffer-fill");
      break;

    case GST_BUFFERING_DOWNLOAD:
      if (priv->in_download_buffering)
        break;

      priv->buffer_fill = 0.0;
      g_object_notify (G_OBJECT (self), "buffer-fill");
      /* install the querying idle handler the first time we receive a download
       * buffering message */
      player_configure_buffering_timeout (self, BUFFERING_TIMEOUT);

      priv->in_download_buffering = TRUE;
      break;

    case GST_BUFFERING_TIMESHIFT:
    default:
      g_warning ("Buffering mode %d not handled", mode);
      break;
    }
}

static void
on_source_changed (GstElement         *pipeline,
                   GParamSpec         *pspec,
                   ClutterGstPlayback *self)
{
  ClutterGstPlaybackPrivate *priv = self->priv;

  player_set_user_agent (self, priv->user_agent);
}

static void
query_duration (ClutterGstPlayback *self)
{
  ClutterGstPlaybackPrivate *priv = self->priv;
  gboolean success;
  gint64 duration;
  gdouble new_duration, difference;

  success = gst_element_query_duration (priv->pipeline,
                                        GST_FORMAT_TIME,
                                        &duration);
  if (G_UNLIKELY (success != TRUE))
    return;

  new_duration = (gdouble) duration / GST_SECOND;

  /* while we store the new duration if it sligthly changes, the duration
   * signal is sent only if the new duration is at least one second different
   * from the old one (as the duration signal is mainly used to update the
   * time displayed in a UI */
  difference = ABS (priv->duration - new_duration);
  if (difference > 1e-3)
    {
      CLUTTER_GST_NOTE (MEDIA, "duration: %.02f", new_duration);
      priv->duration = new_duration;

      if (difference > 1.0)
        g_object_notify (G_OBJECT (self), "duration");
    }
}

static void
bus_message_duration_changed_cb (GstBus             *bus,
                                 GstMessage         *message,
                                 ClutterGstPlayback *self)
{
  /* GstElements send a duration-changed message on the bus to signal
   * that the duration has changed and should be re-queried */
  query_duration (self);
}

static void
bus_message_state_change_cb (GstBus             *bus,
                             GstMessage         *message,
                             ClutterGstPlayback *self)
{
  ClutterGstPlaybackPrivate *priv = self->priv;
  GstState old_state, new_state;
  gpointer src;

  src = GST_MESSAGE_SRC (message);
  if (src != priv->pipeline)
    return;

  gst_message_parse_state_changed (message, &old_state, &new_state, NULL);

  CLUTTER_GST_NOTE (MEDIA, "state change:  %s -> %s",
                    gst_state_to_string (old_state),
                    gst_state_to_string (new_state));

  if (old_state == new_state)
    return;

  if (old_state == GST_STATE_READY &&
      new_state == GST_STATE_PAUSED)
    {
      GstQuery *query;

      /* Determine whether we can seek */
      query = gst_query_new_seeking (GST_FORMAT_TIME);

      if (gst_element_query (priv->pipeline, query))
        {
          gboolean can_seek = FALSE;

          gst_query_parse_seeking (query, NULL, &can_seek,
                                   NULL,
                                   NULL);

          priv->can_seek = (can_seek == TRUE) ? TRUE : FALSE;
        }
      else
        {
	  /* could not query for ability to seek by querying the
           * pipeline; let's crudely try by using the URI
	   */
	  if (priv->uri && g_str_has_prefix (priv->uri, "http://"))
            priv->can_seek = FALSE;
          else
            priv->can_seek = TRUE;
	}

      gst_query_unref (query);

      CLUTTER_GST_NOTE (MEDIA, "can-seek: %d", priv->can_seek);

      g_object_notify (G_OBJECT (self), "can-seek");

      query_duration (self);

      priv->is_changing_uri = FALSE;
      if (priv->stacked_progress != -1.0 && priv->can_seek)
        {
          set_progress (self, priv->stacked_progress);
        }
    }

  /* is_idle controls the drawing with the idle material */
  if (old_state > GST_STATE_READY && new_state == GST_STATE_READY)
    {
      priv->is_idle = TRUE;
      g_object_notify (G_OBJECT (self), "idle");
    }
  else if (new_state == GST_STATE_PLAYING)
    {
      priv->is_idle = FALSE;
      g_object_notify (G_OBJECT (self), "idle");
    }
}

static void
bus_message_async_done_cb (GstBus             *bus,
                           GstMessage         *message,
                           ClutterGstPlayback *self)
{
  ClutterGstPlaybackPrivate *priv = self->priv;

  if (priv->in_seek)
    {
      g_object_notify (G_OBJECT (self), "progress");

      set_in_seek (self, FALSE);
      player_configure_buffering_timeout (self, BUFFERING_TIMEOUT);

      if (priv->stacked_progress != -1.0)
        {
          set_progress (self, priv->stacked_progress);
        }
    }
}

static gboolean
on_volume_changed_main_context (gpointer data)
{
  ClutterGstPlayback *self = CLUTTER_GST_PLAYBACK (data);
  ClutterGstPlaybackPrivate *priv = self->priv;
  gdouble volume;

  volume =
    gst_stream_volume_get_volume (GST_STREAM_VOLUME (priv->pipeline),
                                  GST_STREAM_VOLUME_FORMAT_CUBIC);
  priv->volume = volume;

  g_object_notify (G_OBJECT (self), "audio-volume");

  g_object_unref (self);

  return FALSE;
}

/* playbin proxies the volume property change notification directly from
 * the element having the "volume" property. This means this callback is
 * called from the thread that runs the element, potentially different from
 * the main thread */
static void
on_volume_changed (GstElement         *pipeline,
		   GParamSpec         *pspec,
		   ClutterGstPlayback *self)
{
  g_idle_add (on_volume_changed_main_context, g_object_ref (self));
}

static GList *
get_tags (GstElement  *pipeline,
          const gchar *property_name,
          const gchar *action_signal)
{
  GList *ret = NULL;
  gint i, n;

  g_object_get (G_OBJECT (pipeline), property_name, &n, NULL);
  if (n == 0)
    return NULL;

  for (i = 0; i < n; i++)
    {
      GstTagList *tags = NULL;

      g_signal_emit_by_name (G_OBJECT (pipeline), action_signal, i, &tags);

      ret = g_list_prepend (ret, tags);
    }

  return g_list_reverse (ret);
}

static gboolean
on_audio_changed_main_context (gpointer data)
{
  ClutterGstPlayback *self = CLUTTER_GST_PLAYBACK (data);
  ClutterGstPlaybackPrivate *priv = self->priv;

  free_tags_list (&priv->audio_streams);
  priv->audio_streams = get_tags (priv->pipeline, "n-audio", "get-audio-tags");

  CLUTTER_GST_NOTE (AUDIO_STREAM, "audio-streams changed");

  g_object_notify (G_OBJECT (self), "audio-streams");

  g_object_unref (self);

  return FALSE;
}

/* same explanation as for notify::volume's usage of g_idle_add() */
static void
on_audio_changed (GstElement         *pipeline,
                  ClutterGstPlayback *self)
{
  g_idle_add (on_audio_changed_main_context, g_object_ref (self));
}

static void
on_audio_tags_changed (GstElement         *pipeline,
                       gint                stream,
                       ClutterGstPlayback *self)
{
  gint current_stream;

  g_object_get (G_OBJECT (pipeline), "current-audio", &current_stream, NULL);

  if (current_stream != stream)
    return;

  g_idle_add (on_audio_changed_main_context, g_object_ref (self));
}

static gboolean
on_current_audio_changed_main_context (gpointer data)
{
  ClutterGstPlayback *self = CLUTTER_GST_PLAYBACK (data);

  CLUTTER_GST_NOTE (AUDIO_STREAM, "audio stream changed");
  g_object_notify (G_OBJECT (self), "audio-stream");

  g_object_unref (self);

  return FALSE;
}

static void
on_current_audio_changed (GstElement         *pipeline,
                          GParamSpec         *pspec,
                          ClutterGstPlayback *self)
{
  g_idle_add (on_current_audio_changed_main_context, g_object_ref (self));
}

static gboolean
on_text_changed_main_context (gpointer data)
{
  ClutterGstPlayback *self = CLUTTER_GST_PLAYBACK (data);
  ClutterGstPlaybackPrivate *priv = self->priv;

  free_tags_list (&priv->subtitle_tracks);
  priv->subtitle_tracks = get_tags (priv->pipeline, "n-text", "get-text-tags");

  CLUTTER_GST_NOTE (AUDIO_STREAM, "subtitle-tracks changed");

  g_object_notify (G_OBJECT (self), "subtitle-tracks");

  g_object_unref (self);

  return FALSE;
}

/* same explanation as for notify::volume's usage of g_idle_add() */
static void
on_text_changed (GstElement         *pipeline,
                 ClutterGstPlayback *self)
{
  g_idle_add (on_text_changed_main_context, g_object_ref (self));
}

static void
on_text_tags_changed (GstElement         *pipeline,
                      gint                stream,
                      ClutterGstPlayback *self)
{
  g_idle_add (on_text_changed_main_context, g_object_ref (self));
}

static gboolean
on_current_text_changed_main_context (gpointer data)
{
  ClutterGstPlayback *self = CLUTTER_GST_PLAYBACK (data);

  CLUTTER_GST_NOTE (AUDIO_STREAM, "text stream changed");
  g_object_notify (G_OBJECT (self), "subtitle-track");

  g_object_unref (self);

  return FALSE;
}

static void
on_current_text_changed (GstElement         *pipeline,
                         GParamSpec         *pspec,
                         ClutterGstPlayback *self)
{
  g_idle_add (on_current_text_changed_main_context, g_object_ref (self));
}

/**/

static ClutterGstFrame *
clutter_gst_playback_get_frame (ClutterGstPlayer *self)
{
  ClutterGstPlaybackPrivate *priv = CLUTTER_GST_PLAYBACK (self)->priv;

  return priv->current_frame;
}

static GstElement *
clutter_gst_playback_get_pipeline (ClutterGstPlayer *self)
{
  ClutterGstPlaybackPrivate *priv = CLUTTER_GST_PLAYBACK (self)->priv;

  return priv->pipeline;
}

static ClutterGstVideoSink *
clutter_gst_playback_get_video_sink (ClutterGstPlayer *self)
{
  ClutterGstPlaybackPrivate *priv = CLUTTER_GST_PLAYBACK (self)->priv;

  return priv->video_sink;
}

static gboolean
clutter_gst_playback_get_idle (ClutterGstPlayer *self)
{
  ClutterGstPlaybackPrivate *priv = CLUTTER_GST_PLAYBACK (self)->priv;

  return priv->is_idle;
}

static gdouble
clutter_gst_playback_get_audio_volume (ClutterGstPlayer *self)
{
  return get_audio_volume (CLUTTER_GST_PLAYBACK (self));
}

static void
clutter_gst_playback_set_audio_volume (ClutterGstPlayer *self,
                                       gdouble           volume)
{
  set_audio_volume (CLUTTER_GST_PLAYBACK (self), volume);
}

static gboolean
clutter_gst_playback_get_playing (ClutterGstPlayer *self)
{
  return get_playing (CLUTTER_GST_PLAYBACK (self));
}

static void
clutter_gst_playback_set_playing (ClutterGstPlayer *self,
                                  gboolean          playing)
{
  set_playing (CLUTTER_GST_PLAYBACK (self), playing);
}

static void
player_iface_init (ClutterGstPlayerIface *iface)
{
  iface->get_frame = clutter_gst_playback_get_frame;
  iface->get_pipeline = clutter_gst_playback_get_pipeline;
  iface->get_video_sink = clutter_gst_playback_get_video_sink;

  iface->get_idle = clutter_gst_playback_get_idle;

  iface->get_audio_volume = clutter_gst_playback_get_audio_volume;
  iface->set_audio_volume = clutter_gst_playback_set_audio_volume;

  iface->get_playing = clutter_gst_playback_get_playing;
  iface->set_playing = clutter_gst_playback_set_playing;
}

/**/

static void
clutter_gst_playback_get_property (GObject    *object,
                                   guint       property_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  ClutterGstPlayback *self = CLUTTER_GST_PLAYBACK (object);
  ClutterGstPlaybackPrivate *priv = self->priv;
  gchar *str;

  switch (property_id)
    {
    case PROP_URI:
      g_value_set_string (value, priv->uri);
      break;

    case PROP_PLAYING:
      g_value_set_boolean (value, get_playing (self));
      break;

    case PROP_PROGRESS:
      g_value_set_double (value, get_progress (self));
      break;

    case PROP_SUBTITLE_URI:
      g_object_get (priv->pipeline, "suburi", &str, NULL);
      g_value_take_string (value, str);
      break;

    case PROP_SUBTITLE_FONT_NAME:
      g_value_set_string (value, priv->font_name);
      break;

    case PROP_AUDIO_VOLUME:
      g_value_set_double (value, get_audio_volume (self));
      break;

    case PROP_CAN_SEEK:
      g_value_set_boolean (value, priv->can_seek);
      break;

    case PROP_BUFFER_FILL:
      g_value_set_double (value, priv->buffer_fill);
      break;

    case PROP_DURATION:
      g_value_set_double (value, priv->duration);
      break;

    case PROP_IDLE:
      g_value_set_boolean (value, priv->is_idle);
      break;

    case PROP_USER_AGENT:
      {
        gchar *user_agent;

        user_agent = clutter_gst_playback_get_user_agent (self);
        g_value_take_string (value, user_agent);
      }
      break;

    case PROP_SEEK_FLAGS:
      {
        ClutterGstSeekFlags seek_flags;

        seek_flags = clutter_gst_playback_get_seek_flags (self);
        g_value_set_flags (value, seek_flags);
      }
      break;

    case PROP_AUDIO_STREAMS:
      g_value_set_pointer (value, priv->audio_streams);
      break;

    case PROP_AUDIO_STREAM:
      {
        gint index_;

        index_ = clutter_gst_playback_get_audio_stream (self);
        g_value_set_int (value, index_);
      }
      break;

    case PROP_SUBTITLE_TRACKS:
      g_value_set_pointer (value, priv->subtitle_tracks);
      break;

    case PROP_SUBTITLE_TRACK:
      {
        gint index_;

        index_ = clutter_gst_playback_get_subtitle_track (self);
        g_value_set_int (value, index_);
      }
      break;

    case PROP_IN_SEEK:
      g_value_set_boolean (value, priv->in_seek);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
clutter_gst_playback_set_property (GObject      *object,
                                   guint         property_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  ClutterGstPlayback *self = CLUTTER_GST_PLAYBACK (object);

  switch (property_id)
    {
    case PROP_URI:
      set_uri (self, g_value_get_string (value));
      break;

    case PROP_PLAYING:
      set_playing (self, g_value_get_boolean (value));
      break;

    case PROP_PROGRESS:
      set_progress (self, g_value_get_double (value));
      break;

    case PROP_SUBTITLE_URI:
      set_subtitle_uri (self, g_value_get_string (value));
      break;

    case PROP_SUBTITLE_FONT_NAME:
      set_subtitle_font_name (self, g_value_get_string (value));
      break;

    case PROP_AUDIO_VOLUME:
      set_audio_volume (self, g_value_get_double (value));
      break;

    case PROP_USER_AGENT:
      clutter_gst_playback_set_user_agent (self,
                                           g_value_get_string (value));
      break;

    case PROP_SEEK_FLAGS:
      clutter_gst_playback_set_seek_flags (self,
                                           g_value_get_flags (value));
      break;

    case PROP_AUDIO_STREAM:
      clutter_gst_playback_set_audio_stream (self,
                                             g_value_get_int (value));
      break;

    case PROP_SUBTITLE_TRACK:
      clutter_gst_playback_set_subtitle_track (self,
                                               g_value_get_int (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
clutter_gst_playback_dispose (GObject *object)
{
  ClutterGstPlaybackPrivate *priv = CLUTTER_GST_PLAYBACK (object)->priv;
  guint i;

  if (priv->tick_timeout_id)
    {
      g_source_remove (priv->tick_timeout_id);
      priv->tick_timeout_id = 0;
    }

  if (priv->buffering_timeout_id)
    {
      g_source_remove (priv->buffering_timeout_id);
      priv->buffering_timeout_id = 0;
    }

  if (priv->bus)
    {
      for (i = 0; i < priv->gst_bus_sigs->len; i++)
        g_signal_handler_disconnect (priv->bus,
                                     g_array_index (priv->gst_bus_sigs, gulong, i));
      gst_bus_remove_signal_watch (priv->bus);
      priv->bus = NULL;
    }

  if (priv->pipeline)
    {
      for (i = 0; i < priv->gst_pipe_sigs->len; i++)
        g_signal_handler_disconnect (priv->pipeline,
                                     g_array_index (priv->gst_pipe_sigs, gulong, i));
      gst_element_set_state (priv->pipeline, GST_STATE_NULL);
      g_clear_object (&priv->pipeline);
    }

  if (priv->current_frame)
    {
      g_boxed_free (CLUTTER_GST_TYPE_FRAME, priv->current_frame);
      priv->current_frame = NULL;
    }

  g_free (priv->uri);
  g_free (priv->font_name);
  g_free (priv->user_agent);
  priv->uri = priv->font_name = priv->user_agent = NULL;
  free_tags_list (&priv->audio_streams);
  free_tags_list (&priv->subtitle_tracks);

  G_OBJECT_CLASS (clutter_gst_playback_parent_class)->dispose (object);
}

static void
clutter_gst_playback_finalize (GObject *object)
{
  G_OBJECT_CLASS (clutter_gst_playback_parent_class)->finalize (object);
}

static void
clutter_gst_playback_class_init (ClutterGstPlaybackClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *pspec;

  g_type_class_add_private (klass, sizeof (ClutterGstPlaybackPrivate));

  object_class->get_property = clutter_gst_playback_get_property;
  object_class->set_property = clutter_gst_playback_set_property;
  object_class->dispose = clutter_gst_playback_dispose;
  object_class->finalize = clutter_gst_playback_finalize;
  klass->should_buffer = player_should_buffer;

  /**
   * ClutterGstPlayback:uri:
   *
   * The location of a media file, expressed as a valid URI.
   */
  pspec = g_param_spec_string ("uri",
                               "URI",
                               "URI of a media file",
                               NULL,
                               CLUTTER_GST_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_URI, pspec);

  /**
   * ClutterGstPlayback:progress:
   *
   * The current progress of the playback, as a normalized
   * value between 0.0 and 1.0.
   */
  pspec = g_param_spec_double ("progress",
                               "Progress",
                               "Current progress of the playback",
                               0.0, 1.0, 0.0,
                               CLUTTER_GST_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_PROGRESS, pspec);

  /**
   * ClutterGstPlayback:subtitle-uri:
   *
   * The location of a subtitle file, expressed as a valid URI.
   */
  pspec = g_param_spec_string ("subtitle-uri",
                               "Subtitle URI",
                               "URI of a subtitle file",
                               NULL,
                               CLUTTER_GST_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_SUBTITLE_URI, pspec);

  /**
   * ClutterGstPlayback:subtitle-font-name:
   *
   * The font used to display subtitles. The font description has to
   * follow the same grammar as the one recognized by
   * pango_font_description_from_string().
   */
  pspec = g_param_spec_string ("subtitle-font-name",
                               "Subtitle Font Name",
                               "The font used to display subtitles",
                               NULL,
                               CLUTTER_GST_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_SUBTITLE_FONT_NAME, pspec);

  /**
   * ClutterGstPlayback:can-seek:
   *
   * Whether the current stream is seekable.
   */
  pspec = g_param_spec_boolean ("can-seek",
                                "Can Seek",
                                "Whether the current stream is seekable",
                                FALSE,
                                CLUTTER_GST_PARAM_READABLE);
  g_object_class_install_property (object_class, PROP_CAN_SEEK, pspec);

  /**
   * ClutterGstPlayback:buffer-fill:
   *
   * The fill level of the buffer for the current stream,
   * as a value between 0.0 and 1.0.
   */
  pspec = g_param_spec_double ("buffer-fill",
                               "Buffer Fill",
                               "The fill level of the buffer",
                               0.0, 1.0, 0.0,
                               CLUTTER_GST_PARAM_READABLE);
  g_object_class_install_property (object_class, PROP_BUFFER_FILL, pspec);

  /**
   * ClutterGstPlayback:duration:
   *
   * The duration of the current stream, in seconds
   */
  pspec = g_param_spec_double ("duration",
                               "Duration",
                               "The duration of the stream, in seconds",
                               0, G_MAXDOUBLE, 0,
                               CLUTTER_GST_PARAM_READABLE);
  g_object_class_install_property (object_class, PROP_DURATION, pspec);

  /**
   * ClutterGstPlayback:user-agent:
   *
   * The User Agent used by #ClutterGstPlayback with network protocols.
   *
   * Since: 1.4
   */
  pspec = g_param_spec_string ("user-agent",
                               "User Agent",
                               "User Agent used with network protocols",
                               NULL,
                               CLUTTER_GST_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_USER_AGENT, pspec);

  /**
   * ClutterGstPlayback:seek-flags:
   *
   * Flags to use when seeking.
   *
   * Since: 1.4
   */
  pspec = g_param_spec_flags ("seek-flags",
                              "Seek Flags",
                              "Flags to use when seeking",
                              CLUTTER_GST_TYPE_SEEK_FLAGS,
                              CLUTTER_GST_SEEK_FLAG_NONE,
                              CLUTTER_GST_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_SEEK_FLAGS, pspec);

  /**
   * ClutterGstPlayback:audio-streams:
   *
   * List of audio streams available on the current media.
   *
   * Since: 1.4
   */
  pspec = g_param_spec_pointer ("audio-streams",
                                "Audio Streams",
                                "List of the audio streams of the media",
                                CLUTTER_GST_PARAM_READABLE);
  g_object_class_install_property (object_class, PROP_AUDIO_STREAMS, pspec);

  /**
   * ClutterGstPlayback:audio-stream:
   *
   * Index of the current audio stream.
   *
   * Since: 1.4
   */
  pspec = g_param_spec_int ("audio-stream",
                            "Audio Stream",
                            "Index of the current audio stream",
                            -1, G_MAXINT, -1,
                            CLUTTER_GST_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_AUDIO_STREAM, pspec);

  /**
   * ClutterGstPlayback:subtitle-tracks:
   *
   * List of subtitle tracks available.
   *
   * Since: 1.4
   */
  pspec = g_param_spec_pointer ("subtitle-tracks",
                                "Subtitles Tracks",
                                "List of the subtitles tracks of the media",
                                CLUTTER_GST_PARAM_READABLE);
  g_object_class_install_property (object_class, PROP_SUBTITLE_TRACKS, pspec);

  /**
   * ClutterGstPlayback:subtitle-track:
   *
   * Current subtitle track being displayed.
   *
   * Since: 1.4
   */
  pspec = g_param_spec_int ("subtitle-track",
                            "Subtitle Track",
                            "Index of the current subtitles track",
                            -1, G_MAXINT, -1,
                            CLUTTER_GST_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_SUBTITLE_TRACK, pspec);

  /**
   * ClutterGstPlayback:in-seek:
   *
   * Whether or not the stream is being seeked.
   *
   * Since: 1.6
   */
  pspec = g_param_spec_boolean ("in-seek",
                                "In seek mode",
                                "If currently seeking",
                                FALSE,
                                CLUTTER_GST_PARAM_READABLE);
  g_object_class_install_property (object_class, PROP_IN_SEEK, pspec);


  g_object_class_override_property (object_class,
                                    PROP_IDLE, "idle");
  g_object_class_override_property (object_class,
                                    PROP_PLAYING, "playing");
  g_object_class_override_property (object_class,
                                    PROP_AUDIO_VOLUME, "audio-volume");


  /* Signals */

  /**
   * ClutterGstPlayback::should-buffer:
   * @player: the #ClutterGstPlayback instance that received the signal
   * @query: A gst buffering query of format bytes
   *
   * The ::should-buffer signal is emitted every time the base class needs to
   * decide whether it should continue buffering in download-buffering mode.
   *
   * Since: 1.4
   */
  signals[SHOULD_BUFFER] =
    g_signal_new ("should-buffer",
                  CLUTTER_GST_TYPE_PLAYBACK,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterGstPlaybackClass,
                                   should_buffer),
                  g_signal_accumulator_first_wins, NULL,
                  _clutter_gst_marshal_BOOL__OBJECT,
                  G_TYPE_BOOLEAN, 1, GST_TYPE_QUERY);
}

static void
_new_frame_from_pipeline (ClutterGstVideoSink *sink, ClutterGstPlayback *self)
{
  ClutterGstPlaybackPrivate *priv = self->priv;

  clutter_gst_player_update_frame (CLUTTER_GST_PLAYER (self),
                                   &priv->current_frame,
                                   clutter_gst_video_sink_get_frame (sink));
}

static void
_ready_from_pipeline (ClutterGstVideoSink *sink, ClutterGstPlayback *self)
{
  g_signal_emit_by_name (self, "ready");
}

static void
_pixel_aspect_ratio_changed (ClutterGstVideoSink   *sink,
                             GParamSpec            *spec,
                             ClutterGstPlayback    *self)
{
  clutter_gst_frame_update_pixel_aspect_ratio (self->priv->current_frame, sink);
}

static GstElement *
get_pipeline (ClutterGstPlayback *self)
{
  ClutterGstPlaybackPrivate *priv = self->priv;
  GstElement *pipeline;

  pipeline = gst_element_factory_make ("playbin", "pipeline");
  if (!pipeline)
    {
      g_critical ("Unable to create playbin element");
      return NULL;
    }

  priv->video_sink = clutter_gst_video_sink_new ();

  g_signal_connect (priv->video_sink, "new-frame",
                    G_CALLBACK (_new_frame_from_pipeline), self);
  g_signal_connect (priv->video_sink, "pipeline-ready",
                    G_CALLBACK (_ready_from_pipeline), self);
  g_signal_connect (priv->video_sink, "notify::pixel-aspect-ratio",
                    G_CALLBACK (_pixel_aspect_ratio_changed), self);

  g_object_set (G_OBJECT (pipeline),
                "video-sink", priv->video_sink,
                "subtitle-font-desc", "Sans 16",
                NULL);

  return pipeline;
}

#define connect_signal_custom(store, object, signal, callback, data) \
  do {                                                               \
    gulong s = g_signal_connect (object, signal, callback, data);    \
    g_array_append_val (store, s);                                   \
  } while (0)

#define connect_object_custom(store, object, signal, callback, data, flags)   \
  do {                                                                  \
    gulong s = g_signal_connect_object (object, signal, callback, data, flags); \
    g_array_append_val (store, s);                                      \
  } while (0)

static void
clutter_gst_playback_init (ClutterGstPlayback *self)
{
  ClutterGstPlaybackPrivate *priv;

  self->priv = priv = GST_PLAYBACK_PRIVATE (self);

  priv->gst_pipe_sigs = g_array_new (FALSE, FALSE, sizeof (gulong));
  priv->gst_bus_sigs = g_array_new (FALSE, FALSE, sizeof (gulong));

  priv->is_idle = TRUE;
  priv->in_seek = FALSE;
  priv->is_changing_uri = FALSE;
  priv->in_download_buffering = FALSE;

  priv->pipeline = get_pipeline (self);
  g_assert (priv->pipeline != NULL);

  priv->current_frame = clutter_gst_create_blank_frame (NULL);

  connect_signal_custom (priv->gst_pipe_sigs, priv->pipeline,
                         "notify::source",
                         G_CALLBACK (on_source_changed), self);

  /* We default to not playing until someone calls set_playing(TRUE) */
  priv->target_state = GST_STATE_PAUSED;

  /* Default to a fast seek, ie. same effect than set_seek_flags (NONE); */
  priv->seek_flags = GST_SEEK_FLAG_KEY_UNIT;

  priv->bus = gst_pipeline_get_bus (GST_PIPELINE (priv->pipeline));

  gst_bus_add_signal_watch (priv->bus);

  connect_object_custom (priv->gst_bus_sigs,
                         priv->bus, "message::error",
                         G_CALLBACK (bus_message_error_cb),
                         self, 0);
  connect_object_custom (priv->gst_bus_sigs,
                         priv->bus, "message::eos",
                         G_CALLBACK (bus_message_eos_cb),
                         self, 0);
  connect_object_custom (priv->gst_bus_sigs,
                         priv->bus, "message::buffering",
                         G_CALLBACK (bus_message_buffering_cb),
                         self, 0);
  connect_object_custom (priv->gst_bus_sigs,
                         priv->bus, "message::duration-changed",
                         G_CALLBACK (bus_message_duration_changed_cb),
                         self, 0);
  connect_object_custom (priv->gst_bus_sigs,
                         priv->bus, "message::state-changed",
                         G_CALLBACK (bus_message_state_change_cb),
                         self, 0);
  connect_object_custom (priv->gst_bus_sigs,
                         priv->bus, "message::async-done",
                         G_CALLBACK (bus_message_async_done_cb),
                         self, 0);


  connect_signal_custom (priv->gst_pipe_sigs,
                         priv->pipeline, "notify::volume",
                         G_CALLBACK (on_volume_changed),
                         self);

  connect_signal_custom (priv->gst_pipe_sigs,
                         priv->pipeline, "audio-changed",
                         G_CALLBACK (on_audio_changed),
                         self);
  connect_signal_custom (priv->gst_pipe_sigs,
                         priv->pipeline, "audio-tags-changed",
                         G_CALLBACK (on_audio_tags_changed),
                         self);
  connect_signal_custom (priv->gst_pipe_sigs,
                         priv->pipeline, "notify::current-audio",
                         G_CALLBACK (on_current_audio_changed),
                         self);

  connect_signal_custom (priv->gst_pipe_sigs,
                         priv->pipeline, "text-changed",
                         G_CALLBACK (on_text_changed),
                         self);
  connect_signal_custom (priv->gst_pipe_sigs,
                         priv->pipeline, "text-tags-changed",
                         G_CALLBACK (on_text_tags_changed),
                         self);
  connect_signal_custom (priv->gst_pipe_sigs,
                         priv->pipeline, "notify::current-text",
                         G_CALLBACK (on_current_text_changed),
                         self);

#if defined(CLUTTER_WINDOWING_X11) && defined (HAVE_HW_DECODER_SUPPORT)
  if (clutter_check_windowing_backend (CLUTTER_WINDOWING_X11))
    gst_bus_set_sync_handler (priv->bus, on_sync_message,
        clutter_x11_get_default_display (), NULL);
#endif

  gst_object_unref (GST_OBJECT (priv->bus));
}

ClutterGstPlayback *
clutter_gst_playback_new (void)
{
  return g_object_new (CLUTTER_GST_TYPE_PLAYBACK, NULL);
}

/**
 * clutter_gst_playback_set_uri:
 * @self: a #ClutterGstPlayback
 * @uri: the URI of the media stream
 *
 * Sets the URI of @self to @uri.
 */
void
clutter_gst_playback_set_uri (ClutterGstPlayback *self,
                              const gchar        *uri)
{
  g_return_if_fail (CLUTTER_GST_IS_PLAYBACK (self));

  g_object_set (G_OBJECT (self), "uri", uri, NULL);
}

/**
 * clutter_gst_playback_get_uri:
 * @self: a #ClutterGstPlayback
 *
 * Retrieves the URI from @self.
 *
 * Return value: the URI of the media stream. Use g_free()
 *   to free the returned string
 */
gchar *
clutter_gst_playback_get_uri (ClutterGstPlayback *self)
{
  gchar *retval = NULL;

  g_return_val_if_fail (CLUTTER_GST_IS_PLAYBACK (self), NULL);

  g_object_get (G_OBJECT (self), "uri", &retval, NULL);

  return retval;
}

/**
 * clutter_gst_playback_set_filename:
 * @self: a #ClutterGstPlayback
 * @filename: A filename
 *
 * Sets the source of @self using a file path.
 */
void
clutter_gst_playback_set_filename (ClutterGstPlayback *self,
                                 const gchar      *filename)
{
  gchar *uri;
  GError *uri_error = NULL;

  if (!g_path_is_absolute (filename))
    {
      gchar *abs_path;

      abs_path = g_build_filename (g_get_current_dir (), filename, NULL);
      uri = g_filename_to_uri (abs_path, NULL, &uri_error);
      g_free (abs_path);
    }
  else
    uri = g_filename_to_uri (filename, NULL, &uri_error);

  if (uri_error)
    {
      g_signal_emit_by_name (self, "error", uri_error);
      g_error_free (uri_error);
      return;
    }

  clutter_gst_playback_set_uri (self, uri);

  g_free (uri);
}

/**
 * clutter_gst_playback_get_user_agent:
 * @self: a #ClutterGstPlayback
 *
 * Retrieves the user agent used when streaming.
 *
 * Return value: the user agent used. The returned string has to be freed with
 * g_free()
 *
 * Since: 1.4
 */
gchar *
clutter_gst_playback_get_user_agent (ClutterGstPlayback *self)
{
  ClutterGstPlaybackPrivate *priv;
  GstElement *source;
  GParamSpec *pspec;
  gchar *user_agent;

  g_return_val_if_fail (CLUTTER_GST_IS_PLAYBACK (self), NULL);

  priv = self->priv;

  /* If the user has set a custom user agent, we just return it even if it is
   * not used by the current source element of the pipeline */
  if (priv->user_agent)
    return g_strdup (priv->user_agent);

  /* If not, we try to retrieve the user agent used by the current source */
  g_object_get (priv->pipeline, "source", &source, NULL);
  if (source == NULL)
    return NULL;

  pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (source),
                                        "user-agent");
  if (pspec == NULL)
    return NULL;

  g_object_get (source, "user-agent", &user_agent, NULL);

  return user_agent;
}

/**
 * clutter_gst_playback_set_user_agent:
 * @self: a #ClutterGstPlayback
 * @user_agent: the user agent
 *
 * Sets the user agent to use when streaming.
 *
 * When streaming content, you might want to set a custom user agent, eg. to
 * promote your software, make it appear in statistics or because the server
 * requires a special user agent you want to impersonate.
 *
 * Since: 1.4
 */
void
clutter_gst_playback_set_user_agent (ClutterGstPlayback *self,
                                     const gchar        *user_agent)
{
  ClutterGstPlaybackPrivate *priv;

  g_return_if_fail (CLUTTER_GST_IS_PLAYBACK (self));

  priv = self->priv;

  g_free (priv->user_agent);
  if (user_agent)
    priv->user_agent = g_strdup (user_agent);
  else
    priv->user_agent = NULL;

  player_set_user_agent (self, user_agent);
}

/**
 * clutter_gst_playback_get_seek_flags:
 * @self: a #ClutterGstPlayback
 *
 * Get the current value of the seek-flags property.
 *
 * Return value: a combination of #ClutterGstSeekFlags
 *
 * Since: 1.4
 */
ClutterGstSeekFlags
clutter_gst_playback_get_seek_flags (ClutterGstPlayback *self)
{
  ClutterGstPlaybackPrivate *priv;

  g_return_val_if_fail (CLUTTER_GST_IS_PLAYBACK (self),
                        CLUTTER_GST_SEEK_FLAG_NONE);

  priv = self->priv;

  if (priv->seek_flags == GST_SEEK_FLAG_ACCURATE)
    return CLUTTER_GST_SEEK_FLAG_ACCURATE;
  else
    return CLUTTER_GST_SEEK_FLAG_NONE;
}

/**
 * clutter_gst_playback_set_seek_flags:
 * @self: a #ClutterGstPlayback
 * @flags: a combination of #ClutterGstSeekFlags
 *
 * Seeking can be done with several trade-offs. Clutter-gst defaults
 * to %CLUTTER_GST_SEEK_FLAG_NONE.
 *
 * Since: 1.4
 */
void
clutter_gst_playback_set_seek_flags (ClutterGstPlayback  *self,
                                     ClutterGstSeekFlags  flags)
{
  ClutterGstPlaybackPrivate *priv;

  g_return_if_fail (CLUTTER_GST_IS_PLAYBACK (self));

  priv = self->priv;

  if (flags == CLUTTER_GST_SEEK_FLAG_NONE)
    priv->seek_flags = GST_SEEK_FLAG_KEY_UNIT;
  else if (flags & CLUTTER_GST_SEEK_FLAG_ACCURATE)
    priv->seek_flags = GST_SEEK_FLAG_ACCURATE;
}

/**
 * clutter_gst_playback_get_buffering_mode:
 * @self: a #ClutterGstPlayback
 *
 * Return value: a #ClutterGstBufferingMode
 *
 * Since: 1.4
 */
ClutterGstBufferingMode
clutter_gst_playback_get_buffering_mode (ClutterGstPlayback *self)
{
  ClutterGstPlaybackPrivate *priv;
  GstPlayFlags flags;

  g_return_val_if_fail (CLUTTER_GST_IS_PLAYBACK (self),
                        CLUTTER_GST_BUFFERING_MODE_STREAM);

  priv = self->priv;

  g_object_get (G_OBJECT (priv->pipeline), "flags", &flags, NULL);

  if (flags & GST_PLAY_FLAG_DOWNLOAD)
    return CLUTTER_GST_BUFFERING_MODE_DOWNLOAD;

  return CLUTTER_GST_BUFFERING_MODE_STREAM;
}

/**
 * clutter_gst_playback_set_buffering_mode:
 * @self: a #ClutterGstPlayback
 * @mode: a #ClutterGstBufferingMode
 *
 * Since: 1.4
 */
void
clutter_gst_playback_set_buffering_mode (ClutterGstPlayback      *self,
                                         ClutterGstBufferingMode  mode)
{
  ClutterGstPlaybackPrivate *priv;
  GstPlayFlags flags;

  g_return_if_fail (CLUTTER_GST_IS_PLAYBACK (self));

  priv = self->priv;

  g_object_get (G_OBJECT (priv->pipeline), "flags", &flags, NULL);

  switch (mode)
    {
    case CLUTTER_GST_BUFFERING_MODE_STREAM:
      flags &= ~GST_PLAY_FLAG_DOWNLOAD;
      break;

    case CLUTTER_GST_BUFFERING_MODE_DOWNLOAD:
      flags |= GST_PLAY_FLAG_DOWNLOAD;
      break;

    default:
      g_warning ("Unexpected buffering mode %d", mode);
      break;
    }

  g_object_set (G_OBJECT (priv->pipeline), "flags", flags, NULL);
}

/**
 * clutter_gst_playback_get_buffer_fill:
 * @self: a #ClutterGstPlayback
 *
 * Retrieves the amount of the stream that is buffered.
 *
 * Return value: the fill level, between 0.0 and 1.0
 */
gdouble
clutter_gst_playback_get_buffer_fill (ClutterGstPlayback *self)
{
  gdouble retval = 0.0;

  g_return_val_if_fail (CLUTTER_GST_IS_PLAYBACK (self), 0);

  g_object_get (G_OBJECT (self), "buffer-fill", &retval, NULL);

  return retval;
}

/**
 * clutter_gst_playback_get_buffer_size:
 * @self: a #ClutterGstPlayback
 *
 * Retrieves the buffer size when buffering network streams.
 *
 * Return value: The buffer size
 */
gint
clutter_gst_playback_get_buffer_size (ClutterGstPlayback *self)
{
  ClutterGstPlaybackPrivate *priv;
  gint size;

  g_return_val_if_fail (CLUTTER_GST_IS_PLAYBACK (self), 0);

  priv = self->priv;

  g_object_get (G_OBJECT (priv->pipeline), "buffer-size", &size, NULL);

  return size;
}

/**
 * clutter_gst_playback_set_buffer_size:
 * @self: a #ClutterGstPlayback
 * @size: The new size
 *
 * Sets the buffer size to be used when buffering network streams.
 */
void
clutter_gst_playback_set_buffer_size (ClutterGstPlayback *self,
                                      gint                size)
{
  ClutterGstPlaybackPrivate *priv;

  g_return_if_fail (CLUTTER_GST_IS_PLAYBACK (self));

  priv = self->priv;

  g_object_set (G_OBJECT (priv->pipeline), "buffer-size", size, NULL);
}

/**
 * clutter_gst_playback_get_buffer_duration:
 * @self: a #ClutterGstPlayback
 *
 * Retrieves the buffer duration when buffering network streams.
 *
 * Return value: The buffer duration
 */
gint64
clutter_gst_playback_get_buffer_duration (ClutterGstPlayback *self)
{
  ClutterGstPlaybackPrivate *priv;
  gint64 duration;

  g_return_val_if_fail (CLUTTER_GST_IS_PLAYBACK (self), 0);

  priv = self->priv;

  g_object_get (G_OBJECT (priv->pipeline), "buffer-duration", &duration, NULL);

  return duration;
}

/**
 * clutter_gst_playback_set_buffer_duration:
 * @self: a #ClutterGstPlayback
 * @duration: The new duration
 *
 * Sets the buffer duration to be used when buffering network streams.
 */
void
clutter_gst_playback_set_buffer_duration (ClutterGstPlayback *self,
                                          gint64              duration)
{
  ClutterGstPlaybackPrivate *priv;

  g_return_if_fail (CLUTTER_GST_IS_PLAYBACK (self));

  priv = self->priv;

  g_object_set (G_OBJECT (priv->pipeline), "buffer-duration", duration, NULL);
}

/**
 * clutter_gst_playback_get_audio_streams:
 * @self: a #ClutterGstPlayback
 *
 * Get the list of audio streams of the current media.
 *
 * Return value: (transfer none) (element-type utf8): a list of
 * strings describing the available audio streams
 *
 * Since: 1.4
 */
GList *
clutter_gst_playback_get_audio_streams (ClutterGstPlayback *self)
{
  ClutterGstPlaybackPrivate *priv;

  g_return_val_if_fail (CLUTTER_GST_IS_PLAYBACK (self), NULL);

  priv = self->priv;

  if (CLUTTER_GST_DEBUG_ENABLED (AUDIO_STREAM))
    {
      gchar *streams;

      streams = list_to_string (priv->audio_streams);
      CLUTTER_GST_NOTE (AUDIO_STREAM, "audio streams: %s", streams);
      g_free (streams);
    }

  return priv->audio_streams;
}

/**
 * clutter_gst_playback_get_audio_stream:
 * @self: a #ClutterGstPlayback
 *
 * Get the current audio stream. The number returned in the index of the
 * audio stream playing in the list returned by
 * clutter_gst_playback_get_audio_streams().
 *
 * Return value: the index of the current audio stream, -1 if the media has no
 * audio stream
 *
 * Since: 1.4
 */
gint
clutter_gst_playback_get_audio_stream (ClutterGstPlayback *self)
{
  ClutterGstPlaybackPrivate *priv;
  gint index_ = -1;

  g_return_val_if_fail (CLUTTER_GST_IS_PLAYBACK (self), -1);

  priv = self->priv;

  g_object_get (G_OBJECT (priv->pipeline),
                "current-audio", &index_,
                NULL);

  CLUTTER_GST_NOTE (AUDIO_STREAM, "audio stream is #%d", index_);

  return index_;
}

/**
 * clutter_gst_playback_set_audio_stream:
 * @self: a #ClutterGstPlayback
 * @index_: the index of the audio stream
 *
 * Set the audio stream to play. @index_ is the index of the stream
 * in the list returned by clutter_gst_playback_get_audio_streams().
 *
 * Since: 1.4
 */
void
clutter_gst_playback_set_audio_stream (ClutterGstPlayback *self,
                                       gint                index_)
{
  ClutterGstPlaybackPrivate *priv;

  g_return_if_fail (CLUTTER_GST_IS_PLAYBACK (self));

  priv = self->priv;

  g_return_if_fail (index_ >= 0 &&
                    index_ < (gint) g_list_length (priv->audio_streams));

  CLUTTER_GST_NOTE (AUDIO_STREAM, "set audio audio stream to #%d", index_);

  g_object_set (G_OBJECT (priv->pipeline),
                "current-audio", index_,
                NULL);
}

/**
 * clutter_gst_playback_set_subtitle_uri:
 * @self: a #ClutterGstPlayback
 * @uri: the URI of a subtitle file
 *
 * Sets the location of a subtitle file to display while playing @self.
 */
void
clutter_gst_playback_set_subtitle_uri (ClutterGstPlayback *self,
                                       const char         *uri)
{
  g_return_if_fail (CLUTTER_GST_IS_PLAYBACK (self));

  g_object_set (G_OBJECT (self), "subtitle-uri", uri, NULL);
}

/**
 * clutter_gst_playback_get_subtitle_uri:
 * @self: a #ClutterGstPlayback
 *
 * Retrieves the URI of the subtitle file in use.
 *
 * Return value: the URI of the subtitle file. Use g_free()
 *   to free the returned string
 */
gchar *
clutter_gst_playback_get_subtitle_uri (ClutterGstPlayback *self)
{
  gchar *retval = NULL;

  g_return_val_if_fail (CLUTTER_GST_IS_PLAYBACK (self), NULL);

  g_object_get (G_OBJECT (self), "subtitle-uri", &retval, NULL);

  return retval;
}

/**
 * clutter_gst_playback_set_subtitle_font_name:
 * @self: a #ClutterGstPlayback
 * @font_name: a font name, or %NULL to set the default font name
 *
 * Sets the font used by the subtitle renderer. The @font_name string must be
 * either %NULL, which means that the default font name of the underlying
 * implementation will be used; or must follow the grammar recognized by
 * pango_font_description_from_string() like:
 *
 * |[
 *   clutter_gst_playback_set_subtitle_font_name (player, "Sans 24pt");
 * ]|
 */
void
clutter_gst_playback_set_subtitle_font_name (ClutterGstPlayback *self,
                                             const char         *font_name)
{
  g_return_if_fail (CLUTTER_GST_IS_PLAYBACK (self));

  g_object_set (G_OBJECT (self), "subtitle-font-name", font_name, NULL);
}

/**
 * clutter_gst_playback_get_subtitle_font_name:
 * @self: a #ClutterGstPlayback
 *
 * Retrieves the font name currently used.
 *
 * Return value: a string containing the font name. Use g_free()
 *   to free the returned string
 */
gchar *
clutter_gst_playback_get_subtitle_font_name (ClutterGstPlayback *self)
{
  gchar *retval = NULL;

  g_return_val_if_fail (CLUTTER_GST_IS_PLAYBACK (self), NULL);

  g_object_get (G_OBJECT (self), "subtitle-font-name", &retval, NULL);

  return retval;
}

/**
 * clutter_gst_playback_get_subtitle_tracks:
 * @self: a #ClutterGstPlayback
 *
 * Get the list of subtitles tracks of the current media.
 *
 * Return value: (transfer none) (element-type utf8): a list of
 * strings describing the available subtitles tracks
 *
 * Since: 1.4
 */
GList *
clutter_gst_playback_get_subtitle_tracks (ClutterGstPlayback *self)
{
  ClutterGstPlaybackPrivate *priv;

  g_return_val_if_fail (CLUTTER_GST_IS_PLAYBACK (self), NULL);

  priv = self->priv;

  if (CLUTTER_GST_DEBUG_ENABLED (SUBTITLES))
    {
      gchar *tracks;

      tracks = list_to_string (priv->subtitle_tracks);
      CLUTTER_GST_NOTE (SUBTITLES, "subtitle tracks: %s", tracks);
      g_free (tracks);
    }

  return priv->subtitle_tracks;
}

/**
 * clutter_gst_playback_get_subtitle_track:
 * @self: a #ClutterGstPlayback
 *
 * Get the current subtitles track. The number returned is the index of the
 * subtiles track in the list returned by
 * clutter_gst_playback_get_subtitle_tracks().
 *
 * Return value: the index of the current subtitlest track, -1 if the media has
 * no subtitles track or if the subtitles have been turned off
 *
 * Since: 1.4
 */
gint
clutter_gst_playback_get_subtitle_track (ClutterGstPlayback *self)
{
  ClutterGstPlaybackPrivate *priv;
  gint index_ = -1;

  g_return_val_if_fail (CLUTTER_GST_IS_PLAYBACK (self), -1);

  priv = self->priv;

  g_object_get (G_OBJECT (priv->pipeline),
                "current-text", &index_,
                NULL);

  CLUTTER_GST_NOTE (SUBTITLES, "text track is #%d", index_);

  return index_;
}

/**
 * clutter_gst_playback_set_subtitle_track:
 * @self: a #ClutterGstPlayback
 * @index_: the index of the subtitles track
 *
 * Set the subtitles track to play. @index_ is the index of the stream
 * in the list returned by clutter_gst_playback_get_subtitle_tracks().
 *
 * If @index_ is -1, the subtitles are turned off.
 *
 * Since: 1.4
 */
void
clutter_gst_playback_set_subtitle_track (ClutterGstPlayback *self,
                                         gint                index_)
{
  ClutterGstPlaybackPrivate *priv;
  GstPlayFlags flags;

  g_return_if_fail (CLUTTER_GST_IS_PLAYBACK (self));

  priv = self->priv;

  g_return_if_fail (index_ >= -1 &&
                    index_ < (gint) g_list_length (priv->subtitle_tracks));

  CLUTTER_GST_NOTE (SUBTITLES, "set subtitle track to #%d", index_);

  g_object_get (priv->pipeline, "flags", &flags, NULL);
  flags &= ~GST_PLAY_FLAG_TEXT;
  g_object_set (priv->pipeline, "flags", flags, NULL);

  if (index_ >= 0)
    {
      g_object_set (G_OBJECT (priv->pipeline),
                    "current-text", index_,
                    NULL);

      flags |= GST_PLAY_FLAG_TEXT;
      g_object_set (priv->pipeline, "flags", flags, NULL);
    }
}

/**
 * clutter_gst_playback_get_in_seek:
 * @self: a #ClutterGstPlayback
 *
 * Whether the player is seeking.
 *
 * Return value: TRUE if the player is seeking, FALSE otherwise.
 *
 * Since: 1.6
 */
gboolean
clutter_gst_playback_get_in_seek (ClutterGstPlayback *self)
{
  g_return_val_if_fail (CLUTTER_GST_IS_PLAYBACK (self), FALSE);

  return self->priv->in_seek;
}

/**
 * clutter_gst_playback_get_can_seek:
 * @self: a #ClutterGstPlayback
 *
 * Retrieves whether @self is seekable or not.
 *
 * Return value: %TRUE if @self can seek, %FALSE otherwise.
 */
gboolean
clutter_gst_playback_get_can_seek (ClutterGstPlayback *self)
{
  g_return_val_if_fail (CLUTTER_GST_IS_PLAYBACK (self), FALSE);

  return self->priv->can_seek;
}

/**
 * clutter_gst_playback_set_progress:
 * @self: a #ClutterGstPlayback
 * @progress: the progress of the playback, between 0.0 and 1.0
 *
 * Sets the playback progress of @self. The @progress is
 * a normalized value between 0.0 (begin) and 1.0 (end).
 */
void
clutter_gst_playback_set_progress (ClutterGstPlayback *self,
                                   gdouble             progress)
{
  g_return_if_fail (CLUTTER_GST_IS_PLAYBACK (self));

  set_progress (self, progress);
}

/**
 * clutter_gst_playback_get_progress:
 * @self: a #ClutterGstPlayback
 *
 * Retrieves the playback progress of @self.
 *
 * Return value: the playback progress, between 0.0 and 1.0
 */
gdouble
clutter_gst_playback_get_progress (ClutterGstPlayback *self)
{
  g_return_val_if_fail (CLUTTER_GST_IS_PLAYBACK (self), 0);

  return get_progress (self);
}
/**
 * clutter_gst_playback_get_position:
 * @self: a #ClutterGstPlayback
 *
 * Retrieves the position in the media stream that @self represents.
 *
 * Return value: the position in the media stream, in seconds
 */
gdouble
clutter_gst_playback_get_position (ClutterGstPlayback *self)
{
  g_return_val_if_fail (CLUTTER_GST_IS_PLAYBACK (self), 0);

  return get_position (self);
}

/**
 * clutter_gst_playback_get_duration:
 * @self: a #ClutterGstPlayback
 *
 * Retrieves the duration of the media stream that @self represents.
 *
 * Return value: the duration of the media stream, in seconds
 */
gdouble
clutter_gst_playback_get_duration (ClutterGstPlayback *self)
{
  gdouble retval = 0;

  g_return_val_if_fail (CLUTTER_GST_IS_PLAYBACK (self), 0);

  g_object_get (G_OBJECT (self), "duration", &retval, NULL);

  return retval;
}

/**
 * clutter_gst_playback_is_live_media:
 * @self: a #ClutterGstPlayback
 *
 * Whether the player is using a live media.
 *
 * Return value: TRUE if the player is using a live media, FALSE otherwise.
 */
gboolean
clutter_gst_playback_is_live_media (ClutterGstPlayback *self)
{
  g_return_val_if_fail (CLUTTER_GST_IS_PLAYBACK (self), FALSE);

  return self->priv->is_live;
}
