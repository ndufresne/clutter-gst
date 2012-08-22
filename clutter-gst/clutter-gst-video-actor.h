/*
 * Clutter-GStreamer.
 *
 * GStreamer integration library for Clutter.
 *
 * clutter-gst-video-actor.h - ClutterActor using GStreamer to display a
 *                             video stream.
 *
 * Authored By Matthew Allum  <mallum@openedhand.com>
 *             Andre Moreira Magalhaes <andre.magalhaes@collabora.co.uk>
 *
 * Copyright (C) 2006 OpenedHand
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

#if !defined(__CLUTTER_GST_H_INSIDE__) && !defined(CLUTTER_GST_COMPILATION)
#error "Only <clutter-gst/clutter-gst.h> can be included directly."
#endif

#ifndef __CLUTTER_GST_VIDEO_ACTOR_H__
#define __CLUTTER_GST_VIDEO_ACTOR_H__

#include <glib-object.h>
#include <clutter/clutter.h>
#include <gst/gstelement.h>

#include <clutter-gst/clutter-gst-actor.h>
#include <clutter-gst/clutter-gst-types.h>

G_BEGIN_DECLS

#define CLUTTER_GST_TYPE_VIDEO_ACTOR clutter_gst_video_actor_get_type()

#define CLUTTER_GST_VIDEO_ACTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  CLUTTER_GST_TYPE_VIDEO_ACTOR, ClutterGstVideoActor))

#define CLUTTER_GST_VIDEO_ACTOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  CLUTTER_GST_TYPE_VIDEO_ACTOR, ClutterGstVideoActorClass))

#define CLUTTER_GST_IS_VIDEO_ACTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  CLUTTER_GST_TYPE_VIDEO_ACTOR))

#define CLUTTER_GST_IS_VIDEO_ACTOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  CLUTTER_GST_TYPE_VIDEO_ACTOR))

#define CLUTTER_GST_VIDEO_ACTOR_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  CLUTTER_GST_TYPE_VIDEO_ACTOR, ClutterGstVideoActorClass))

typedef struct _ClutterGstVideoActor        ClutterGstVideoActor;
typedef struct _ClutterGstVideoActorClass   ClutterGstVideoActorClass;
typedef struct _ClutterGstVideoActorPrivate ClutterGstVideoActorPrivate;

/**
 * ClutterGstVideoActor:
 *
 * Subclass of #ClutterGstActor that displays videos using GStreamer.
 *
 * The #ClutterGstVideoActor structure contains only private data and
 * should not be accessed directly.
 */
struct _ClutterGstVideoActor
{
  /*< private >*/
  ClutterGstActor parent;
  ClutterGstVideoActorPrivate *priv;
};

/**
 * ClutterGstVideoActorClass:
 *
 * Base class for #ClutterGstVideoActor.
 */
struct _ClutterGstVideoActorClass
{
  /*< private >*/
  ClutterGstActorClass parent_class;

  /* Future padding */
  void (* _clutter_reserved1) (void);
  void (* _clutter_reserved2) (void);
  void (* _clutter_reserved3) (void);
  void (* _clutter_reserved4) (void);
  void (* _clutter_reserved5) (void);
  void (* _clutter_reserved6) (void);
};

GType clutter_gst_video_actor_get_type (void) G_GNUC_CONST;

ClutterActor *          clutter_gst_video_actor_new                 (void);

G_END_DECLS

#endif /* __CLUTTER_GST_VIDEO_ACTOR_H__ */
