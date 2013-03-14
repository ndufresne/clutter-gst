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

#include "clutter-gst-aspectratio.h"

G_DEFINE_TYPE (ClutterGstAspectratio, clutter_gst_aspectratio, CLUTTER_GST_TYPE_ACTOR)

#define ASPECTRATIO_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), CLUTTER_GST_TYPE_ASPECTRATIO, ClutterGstAspectratioPrivate))

struct _ClutterGstAspectratioPrivate
{
  ClutterGstPlayer *player;

  gint frame_width;
  gint frame_height;
  ClutterActorBox paint_box;
};

/**/

static void
clutter_gst_aspectratio_get_preferred_width (ClutterActor *actor,
                                             gfloat        for_height,
                                             gfloat       *min_width,
                                             gfloat       *nat_width)
{
  ClutterGstAspectratioPrivate *priv = CLUTTER_GST_ASPECTRATIO (actor)->priv;

  if (min_width)
    *min_width = 0;
  if (nat_width)
    {
      gdouble aspect = (gdouble) priv->frame_width / (gdouble) priv->frame_height;

      if (for_height > 0)
        *nat_width = for_height * aspect;
      else
        *nat_width = priv->frame_width;
    }
}

static void
clutter_gst_aspectratio_get_preferred_height (ClutterActor *actor,
                                              gfloat        for_width,
                                              gfloat       *min_height,
                                              gfloat       *nat_height)
{
  ClutterGstAspectratioPrivate *priv = CLUTTER_GST_ASPECTRATIO (actor)->priv;

  if (min_height)
    *min_height = 0;
  if (nat_height)
    {
      gdouble aspect = (gdouble) priv->frame_width / (gdouble) priv->frame_height;

      if (for_width > 0)
        *nat_height = for_width / aspect;
      else
        *nat_height = priv->frame_height;
    }
}


static void
clutter_gst_aspectratio_paint_frame (ClutterGstActor *self,
                                     ClutterGstFrame *frame)
{
  ClutterGstAspectratioPrivate *priv = CLUTTER_GST_ASPECTRATIO (self)->priv;
  guint8 paint_opacity;

  paint_opacity = clutter_actor_get_paint_opacity (CLUTTER_ACTOR (self));
  cogl_pipeline_set_color4ub (frame->pipeline,
                              paint_opacity,
                              paint_opacity,
                              paint_opacity,
                              paint_opacity);
  cogl_set_source (frame->pipeline);

  cogl_rectangle (priv->paint_box.x1, priv->paint_box.y1,
                  priv->paint_box.x2, priv->paint_box.y2);
}

static void
_recompute_paint_box (ClutterGstAspectratio *self)
{
  ClutterGstAspectratioPrivate *priv = self->priv;
  ClutterActorBox box;
  gfloat actor_width, actor_height;
  gdouble new_width, new_height;
  gdouble frame_aspect, actor_aspect;

  clutter_actor_get_allocation_box (CLUTTER_ACTOR (self), &box);

  actor_width = box.x2 - box.x1;
  actor_height = box.y2 - box.y1;

  if (actor_width <= 0 || actor_height <= 0)
    return;

  frame_aspect = (gdouble) priv->frame_width / (gdouble) priv->frame_height;
  actor_aspect = actor_width / actor_height;

  if (actor_aspect < frame_aspect)
    {
      new_width = actor_width;
      new_height = actor_width / frame_aspect;
    }
  else
    {
      new_height = actor_height;
      new_width = actor_height * frame_aspect;
    }

  priv->paint_box.x1 = (actor_width - new_width) / 2;
  priv->paint_box.y1 = (actor_height - new_height) / 2;
  priv->paint_box.x2 = priv->paint_box.x1 + new_width;
  priv->paint_box.y2 = priv->paint_box.y1 + new_height;
}

static void
_player_size_changed (ClutterGstPlayer      *player,
                      gint                   width,
                      gint                   height,
                      ClutterGstAspectratio *self)
{
  ClutterGstAspectratioPrivate *priv = self->priv;

  priv->frame_width = width;
  priv->frame_height = height;

  _recompute_paint_box (self);
  clutter_actor_queue_relayout (CLUTTER_ACTOR (self));
}

static void
_player_changed (ClutterGstAspectratio *self,
                 GParamSpec            *spec,
                 gpointer               user_data)
{
  ClutterGstAspectratioPrivate *priv = self->priv;
  ClutterGstPlayer *player = clutter_gst_actor_get_player (CLUTTER_GST_ACTOR (self));

  if (priv->player)
    g_signal_handlers_disconnect_by_func (priv->player, _player_size_changed, self);
  priv->player = player;
  if (priv->player)
    {
      ClutterGstFrame *frame = clutter_gst_player_get_frame (player);

      priv->frame_width = frame->resolution.width;
      priv->frame_height = frame->resolution.height;

      g_signal_connect (priv->player, "size-change",
                        G_CALLBACK (_player_size_changed), self);
    }

  _recompute_paint_box (self);
}

/**/

static void
clutter_gst_aspectratio_get_property (GObject    *object,
                                      guint       property_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  switch (property_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
clutter_gst_aspectratio_set_property (GObject      *object,
                                      guint         property_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  switch (property_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
clutter_gst_aspectratio_dispose (GObject *object)
{
  G_OBJECT_CLASS (clutter_gst_aspectratio_parent_class)->dispose (object);
}

static void
clutter_gst_aspectratio_finalize (GObject *object)
{
  G_OBJECT_CLASS (clutter_gst_aspectratio_parent_class)->finalize (object);
}

static void
clutter_gst_aspectratio_class_init (ClutterGstAspectratioClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  ClutterGstActorClass *gst_actor_class = CLUTTER_GST_ACTOR_CLASS (klass);

  g_type_class_add_private (klass, sizeof (ClutterGstAspectratioPrivate));

  object_class->get_property = clutter_gst_aspectratio_get_property;
  object_class->set_property = clutter_gst_aspectratio_set_property;
  object_class->dispose = clutter_gst_aspectratio_dispose;
  object_class->finalize = clutter_gst_aspectratio_finalize;

  actor_class->get_preferred_width = clutter_gst_aspectratio_get_preferred_width;
  actor_class->get_preferred_height = clutter_gst_aspectratio_get_preferred_height;

  gst_actor_class->paint_frame = clutter_gst_aspectratio_paint_frame;
}

static void
clutter_gst_aspectratio_init (ClutterGstAspectratio *self)
{
  self->priv = ASPECTRATIO_PRIVATE (self);

  g_signal_connect (self, "notify::player",
                    G_CALLBACK (_player_changed), NULL);
  g_signal_connect_swapped (self, "allocation-changed",
                            G_CALLBACK (_recompute_paint_box), self);
}

ClutterGstAspectratio *
clutter_gst_aspectratio_new (void)
{
  return g_object_new (CLUTTER_GST_TYPE_ASPECTRATIO, NULL);
}
