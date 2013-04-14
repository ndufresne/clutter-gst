/*
 * Clutter-GStreamer.
 *
 * GStreamer integration library for Clutter.
 *
 * clutter-gst-aspectratio.c - An actor rendering a video with respect
 * to its aspect ratio.
 *
 * Authored by Lionel Landwerlin <lionel.g.landwerlin@linux.intel.com>
 *
 * Copyright (C) 2013 Intel Corporation
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "clutter-gst-debug.h"
#include "clutter-gst-enum-types.h"
#include "clutter-gst-pipeline.h"
#include "clutter-gst-player.h"
#include "clutter-gst-private.h"

static void player_iface_init (ClutterGstPlayerIface *iface);

void clutter_gst_pipeline_set_video_sink_internal (ClutterGstPipeline *self,
                                                   CoglGstVideoSink   *sink);

G_DEFINE_TYPE_WITH_CODE (ClutterGstPipeline, clutter_gst_pipeline, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (CLUTTER_GST_TYPE_PLAYER, player_iface_init))

#define PIPELINE_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), CLUTTER_GST_TYPE_PIPELINE, ClutterGstPipelinePrivate))

enum
{
  PROP_0,

  PROP_PLAYING,
  PROP_IDLE,
  PROP_AUDIO_VOLUME,
  PROP_VIDEO_SINK
};

struct _ClutterGstPipelinePrivate
{
  CoglGstVideoSink *sink;
  GstElement *pipeline;

  ClutterGstFrame *current_frame;
};

/**/

static gboolean
get_playing (ClutterGstPipeline *player)
{
  ClutterGstPipelinePrivate *priv = player->priv;
  GstState state, pending;
  gboolean playing;

  if (!priv->pipeline)
    return FALSE;

  gst_element_get_state (priv->pipeline, &state, &pending, 0);

  if (pending)
    playing = (pending == GST_STATE_PLAYING);
  else
    playing = (state == GST_STATE_PLAYING);

  CLUTTER_GST_NOTE (MEDIA, "get playing: %d", playing);

  return playing;
}

/**/

static ClutterGstFrame *
clutter_gst_pipeline_get_frame (ClutterGstPlayer *self)
{
  ClutterGstPipelinePrivate *priv = CLUTTER_GST_PIPELINE (self)->priv;

  return priv->current_frame;
}

static GstElement *
clutter_gst_pipeline_get_pipeline (ClutterGstPlayer *self)
{
  ClutterGstPipelinePrivate *priv = CLUTTER_GST_PIPELINE (self)->priv;

  return priv->pipeline;
}

static gboolean
clutter_gst_pipeline_get_idle (ClutterGstPlayer *self)
{
  return FALSE;
}

static gdouble
clutter_gst_pipeline_get_audio_volume (ClutterGstPlayer *self)
{
  return 0.0;
}

static void
clutter_gst_pipeline_set_audio_volume (ClutterGstPlayer *self,
                                       gdouble           volume)
{
}

static gboolean
clutter_gst_pipeline_get_playing (ClutterGstPlayer *self)
{
  return get_playing (CLUTTER_GST_PIPELINE (self));
}

static void
clutter_gst_pipeline_set_playing (ClutterGstPlayer *self,
                                  gboolean          playing)
{
}

static void
player_iface_init (ClutterGstPlayerIface *iface)
{
  iface->get_frame = clutter_gst_pipeline_get_frame;
  iface->get_pipeline = clutter_gst_pipeline_get_pipeline;
  iface->get_idle = clutter_gst_pipeline_get_idle;

  iface->get_audio_volume = clutter_gst_pipeline_get_audio_volume;
  iface->set_audio_volume = clutter_gst_pipeline_set_audio_volume;

  iface->get_playing = clutter_gst_pipeline_get_playing;
  iface->set_playing = clutter_gst_pipeline_set_playing;
}

/**/

static void
clutter_gst_pipeline_get_property (GObject    *object,
                                   guint       property_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  ClutterGstPipeline *self = CLUTTER_GST_PIPELINE (object);
  ClutterGstPipelinePrivate *priv = self->priv;

  switch (property_id)
    {
    case PROP_IDLE:
      g_value_set_boolean (value,
                           clutter_gst_pipeline_get_playing (CLUTTER_GST_PLAYER (self)));
      break;

    case PROP_PLAYING:
      g_value_set_boolean (value,
                           clutter_gst_pipeline_get_playing (CLUTTER_GST_PLAYER (self)));
      break;

    case PROP_AUDIO_VOLUME:
      g_value_set_double (value,
                          clutter_gst_pipeline_get_audio_volume (CLUTTER_GST_PLAYER (self)));
      break;

    case PROP_VIDEO_SINK:
      g_value_set_object (value, G_OBJECT (priv->sink));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
clutter_gst_pipeline_set_property (GObject      *object,
                                   guint         property_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  switch (property_id)
    {
    case PROP_PLAYING:
      clutter_gst_pipeline_set_playing (CLUTTER_GST_PLAYER (object),
                                        g_value_get_boolean (value));
      break;

    case PROP_AUDIO_VOLUME:
      clutter_gst_pipeline_set_audio_volume (CLUTTER_GST_PLAYER (object),
                                             g_value_get_boolean (value));
      break;

    case PROP_VIDEO_SINK:
      clutter_gst_pipeline_set_video_sink_internal (CLUTTER_GST_PIPELINE (object),
                                                    (CoglGstVideoSink *) g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
clutter_gst_pipeline_dispose (GObject *object)
{
  ClutterGstPipelinePrivate *priv = CLUTTER_GST_PIPELINE (object)->priv;

  g_clear_object (&priv->sink);
  priv->pipeline = NULL;

  if (priv->current_frame)
    {
      g_boxed_free (CLUTTER_GST_TYPE_FRAME, priv->current_frame);
      priv->current_frame = NULL;
    }

  G_OBJECT_CLASS (clutter_gst_pipeline_parent_class)->dispose (object);
}

static void
clutter_gst_pipeline_finalize (GObject *object)
{
  G_OBJECT_CLASS (clutter_gst_pipeline_parent_class)->finalize (object);
}

static void
clutter_gst_pipeline_class_init (ClutterGstPipelineClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *pspec;

  g_type_class_add_private (klass, sizeof (ClutterGstPipelinePrivate));

  object_class->get_property = clutter_gst_pipeline_get_property;
  object_class->set_property = clutter_gst_pipeline_set_property;
  object_class->dispose = clutter_gst_pipeline_dispose;
  object_class->finalize = clutter_gst_pipeline_finalize;

  pspec = g_param_spec_object ("video-sink",
                               "Video Sink",
                               "Video Sink",
                               COGL_GST_TYPE_VIDEO_SINK,
                               CLUTTER_GST_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_VIDEO_SINK, pspec);

  g_object_class_override_property (object_class,
                                    PROP_IDLE, "idle");
  g_object_class_override_property (object_class,
                                    PROP_PLAYING, "playing");
  g_object_class_override_property (object_class,
                                    PROP_AUDIO_VOLUME, "audio-volume");
}

static void
clutter_gst_pipeline_init (ClutterGstPipeline *self)
{
  ClutterGstPipelinePrivate *priv;

 self->priv = priv = PIPELINE_PRIVATE (self);

  priv->current_frame = clutter_gst_create_blank_frame (NULL);
}

ClutterGstPipeline *
clutter_gst_pipeline_new (void)
{
  return g_object_new (CLUTTER_GST_TYPE_PIPELINE, NULL);
}

/**/

static void
_new_frame_from_pipeline (CoglGstVideoSink *sink, ClutterGstPipeline *self)
{
  ClutterGstPipelinePrivate *priv = self->priv;

  clutter_gst_player_update_frame (CLUTTER_GST_PLAYER (self),
                                   &priv->current_frame,
                                   cogl_gst_video_sink_get_pipeline (sink));
}

static void
_ready_from_pipeline (CoglGstVideoSink *sink, ClutterGstPipeline *self)
{
  g_signal_emit_by_name (self, "ready");
}

static void
_pixel_aspect_ratio_changed (CoglGstVideoSink   *sink,
                             GParamSpec         *spec,
                             ClutterGstPipeline *self)
{
  clutter_gst_frame_update_pixel_aspect_ratio (self->priv->current_frame, sink);
}

void
clutter_gst_pipeline_set_video_sink_internal (ClutterGstPipeline *self,
                                              CoglGstVideoSink   *sink)
{
  ClutterGstPipelinePrivate *priv = self->priv;

  if (priv->sink == sink)
    return;

  if (priv->sink)
    {
      g_signal_handlers_disconnect_by_func (priv->sink,
                                            _new_frame_from_pipeline, self);
      g_signal_handlers_disconnect_by_func (priv->sink,
                                            _ready_from_pipeline, self);
      g_signal_handlers_disconnect_by_func (priv->sink,
                                            _pixel_aspect_ratio_changed, self);
      g_clear_object (&priv->sink);
      priv->pipeline = NULL;
    }

  if (sink)
    {
      GstObject *tmpobj;
      CoglPipeline *pipeline;

      priv->pipeline = GST_ELEMENT (sink);
      while ((tmpobj = gst_element_get_parent (priv->pipeline)))
        priv->pipeline = GST_ELEMENT (tmpobj);

      priv->sink = g_object_ref_sink (sink);
      g_signal_connect (priv->sink, "new-frame",
                        G_CALLBACK (_new_frame_from_pipeline), self);
      g_signal_connect (priv->sink, "pipeline-ready",
                        G_CALLBACK (_ready_from_pipeline), self);
      g_signal_connect (priv->sink, "notify::pixel-aspect-ratio",
                        G_CALLBACK (_pixel_aspect_ratio_changed), self);

      pipeline = cogl_gst_video_sink_get_pipeline (priv->sink);
      if (pipeline)
        clutter_gst_player_update_frame (CLUTTER_GST_PLAYER (self),
                                         &priv->current_frame,
                                         pipeline);
    }

  g_object_notify (G_OBJECT (self), "video-sink");
}

void
clutter_gst_pipeline_set_video_sink (ClutterGstPipeline *self,
                                     CoglGstVideoSink   *sink)
{
  g_return_if_fail (CLUTTER_GST_IS_PIPELINE (self));
  g_return_if_fail (COGL_GST_IS_VIDEO_SINK (sink));

  clutter_gst_pipeline_set_video_sink_internal (self, sink);
}

CoglGstVideoSink *
clutter_gst_pipeline_get_video_sink (ClutterGstPipeline *self)
{
  g_return_val_if_fail (CLUTTER_GST_IS_PIPELINE (self), NULL);

  return self->priv->sink;
}
