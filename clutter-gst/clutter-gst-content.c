/*
 * Clutter-GStreamer.
 *
 * GStreamer integration library for Clutter.
 *
 * Authored By Lionel Landwerlin <lionel.g.landwerlin@linux.intel.com>
 *             Bastian Winkler   <buz@netbuz.org>
 *
 * Copyright (C) 2013 Intel Corporation
 * Copyright (C) 2013 Bastian Winkler <buz@netbuz.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:clutter-gst-content
 * @short_description: A #ClutterContent for displaying video frames.
 *
 * #ClutterGstContent implements the #ClutterContent interface.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "clutter-gst-content.h"
#include "clutter-gst-private.h"
#include "clutter-gst-marshal.h"

static void content_iface_init (ClutterContentIface *iface);

G_DEFINE_TYPE_WITH_CODE (ClutterGstContent,
                         clutter_gst_content,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_CONTENT, content_iface_init))

#define CLUTTER_GST_CONTENT_GET_PRIVATE(obj)\
  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
  CLUTTER_GST_TYPE_CONTENT, \
  ClutterGstContentPrivate))


struct _ClutterGstContentPrivate
{
  ClutterGstVideoSink *sink;
  ClutterGstPlayer *player;
  ClutterGstFrame *current_frame;
};

enum
{
  PROP_0,

  PROP_VIDEO_SINK,
  PROP_PLAYER,

  PROP_LAST
};

static GParamSpec *props[PROP_LAST];

enum
{
  SIZE_CHANGE,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];


static void
update_frame (ClutterGstContent *self,
              ClutterGstFrame   *new_frame)
{
  ClutterGstContentPrivate *priv = self->priv;
  ClutterGstFrame *old_frame;

  old_frame = priv->current_frame;
  priv->current_frame = g_boxed_copy (CLUTTER_GST_TYPE_FRAME, new_frame);

  if (old_frame)
    {
      new_frame->resolution.par_n = old_frame->resolution.par_n;
      new_frame->resolution.par_d = old_frame->resolution.par_d;
    }

  if (!old_frame ||
      (new_frame->resolution.width != old_frame->resolution.width ||
       new_frame->resolution.height != old_frame->resolution.height))
    {
      g_signal_emit (self, signals[SIZE_CHANGE], 0,
                     new_frame->resolution.width,
                     new_frame->resolution.height);
    }

  if (old_frame)
    g_boxed_free (CLUTTER_GST_TYPE_FRAME, old_frame);
}

static void
_new_frame_from_pipeline (ClutterGstVideoSink *sink,
                          ClutterGstContent   *self)
{
  update_frame (self, clutter_gst_video_sink_get_frame (sink));

  clutter_content_invalidate (CLUTTER_CONTENT (self));
}

static void
_pixel_aspect_ratio_changed (ClutterGstVideoSink *sink,
                             GParamSpec          *pspec,
                             ClutterGstContent   *self)
{
  clutter_gst_frame_update_pixel_aspect_ratio (self->priv->current_frame,
                                               sink);
}

static void content_set_sink (ClutterGstContent   *self,
                              ClutterGstVideoSink *sink,
                              gboolean             set_from_player);

static void
content_set_player (ClutterGstContent *self,
                    ClutterGstPlayer  *player)
{
  ClutterGstContentPrivate *priv = self->priv;

  if (priv->player == player)
    return;

  if (priv->player)
    g_clear_object (&priv->player);

  if (player)
    {
      priv->player = g_object_ref_sink (player);
      content_set_sink (self, clutter_gst_player_get_video_sink (player), TRUE);
    }
  else
    content_set_sink (self, NULL, TRUE);

  g_object_notify (G_OBJECT (self), "player");
}

static void
content_set_sink (ClutterGstContent   *self,
                  ClutterGstVideoSink *sink,
                  gboolean             set_from_player)
{
  ClutterGstContentPrivate *priv = self->priv;

  if (priv->sink == sink)
    return;

  if (!set_from_player)
    content_set_player (self, NULL);

  if (priv->sink)
    {
      g_signal_handlers_disconnect_by_func (priv->sink,
                                            _new_frame_from_pipeline, self);
      g_signal_handlers_disconnect_by_func (priv->sink,
                                            _pixel_aspect_ratio_changed, self);
      g_clear_object (&priv->sink);
    }

  if (sink)
    {
      priv->sink = g_object_ref_sink (sink);
      g_signal_connect (priv->sink, "new-frame",
                        G_CALLBACK (_new_frame_from_pipeline), self);
      g_signal_connect (priv->sink, "notify::pixel-aspect-ratio",
                        G_CALLBACK (_pixel_aspect_ratio_changed), self);

      if (clutter_gst_video_sink_is_ready (priv->sink))
        {
          ClutterGstFrame *frame = clutter_gst_video_sink_get_frame (priv->sink);
          if (frame)
            update_frame (self, frame);
        }
    }

  g_object_notify (G_OBJECT (self), "video-sink");
}

static gboolean
clutter_gst_content_get_preferred_size (ClutterContent *content,
                                        gfloat         *width,
                                        gfloat         *height)
{
  ClutterGstContentPrivate *priv = CLUTTER_GST_CONTENT (content)->priv;

  if (!priv->current_frame)
    return FALSE;

  if (width)
    *width = priv->current_frame->resolution.width;
  if (height)
    *height = priv->current_frame->resolution.height;

  return TRUE;
}

static void
clutter_gst_content_paint_content (ClutterContent   *content,
                                   ClutterActor     *actor,
                                   ClutterPaintNode *root)
{
  ClutterGstContentPrivate *priv = CLUTTER_GST_CONTENT (content)->priv;
  ClutterActorBox box;
  ClutterPaintNode *node;
  ClutterContentRepeat repeat;
  guint8 paint_opacity;

  if (!priv->current_frame)
    return;

  clutter_actor_get_content_box (actor, &box);
  paint_opacity = clutter_actor_get_paint_opacity (actor);
  repeat = clutter_actor_get_content_repeat (actor);


  cogl_pipeline_set_color4ub (priv->current_frame->pipeline,
                              paint_opacity, paint_opacity,
                              paint_opacity, paint_opacity);

  node = clutter_pipeline_node_new (priv->current_frame->pipeline);
  clutter_paint_node_set_name (node, "Video");

  if (repeat == CLUTTER_REPEAT_NONE)
    clutter_paint_node_add_rectangle (node, &box);
  else
    {
      float t_w = 1.f, t_h = 1.f;

      if ((repeat & CLUTTER_REPEAT_X_AXIS) != FALSE)
        t_w = (box.x2 - box.x1) / priv->current_frame->resolution.width;

      if ((repeat & CLUTTER_REPEAT_Y_AXIS) != FALSE)
        t_h = (box.y2 - box.y1) / priv->current_frame->resolution.height;

      clutter_paint_node_add_texture_rectangle (node, &box,
                                                0.f, 0.f,
                                                t_w, t_h);
    }

  clutter_paint_node_add_child (root, node);
  clutter_paint_node_unref (node);
}

static void
content_iface_init (ClutterContentIface *iface)
{
  iface->get_preferred_size = clutter_gst_content_get_preferred_size;
  iface->paint_content = clutter_gst_content_paint_content;
}

static void
clutter_gst_content_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  ClutterGstContent *self = CLUTTER_GST_CONTENT (object);

  switch (prop_id)
    {
    case PROP_VIDEO_SINK:
      content_set_sink (self, g_value_get_object (value), FALSE);
      break;

    case PROP_PLAYER:
      content_set_player (self,
                          CLUTTER_GST_PLAYER (g_value_get_object (value)));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_gst_content_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  ClutterGstContentPrivate *priv = CLUTTER_GST_CONTENT (object)->priv;

  switch (prop_id)
    {
    case PROP_VIDEO_SINK:
      g_value_set_object (value, priv->sink);
      break;

    case PROP_PLAYER:
      g_value_set_object (value, priv->player);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
clutter_gst_content_dispose (GObject *object)
{
  ClutterGstContentPrivate *priv = CLUTTER_GST_CONTENT (object)->priv;

  g_clear_object (&priv->sink);

  if (priv->current_frame)
    {
      g_boxed_free (CLUTTER_GST_TYPE_FRAME, priv->current_frame);
      priv->current_frame = NULL;
    }

  G_OBJECT_CLASS (clutter_gst_content_parent_class)->dispose (object);
}

static void
clutter_gst_content_finalize (GObject *object)
{
  G_OBJECT_CLASS (clutter_gst_content_parent_class)->finalize (object);
}

static void
clutter_gst_content_class_init (ClutterGstContentClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property   = clutter_gst_content_set_property;
  gobject_class->get_property   = clutter_gst_content_get_property;
  gobject_class->dispose        = clutter_gst_content_dispose;
  gobject_class->finalize       = clutter_gst_content_finalize;

  g_type_class_add_private (klass, sizeof (ClutterGstContentPrivate));

  props[PROP_PLAYER] =
    g_param_spec_object ("player",
                         "ClutterGst Player",
                         "ClutterGst Player",
                         G_TYPE_OBJECT,
                         G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE);

  props[PROP_VIDEO_SINK] =
    g_param_spec_object ("video-sink",
                         "Cogl Video Sink",
                         "Cogl Video Sink",
                         CLUTTER_GST_TYPE_VIDEO_SINK,
                         G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE);
  g_object_class_install_properties (gobject_class, PROP_LAST, props);


  /**
   * ClutterGstContent::size-change:
   * @content: the #ClutterGstContent instance that received the signal
   * @width: new width of the frames
   * @height: new height of the frames
   *
   * The ::size-change signal is emitted each time the video size changes.
   */
  signals[SIZE_CHANGE] =
    g_signal_new ("size-change",
                  CLUTTER_GST_TYPE_CONTENT,
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  _clutter_gst_marshal_VOID__INT_INT,
                  G_TYPE_NONE, 2,
                  G_TYPE_INT, G_TYPE_INT);
}


static void
clutter_gst_content_init (ClutterGstContent *self)
{
  ClutterGstContentPrivate *priv;

  self->priv = priv = CLUTTER_GST_CONTENT_GET_PRIVATE (self);

  content_set_sink (self,
                    CLUTTER_GST_VIDEO_SINK (clutter_gst_create_video_sink ()),
                    FALSE);
}


/**
 * clutter_gst_content_new:
 *
 * Returns: (transfer full): a new #ClutterGstContent instance
 */
ClutterContent *
clutter_gst_content_new (void)
{
  return g_object_new (CLUTTER_GST_TYPE_CONTENT,
                       NULL);
}

/**
 * clutter_gst_content_new_with_sink:
 *
 * Returns: (transfer full): a new #ClutterGstContent instance
 *
 * Since: 3.0
 */
ClutterContent *
clutter_gst_content_new_with_sink (ClutterGstVideoSink *sink)
{
  return g_object_new (CLUTTER_GST_TYPE_CONTENT,
                       "video-sink", sink,
                       NULL);
}

/**
 * clutter_gst_content_get_frame:
 * @self: A #ClutterGstContent
 *
 * Returns: (transfer none): The #ClutterGstFrame currently attached to @self.
 *
 * Since: 3.0
 */
ClutterGstFrame *
clutter_gst_content_get_frame (ClutterGstContent *self)
{
  g_return_val_if_fail (CLUTTER_GST_IS_CONTENT (self), NULL);

  return self->priv->current_frame;
}

/**
 * clutter_gst_content_get_sink:
 * @self: A #ClutterGstContent
 *
 * Returns: (transfer none): The #ClutterGstVideoSink currently attached to @self.
 *
 * Since: 3.0
 */
ClutterGstVideoSink *
clutter_gst_content_get_sink (ClutterGstContent *self)
{
  g_return_val_if_fail (CLUTTER_GST_IS_CONTENT (self), NULL);

  return self->priv->sink;
}

/**
 * clutter_gst_content_set_sink:
 * @self: A #ClutterGstContent
 * @sink: A #ClutterGstVideoSink or %NULL
 *
 * Since: 3.0
 */
void
clutter_gst_content_set_sink (ClutterGstContent   *self,
                              ClutterGstVideoSink *sink)
{
  g_return_if_fail (CLUTTER_GST_IS_CONTENT (self));
  g_return_if_fail (sink == NULL || CLUTTER_GST_IS_VIDEO_SINK (sink));

  content_set_sink (self, sink, FALSE);
}

/**
 * clutter_gst_content_get_player:
 * @self: A #ClutterGstContent
 *
 * Returns: (transfer none): The #ClutterGstPlayer currently attached to @self.
 *
 * Since: 3.0
 */
ClutterGstPlayer *
clutter_gst_content_get_player (ClutterGstContent *self)
{
  g_return_val_if_fail (CLUTTER_GST_IS_CONTENT (self), NULL);

  return self->priv->player;
}

/**
 * clutter_gst_content_set_player:
 * @self: A #ClutterGstContent
 * @player: A #ClutterGstPlayer or %NULL
 *
 * Since: 3.0
 */
void
clutter_gst_content_set_player (ClutterGstContent *self,
                                ClutterGstPlayer  *player)
{
  g_return_if_fail (CLUTTER_GST_IS_CONTENT (self));
  g_return_if_fail (player == NULL || CLUTTER_GST_IS_PLAYER (player));

  content_set_player (self, player);
}
