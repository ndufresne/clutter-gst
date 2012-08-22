/*
 * Clutter-GStreamer.
 *
 * GStreamer integration library for Clutter.
 *
 * clutter-gst-actor.h - ClutterActor using GStreamer
 *
 * Authored By Andre Moreira Magalhaes <andre.magalhaes@collabora.co.uk>
 *
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

#ifndef __CLUTTER_GST_ACTOR_H__
#define __CLUTTER_GST_ACTOR_H__

#include <glib-object.h>
#include <clutter/clutter.h>
#include <gst/gstelement.h>

#include <clutter-gst/clutter-gst-types.h>

G_BEGIN_DECLS

#define CLUTTER_GST_TYPE_ACTOR clutter_gst_actor_get_type()

#define CLUTTER_GST_ACTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  CLUTTER_GST_TYPE_ACTOR, ClutterGstActor))

#define CLUTTER_GST_ACTOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  CLUTTER_GST_TYPE_ACTOR, ClutterGstActorClass))

#define CLUTTER_GST_IS_ACTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  CLUTTER_GST_TYPE_ACTOR))

#define CLUTTER_GST_IS_ACTOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  CLUTTER_GST_TYPE_ACTOR))

#define CLUTTER_GST_ACTOR_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  CLUTTER_GST_TYPE_ACTOR, ClutterGstActorClass))

typedef struct _ClutterGstActor        ClutterGstActor;
typedef struct _ClutterGstActorClass   ClutterGstActorClass;
typedef struct _ClutterGstActorPrivate ClutterGstActorPrivate;

/**
 * ClutterGstActor:
 *
 * The #ClutterGstActor structure contains only private data and
 * should not be accessed directly.
 */
struct _ClutterGstActor
{
  /*< private >*/
  ClutterActor parent;
  ClutterGstActorPrivate *priv;
};

/**
 * ClutterGstActorClass:
 *
 * Base class for #ClutterGstActor.
 */
struct _ClutterGstActorClass
{
  /*< private >*/
  ClutterActorClass parent_class;

  /*< public >*/
  void     (* size_change) (ClutterGstActor *actor,
                            gint             width,
                            gint             height);
  gboolean (* is_idle)     (ClutterGstActor *actor);

  /* Future padding */
  void (* _clutter_reserved1) (void);
  void (* _clutter_reserved2) (void);
  void (* _clutter_reserved3) (void);
  void (* _clutter_reserved4) (void);
  void (* _clutter_reserved5) (void);
  void (* _clutter_reserved6) (void);
};

GType clutter_gst_actor_get_type (void) G_GNUC_CONST;

CoglHandle     clutter_gst_actor_get_cogl_texture  (ClutterGstActor *actor);
void           clutter_gst_actor_set_cogl_texture  (ClutterGstActor *actor,
                                                    CoglHandle       cogl_tex);
CoglHandle     clutter_gst_actor_get_cogl_material (ClutterGstActor *actor);
void           clutter_gst_actor_set_cogl_material (ClutterGstActor *actor,
                                                    CoglHandle       cogl_material);

gboolean       clutter_gst_actor_is_idle           (ClutterGstActor *actor);
CoglHandle     clutter_gst_actor_get_idle_material (ClutterGstActor *actor);
void           clutter_gst_actor_set_idle_material (ClutterGstActor *actor,
                                                    CoglHandle       cogl_material);

G_END_DECLS

#endif /* __CLUTTER_GST_ACTOR_H__ */
