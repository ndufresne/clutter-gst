/*
 * Clutter-GStreamer.
 *
 * GStreamer integration library for Clutter.
 *
 * clutter-gst-actor.c - ClutterActor using GStreamer
 *
 * Authored By Matthew Allum     <mallum@openedhand.com>
 *             Damien Lespiau    <damien.lespiau@intel.com>
 *             Lionel Landwerlin <lionel.g.landwerlin@linux.intel.com>
 *             Andre Moreira Magalhaes <andre.magalhaes@collabora.co.uk>
 *
 * Copyright (C) 2006 OpenedHand
 * Copyright (C) 2010-2013 Intel Corporation
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
 * SECTION:clutter-gst-video-actor
 * @short_description: Actor for playback of video files.
 *
 * #ClutterGstActor is a #ClutterActor that plays video files.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <glib.h>
#include <gio/gio.h>
#include <gst/base/gstbasesink.h>
#include <gst/video/video.h>

#include "clutter-gst-actor.h"
#include "clutter-gst-debug.h"
#include "clutter-gst-enum-types.h"
#include "clutter-gst-marshal.h"
#include "clutter-gst-private.h"

struct _ClutterGstActorPrivate
{
  ClutterGstPlayer *player;
  ClutterGstFrame  *frame;
};

enum {
  PROP_0,

  PROP_PLAYER
};

G_DEFINE_TYPE (ClutterGstActor, clutter_gst_actor, CLUTTER_TYPE_ACTOR)

static void
clutter_gst_actor_get_preferred_width (ClutterActor *actor,
                                       gfloat        for_height,
                                       gfloat       *min_width,
                                       gfloat       *nat_width)
{
  ClutterGstActorPrivate *priv = CLUTTER_GST_ACTOR (actor)->priv;

  if (min_width)
    *min_width = 0;
  if (nat_width)
    *nat_width = priv->frame->resolution.width;
}

static void
clutter_gst_actor_get_preferred_height (ClutterActor *actor,
                                        gfloat        for_width,
                                        gfloat       *min_height,
                                        gfloat       *nat_height)
{
  ClutterGstActorPrivate *priv = CLUTTER_GST_ACTOR (actor)->priv;

  if (min_height)
    *min_height = 0;
  if (nat_height)
    *nat_height = priv->frame->resolution.height;
}

static void
clutter_gst_actor_paint_frame (ClutterGstActor *self,
                               ClutterGstFrame *frame)
{
  ClutterActorBox box;
  guint8 paint_opacity;

  clutter_actor_get_allocation_box (CLUTTER_ACTOR (self), &box);
  paint_opacity = clutter_actor_get_paint_opacity (CLUTTER_ACTOR (self));
  cogl_pipeline_set_color4ub (frame->pipeline,
                              paint_opacity,
                              paint_opacity,
                              paint_opacity,
                              paint_opacity);
  cogl_set_source (frame->pipeline);

  cogl_rectangle (0, 0, box.x2 - box.x1, box.y2 - box.y1);
}

static void
clutter_gst_actor_paint (ClutterActor *actor)
{
  ClutterGstActor *self = CLUTTER_GST_ACTOR (actor);
  ClutterGstActorPrivate *priv = self->priv;

  if (priv->player)
    {
      ClutterGstFrame *frame = clutter_gst_player_get_frame (priv->player);

      if (frame)
        CLUTTER_GST_ACTOR_GET_CLASS (self)->paint_frame (self, frame);
    }
}

static void
_player_new_frame (ClutterGstPlayer *player,
                   ClutterGstFrame  *frame,
                   ClutterGstActor  *self)
{
  ClutterGstActorPrivate *priv = self->priv;

  if (priv->frame)
    g_boxed_free (CLUTTER_GST_TYPE_FRAME, priv->frame);
  priv->frame = g_boxed_copy (CLUTTER_GST_TYPE_FRAME, frame);

  clutter_actor_queue_redraw (CLUTTER_ACTOR (self));
}

static void
clutter_gst_actor_set_player_internal (ClutterGstActor  *self,
                                       ClutterGstPlayer *player)
{
  ClutterGstActorPrivate *priv = self->priv;

  if (priv->player) {
    g_boxed_free (CLUTTER_GST_TYPE_FRAME, priv->frame);
    priv->frame = NULL;
    g_signal_handlers_disconnect_by_func (priv->player,
                                          _player_new_frame,
                                          self);

    g_clear_object (&priv->player);
  }

  if (player != NULL) {
    priv->player = g_object_ref_sink (player);
    priv->frame = g_boxed_copy (CLUTTER_GST_TYPE_FRAME,
                                clutter_gst_player_get_frame (player));

    g_signal_connect (priv->player, "new-frame",
                      G_CALLBACK (_player_new_frame), self);
  }

  clutter_actor_queue_redraw (CLUTTER_ACTOR (self));
  g_object_notify (G_OBJECT (self), "player");
}

/*
 * GObject implementation
 */

static void
clutter_gst_actor_dispose (GObject *object)
{
  ClutterGstActorPrivate *priv = CLUTTER_GST_ACTOR (object)->priv;

  g_clear_object (&priv->player);

  if (priv->frame)
    {
      g_boxed_free (CLUTTER_GST_TYPE_FRAME, priv->frame);
      priv->frame = NULL;
    }

  G_OBJECT_CLASS (clutter_gst_actor_parent_class)->dispose (object);
}

static void
clutter_gst_actor_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  ClutterGstActor *actor = CLUTTER_GST_ACTOR (object);
  ClutterGstActorPrivate *priv = actor->priv;

  switch (property_id)
    {
    case PROP_PLAYER:
      g_value_set_object (value, priv->player);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
clutter_gst_actor_set_property (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  ClutterGstActor *actor = CLUTTER_GST_ACTOR (object);

  switch (property_id)
    {
    case PROP_PLAYER:
      clutter_gst_actor_set_player_internal (actor,
                                             CLUTTER_GST_PLAYER (g_value_get_object (value)));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
clutter_gst_actor_class_init (ClutterGstActorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  GParamSpec *pspec;

  g_type_class_add_private (klass, sizeof (ClutterGstActorPrivate));

  object_class->dispose = clutter_gst_actor_dispose;
  object_class->set_property = clutter_gst_actor_set_property;
  object_class->get_property = clutter_gst_actor_get_property;

  actor_class->get_preferred_width = clutter_gst_actor_get_preferred_width;
  actor_class->get_preferred_height = clutter_gst_actor_get_preferred_height;
  actor_class->paint = clutter_gst_actor_paint;

  klass->paint_frame = clutter_gst_actor_paint_frame;

  pspec = g_param_spec_object ("player",
                               "Player",
                               "Player",
                               G_TYPE_OBJECT,
                               CLUTTER_GST_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_PLAYER, pspec);
}

static void
clutter_gst_actor_init (ClutterGstActor *actor)
{
  actor->priv = G_TYPE_INSTANCE_GET_PRIVATE (actor,
                                             CLUTTER_GST_TYPE_ACTOR,
                                             ClutterGstActorPrivate);
}

/*
 * Public symbols
 */

/**
 * clutter_gst_actor_get_player:
 * @self: a #ClutterGstActor
 *
 * Retrieves the #ClutterGstPlayer used by the @self.
 *
 * Return value: (transfer none): the #ClutterGstPlayer element used by the actor
 *
 * Since: 3.0
 */
ClutterGstPlayer *
clutter_gst_actor_get_player (ClutterGstActor *self)
{
  ClutterGstActorPrivate *priv;

  g_return_val_if_fail (CLUTTER_GST_IS_ACTOR (self), NULL);

  priv = self->priv;

  return priv->player;
}

/**
 * clutter_gst_actor_set_player:
 * @self: a #ClutterGstActor
 * @player: a #ClutterGstPlayer
 *
 * Set the #ClutterGstPlayer used by the @self.
 *
 * Since: 3.0
 */
void
clutter_gst_actor_set_player (ClutterGstActor  *self,
                              ClutterGstPlayer *player)
{
  g_return_if_fail (CLUTTER_GST_IS_ACTOR (self));
  g_return_if_fail (CLUTTER_GST_IS_PLAYER (player) || player == NULL);

  clutter_gst_actor_set_player_internal (self, player);
}
