/*
 * Clutter-GStreamer.
 *
 * GStreamer integration library for Clutter.
 *
 * clutter-gst-camera-actor.h - ClutterActor using GStreamer to display/manipulate a
 *                              camera stream.
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

#ifndef __CLUTTER_GST_CAMERA_ACTOR_H__
#define __CLUTTER_GST_CAMERA_ACTOR_H__

#include <clutter/clutter.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <gst/gstelement.h>
#include <gst/pbutils/encoding-profile.h>

#include <clutter-gst/clutter-gst-actor.h>
#include <clutter-gst/clutter-gst-camera-device.h>
#include <clutter-gst/clutter-gst-types.h>

G_BEGIN_DECLS

#define CLUTTER_GST_TYPE_CAMERA_ACTOR clutter_gst_camera_actor_get_type()

#define CLUTTER_GST_CAMERA_ACTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  CLUTTER_GST_TYPE_CAMERA_ACTOR, ClutterGstCameraActor))

#define CLUTTER_GST_CAMERA_ACTOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  CLUTTER_GST_TYPE_CAMERA_ACTOR, ClutterGstCameraActorClass))

#define CLUTTER_GST_IS_CAMERA_ACTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  CLUTTER_GST_TYPE_CAMERA_ACTOR))

#define CLUTTER_GST_IS_CAMERA_ACTOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  CLUTTER_GST_TYPE_CAMERA_ACTOR))

#define CLUTTER_GST_CAMERA_ACTOR_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  CLUTTER_GST_TYPE_CAMERA_ACTOR, ClutterGstCameraActorClass))

typedef struct _ClutterGstCameraActor        ClutterGstCameraActor;
typedef struct _ClutterGstCameraActorClass   ClutterGstCameraActorClass;
typedef struct _ClutterGstCameraActorPrivate ClutterGstCameraActorPrivate;

/**
 * ClutterGstCameraActor:
 *
 * Subclass of #ClutterGstActor that displays camera streams using GStreamer.
 *
 * The #ClutterGstCameraActor structure contains only private data and
 * should not be accessed directly.
 */
struct _ClutterGstCameraActor
{
  /*< private >*/
  ClutterGstActor parent;
  ClutterGstCameraActorPrivate *priv;
};

/**
 * ClutterGstCameraActorClass:
 *
 * Base class for #ClutterGstCameraActor.
 */
struct _ClutterGstCameraActorClass
{
  /*< private >*/
  ClutterGstActorClass parent_class;

  void (* ready_for_capture) (ClutterGstCameraActor *camera_actor,
                              gboolean               ready);
  void (* photo_saved)       (ClutterGstCameraActor *camera_actor);
  void (* photo_taken)       (ClutterGstCameraActor *camera_actor,
                              GdkPixbuf             *pixbuf);
  void (* video_saved)       (ClutterGstCameraActor *camera_actor);

  /* Future padding */
  void (* _clutter_reserved1) (void);
  void (* _clutter_reserved2) (void);
  void (* _clutter_reserved3) (void);
  void (* _clutter_reserved4) (void);
  void (* _clutter_reserved5) (void);
  void (* _clutter_reserved6) (void);
};

GType clutter_gst_camera_actor_get_type (void) G_GNUC_CONST;

ClutterActor * clutter_gst_camera_actor_new                   (void);

GstElement *   clutter_gst_camera_actor_get_pipeline          (ClutterGstCameraActor  *camera_actor);
GstElement *   clutter_gst_camera_actor_get_camerabin         (ClutterGstCameraActor  *camera_actor);

const GPtrArray *
               clutter_gst_camera_actor_get_camera_devices    (ClutterGstCameraActor  *camera_actor);
ClutterGstCameraDevice *
               clutter_gst_camera_actor_get_camera_device     (ClutterGstCameraActor  *camera_actor);
gboolean       clutter_gst_camera_actor_set_camera_device
                                                              (ClutterGstCameraActor  *camera_actor,
                                                               ClutterGstCameraDevice *camera_device);

gboolean       clutter_gst_camera_actor_supports_gamma_correction
                                                              (ClutterGstCameraActor  *camera_actor);
gboolean       clutter_gst_camera_actor_get_gamma_range       (ClutterGstCameraActor  *camera_actor,
                                                               gdouble                *min_value,
                                                               gdouble                *max_value,
                                                               gdouble                *default_value);
gboolean       clutter_gst_camera_actor_get_gamma             (ClutterGstCameraActor  *camera_actor,
                                                               gdouble                *cur_value);
gboolean       clutter_gst_camera_actor_set_gamma             (ClutterGstCameraActor  *camera_actor,
                                                               gdouble                 value);

gboolean       clutter_gst_camera_actor_supports_color_balance
                                                              (ClutterGstCameraActor  *camera_actor);
gboolean       clutter_gst_camera_actor_get_color_balance_property_range
                                                              (ClutterGstCameraActor  *camera_actor,
                                                               const gchar            *property,
                                                               gdouble                *min_value,
                                                               gdouble                *max_value,
                                                               gdouble                *default_value);
gboolean       clutter_gst_camera_actor_get_color_balance_property
                                                              (ClutterGstCameraActor  *camera_actor,
                                                               const gchar            *property,
                                                               gdouble                *cur_value);
gboolean       clutter_gst_camera_actor_set_color_balance_property
                                                              (ClutterGstCameraActor  *camera_actor,
                                                               const gchar            *property,
                                                               gdouble                 value);
gboolean       clutter_gst_camera_actor_get_brightness_range  (ClutterGstCameraActor  *camera_actor,
                                                               gdouble                *min_value,
                                                               gdouble                *max_value,
                                                               gdouble                *default_value);
gboolean       clutter_gst_camera_actor_get_brightness        (ClutterGstCameraActor  *camera_actor,
                                                               gdouble                *cur_value);
gboolean       clutter_gst_camera_actor_set_brightness        (ClutterGstCameraActor  *camera_actor,
                                                               gdouble                 value);
gboolean       clutter_gst_camera_actor_get_contrast_range    (ClutterGstCameraActor  *camera_actor,
                                                               gdouble                *min_value,
                                                               gdouble                *max_value,
                                                               gdouble                *default_value);
gboolean       clutter_gst_camera_actor_get_contrast          (ClutterGstCameraActor  *camera_actor,
                                                               gdouble                *cur_value);
gboolean       clutter_gst_camera_actor_set_contrast          (ClutterGstCameraActor  *camera_actor,
                                                               gdouble                 value);
gboolean       clutter_gst_camera_actor_get_saturation_range  (ClutterGstCameraActor  *camera_actor,
                                                               gdouble                *min_value,
                                                               gdouble                *max_value,
                                                               gdouble                *default_value);
gboolean       clutter_gst_camera_actor_get_saturation        (ClutterGstCameraActor  *camera_actor,
                                                               gdouble                *cur_value);
gboolean       clutter_gst_camera_actor_set_saturation        (ClutterGstCameraActor  *camera_actor,
                                                               gdouble                 value);
gboolean       clutter_gst_camera_actor_get_hue_range         (ClutterGstCameraActor  *camera_actor,
                                                               gdouble                *min_value,
                                                               gdouble                *max_value,
                                                               gdouble                *default_value);
gboolean       clutter_gst_camera_actor_get_hue               (ClutterGstCameraActor  *camera_actor,
                                                               gdouble                *cur_value);
gboolean       clutter_gst_camera_actor_set_hue               (ClutterGstCameraActor  *camera_actor,
                                                               gdouble                 value);

GstElement *   clutter_gst_camera_actor_get_filter            (ClutterGstCameraActor  *camera_actor);
gboolean       clutter_gst_camera_actor_set_filter            (ClutterGstCameraActor  *camera_actor,
                                                               GstElement             *filter);
gboolean       clutter_gst_camera_actor_remove_filter         (ClutterGstCameraActor  *camera_actor);

gboolean       clutter_gst_camera_actor_is_playing            (ClutterGstCameraActor  *camera_actor);
void           clutter_gst_camera_actor_set_playing           (ClutterGstCameraActor  *camera_actor,
                                                               gboolean                playing);

gboolean       clutter_gst_camera_actor_is_ready_for_capture  (ClutterGstCameraActor  *camera_actor);

void           clutter_gst_camera_actor_set_video_profile     (ClutterGstCameraActor  *camera_actor,
                                                               GstEncodingProfile     *profile);
gboolean       clutter_gst_camera_actor_is_recording_video    (ClutterGstCameraActor  *camera_actor);
gboolean       clutter_gst_camera_actor_start_video_recording (ClutterGstCameraActor  *camera_actor,
                                                               const gchar            *filename);
void           clutter_gst_camera_actor_stop_video_recording  (ClutterGstCameraActor  *camera_actor);

void           clutter_gst_camera_actor_set_photo_profile     (ClutterGstCameraActor  *camera_actor,
                                                               GstEncodingProfile     *profile);
gboolean       clutter_gst_camera_actor_take_photo            (ClutterGstCameraActor  *camera_actor,
                                                               const gchar            *filename);
gboolean       clutter_gst_camera_actor_take_photo_pixbuf     (ClutterGstCameraActor  *camera);

G_END_DECLS

#endif /* __CLUTTER_GST_CAMERA_ACTOR_H__ */
