/*
 * Clutter-GStreamer.
 *
 * GStreamer integration library for Clutter.
 *
 * clutter-gst-video-actor.c - ClutterActor using GStreamer to display a
 *                             video stream.
 *
 * Authored By Matthew Allum     <mallum@openedhand.com>
 *             Damien Lespiau    <damien.lespiau@intel.com>
 *             Lionel Landwerlin <lionel.g.landwerlin@linux.intel.com>
 *             Andre Moreira Magalhaes <andre.magalhaes@collabora.co.uk>
 *
 * Copyright (C) 2006 OpenedHand
 * Copyright (C) 2010, 2011 Intel Corporation
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
 * SECTION:clutter-gst-actor
 * @short_description: Actor for playback of video files.
 *
 * #ClutterGstVideoActor is a #ClutterActor that plays video files.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <glib.h>
#include <gio/gio.h>
#include <gst/base/gstbasesink.h>
#include <gst/video/video.h>

#include "clutter-gst-video-actor.h"
#include "clutter-gst-debug.h"
#include "clutter-gst-enum-types.h"
#include "clutter-gst-marshal.h"
#include "clutter-gst-player.h"
#include "clutter-gst-private.h"

struct _ClutterGstVideoActorPrivate
{
};

static void clutter_gst_video_actor_player_init (ClutterGstPlayerIface *iface);

G_DEFINE_TYPE_WITH_CODE (ClutterGstVideoActor,
                         clutter_gst_video_actor,
                         CLUTTER_GST_TYPE_ACTOR,
                         G_IMPLEMENT_INTERFACE (CLUTTER_GST_TYPE_PLAYER,
                                                clutter_gst_video_actor_player_init));

/*
 * ClutterGstPlayer implementation
 */

static void
clutter_gst_video_actor_player_init (ClutterGstPlayerIface *iface)
{
}

/*
 * ClutterGstActor implementation
 */

static gboolean
clutter_gst_video_actor_is_idle (ClutterGstActor *actor)
{
  return clutter_gst_player_get_idle (CLUTTER_GST_PLAYER (actor));
}

/*
 * GObject implementation
 */

static void
clutter_gst_video_actor_dispose (GObject *object)
{
  clutter_gst_player_deinit (CLUTTER_GST_PLAYER (object));

  G_OBJECT_CLASS (clutter_gst_video_actor_parent_class)->dispose (object);
}

static void
clutter_gst_video_actor_class_init (ClutterGstVideoActorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterGstActorClass *gst_actor_class = CLUTTER_GST_ACTOR_CLASS (klass);

  //g_type_class_add_private (klass, sizeof (ClutterGstVideoActorPrivate));

  object_class->dispose = clutter_gst_video_actor_dispose;

  gst_actor_class->is_idle = clutter_gst_video_actor_is_idle;

  clutter_gst_player_class_init (object_class);
}

static gboolean
setup_pipeline (ClutterGstVideoActor *video_actor)
{
  GstElement *pipeline, *video_sink;

  pipeline =
    clutter_gst_player_get_pipeline (CLUTTER_GST_PLAYER (video_actor));
  if (!pipeline)
    {
      g_critical ("Unable to create pipeline");
      return FALSE;
    }

  video_sink = gst_element_factory_make ("cluttersink", NULL);
  g_object_set (video_sink,
                "actor", CLUTTER_GST_ACTOR (video_actor),
                NULL);
  g_object_set (pipeline,
                "video-sink", video_sink,
                "subtitle-font-desc", "Sans 16",
                NULL);

  return TRUE;
}

static void
clutter_gst_video_actor_init (ClutterGstVideoActor *video_actor)
{
  //video_actor->priv =
  //  G_TYPE_INSTANCE_GET_PRIVATE (video_actor,
  //                               CLUTTER_GST_TYPE_VIDEO_ACTOR,
  //                               ClutterGstVideoActorPrivate);

  if (!clutter_gst_player_init (CLUTTER_GST_PLAYER (video_actor)))
    {
      g_warning ("Failed to initiate suitable playback pipeline.");
      return;
    }

  if (!setup_pipeline (video_actor))
    {
      g_warning ("Failed to initiate suitable sinks for pipeline.");
      return;
    }
}

/*
 * Public symbols
 */

/**
 * clutter_gst_video_actor_new:
 *
 * Creates a video actor.
 *
 * <note>This function has to be called from Clutter's main thread. While
 * GStreamer will spawn threads to do its work, we want all the GL calls to
 * happen in the same thread. Clutter-gst knows which thread it is by
 * assuming this constructor is called from the Clutter thread.</note>
 *
 * Return value: the newly created video actor
 */
ClutterActor*
clutter_gst_video_actor_new (void)
{
  return g_object_new (CLUTTER_GST_TYPE_VIDEO_ACTOR,
                       NULL);
}
