/*
 * Clutter-GStreamer.
 *
 * GStreamer integration library for Clutter.
 *
 * clutter-gst-camera.c - a GStreamer pipeline to display/manipulate a
 *                        camera stream.
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

/**
 * SECTION:clutter-gst-camera
 * @short_description: A player of camera streams.
 *
 * #ClutterGstCamera implements the #ClutterGstPlayer interface and
 * plays camera streams.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <glib.h>
#include <gio/gio.h>
#include <gst/base/gstbasesink.h>
#include <gst/video/video.h>

#include "clutter-gst-camera.h"
#include "clutter-gst-camera-manager.h"
#include "clutter-gst-debug.h"
#include "clutter-gst-enum-types.h"
#include "clutter-gst-marshal.h"
#include "clutter-gst-player.h"
#include "clutter-gst-private.h"

static const gchar *supported_media_types[] = {
  "video/x-raw",
  NULL
};

struct _ClutterGstCameraPrivate
{
  ClutterGstCameraDevice *camera_device;

  ClutterGstFrame *current_frame;

  GstBus *bus;
  GstElement *camerabin;
  GstElement *camera_source;
  ClutterGstVideoSink *video_sink;

  /* video filter */
  GstElement *video_filter_bin;
  GstElement *identity;
  GstElement *valve;
  GstElement *custom_filter;
  GstElement *gamma;
  GstElement *pre_colorspace;
  GstElement *color_balance;
  GstElement *post_colorspace;

  gboolean is_idle;
  gboolean is_recording;
  gchar *photo_filename;
};

enum
{
  CAPTURE_MODE_IMAGE = 1,
  CAPTURE_MODE_VIDEO
};

enum
{
  PROP_0,

  PROP_IDLE,
  PROP_PLAYING,
  PROP_AUDIO_VOLUME,
  PROP_DEVICE,
};

enum
{
  READY_FOR_CAPTURE,
  PHOTO_SAVED,
  PHOTO_TAKEN,
  VIDEO_SAVED,
  LAST_SIGNAL
};

static int camera_signals[LAST_SIGNAL] = { 0 };

static void player_iface_init (ClutterGstPlayerIface *iface);

G_DEFINE_TYPE_WITH_CODE (ClutterGstCamera, clutter_gst_camera, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (CLUTTER_GST_TYPE_PLAYER, player_iface_init));

/*
 * ClutterGstPlayer implementation
 */

static ClutterGstFrame *
clutter_gst_camera_get_frame (ClutterGstPlayer *self)
{
  ClutterGstCameraPrivate *priv = CLUTTER_GST_CAMERA (self)->priv;

  return priv->current_frame;
}

static GstElement *
clutter_gst_camera_get_pipeline (ClutterGstPlayer *player)
{
  ClutterGstCameraPrivate *priv = CLUTTER_GST_CAMERA (player)->priv;

  return priv->camerabin;
}

static ClutterGstVideoSink *
clutter_gst_camera_get_video_sink (ClutterGstPlayer *player)
{
  ClutterGstCameraPrivate *priv = CLUTTER_GST_CAMERA (player)->priv;

  return priv->video_sink;
}

static gboolean
clutter_gst_camera_get_idle (ClutterGstPlayer *player)
{
  ClutterGstCameraPrivate *priv = CLUTTER_GST_CAMERA (player)->priv;

  return priv->is_idle;
}

static gdouble
clutter_gst_camera_get_audio_volume (ClutterGstPlayer *player)
{
  return 0;
}


static void
clutter_gst_camera_set_audio_volume (ClutterGstPlayer *player,
                                     gdouble           volume)
{
}

static gboolean
clutter_gst_camera_get_playing (ClutterGstPlayer *player)
{
  ClutterGstCameraPrivate *priv = CLUTTER_GST_CAMERA (player)->priv;
  GstState state, pending;
  gboolean playing;

  if (!priv->camerabin)
    return FALSE;

  gst_element_get_state (priv->camerabin, &state, &pending, 0);

  if (pending)
    playing = (pending == GST_STATE_PLAYING);
  else
    playing = (state == GST_STATE_PLAYING);

  return playing;
}

static void
clutter_gst_camera_set_playing (ClutterGstPlayer *player,
                                gboolean          playing)
{
  ClutterGstCameraPrivate *priv = CLUTTER_GST_CAMERA (player)->priv;
  GstState target_state;

  if (!priv->camerabin)
    return;

  target_state = playing ? GST_STATE_PLAYING : GST_STATE_NULL;

  gst_element_set_state (priv->camerabin, target_state);
}

static void
player_iface_init (ClutterGstPlayerIface *iface)
{
  iface->get_frame = clutter_gst_camera_get_frame;
  iface->get_pipeline = clutter_gst_camera_get_pipeline;
  iface->get_video_sink = clutter_gst_camera_get_video_sink;

  iface->get_idle = clutter_gst_camera_get_idle;

  iface->get_audio_volume = clutter_gst_camera_get_audio_volume;
  iface->set_audio_volume = clutter_gst_camera_set_audio_volume;

  iface->get_playing = clutter_gst_camera_get_playing;
  iface->set_playing = clutter_gst_camera_set_playing;
}

/*
 * GObject implementation
 */

static void
clutter_gst_camera_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  switch (property_id)
    {
    case PROP_IDLE:
      g_value_set_boolean (value,
                           clutter_gst_camera_get_idle (CLUTTER_GST_PLAYER (object)));
      break;

    case PROP_PLAYING:
      g_value_set_boolean (value,
                           clutter_gst_camera_get_playing (CLUTTER_GST_PLAYER (object)));
      break;

    case PROP_AUDIO_VOLUME:
      g_value_set_double (value,
                          clutter_gst_camera_get_audio_volume (CLUTTER_GST_PLAYER (object)));
      break;

    case PROP_DEVICE:
      g_value_set_object (value,
                          clutter_gst_camera_get_camera_device (CLUTTER_GST_CAMERA (object)));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
clutter_gst_camera_set_property (GObject      *object,
                                 guint         property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  switch (property_id)
    {
    case PROP_PLAYING:
      clutter_gst_camera_set_playing (CLUTTER_GST_PLAYER (object),
                                      g_value_get_boolean (value));
      break;

    case PROP_AUDIO_VOLUME:
      clutter_gst_camera_set_audio_volume (CLUTTER_GST_PLAYER (object),
                                           g_value_get_double (value));
      break;

    case PROP_DEVICE:
      clutter_gst_camera_set_camera_device (CLUTTER_GST_CAMERA (object),
                                            CLUTTER_GST_CAMERA_DEVICE (g_value_get_object (value)));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
clutter_gst_camera_dispose (GObject *object)
{
  ClutterGstCamera *self = CLUTTER_GST_CAMERA (object);
  ClutterGstCameraPrivate *priv = self->priv;

  g_free (priv->photo_filename);
  priv->photo_filename = NULL;

  g_clear_object (&priv->camera_device);

  if (priv->bus)
    {
      gst_object_unref (priv->bus);
      priv->bus = NULL;
    }

  if (priv->camerabin)
    {
      gst_element_set_state (priv->camerabin, GST_STATE_NULL);
      gst_object_unref (priv->camerabin);
      priv->camerabin = NULL;
    }

  G_OBJECT_CLASS (clutter_gst_camera_parent_class)->dispose (object);
}

static void
clutter_gst_camera_class_init (ClutterGstCameraClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *pspec;

  g_type_class_add_private (klass, sizeof (ClutterGstCameraPrivate));

  object_class->get_property = clutter_gst_camera_get_property;
  object_class->set_property = clutter_gst_camera_set_property;
  object_class->dispose = clutter_gst_camera_dispose;

  g_object_class_override_property (object_class,
                                    PROP_IDLE, "idle");
  g_object_class_override_property (object_class,
                                    PROP_PLAYING, "playing");
  g_object_class_override_property (object_class,
                                    PROP_AUDIO_VOLUME, "audio-volume");


  /**
   * ClutterGstCamera:camera-device:
   *
   * The camera device associated with the camera player.
   */
  pspec = g_param_spec_object ("device",
                               "Device",
                               "Camera Device",
                               CLUTTER_GST_TYPE_CAMERA_DEVICE,
                               CLUTTER_GST_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_DEVICE, pspec);


  /* Signals */

  /**
   * ClutterGstCamera::ready-for-capture:
   * @self: the actor which received the signal
   * @ready: whether the @self is ready for a new capture
   *
   * The ::ready-for-capture signal is emitted whenever the value of
   * clutter_gst_camera_is_ready_for_capture changes.
   */
  camera_signals[READY_FOR_CAPTURE] =
    g_signal_new ("ready-for-capture",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterGstCameraClass, ready_for_capture),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOOLEAN,
                  G_TYPE_NONE, 1,
                  G_TYPE_BOOLEAN);
  /**
   * ClutterGstCamera::photo-saved:
   * @self: the actor which received the signal
   *
   * The ::photo-saved signal is emitted when a photo was saved to disk.
   */
  camera_signals[PHOTO_SAVED] =
    g_signal_new ("photo-saved",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (ClutterGstCameraClass, photo_saved),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
  /**
   * ClutterGstCamera::photo-taken:
   * @self: the actor which received the signal
   * @pixbuf: the photo taken as a #GdkPixbuf
   *
   * The ::photo-taken signal is emitted when a photo was taken.
   */
  camera_signals[PHOTO_TAKEN] =
    g_signal_new ("photo-taken",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (ClutterGstCameraClass, photo_taken),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1, GDK_TYPE_PIXBUF);
  /**
   * ClutterGstCamera::video-saved:
   * @self: the actor which received the signal
   *
   * The ::video-saved signal is emitted when a video was saved to disk.
   */
  camera_signals[VIDEO_SAVED] =
    g_signal_new ("video-saved",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (ClutterGstCameraClass, video_saved),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}

static void
notify_ready_for_capture (GObject               *object,
                          GParamSpec            *pspec,
                          ClutterGstCamera *self)
{
  ClutterGstCameraPrivate *priv = self->priv;
  gboolean ready_for_capture;

  g_object_get (priv->camera_source, "ready-for-capture",
                &ready_for_capture, NULL);
  g_signal_emit (self, camera_signals[READY_FOR_CAPTURE],
                 0, ready_for_capture);
}

static void
parse_photo_data (ClutterGstCamera *self,
                  GstSample        *sample)
{
  ClutterGstCameraPrivate *priv = self->priv;
  GstBuffer *buffer;
  GstCaps *caps;
  const GstStructure *structure;
  gint width, height, stride;
  GdkPixbuf *pixbuf;
  const gint bits_per_pixel = 8;
  guchar *data = NULL;
  GstMapInfo info;

  buffer = gst_sample_get_buffer (sample);
  caps = gst_sample_get_caps (sample);

  gst_buffer_map (buffer, &info, GST_MAP_READ);

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);

  stride = info.size / height;

  data = g_memdup (info.data, info.size);
  pixbuf = gdk_pixbuf_new_from_data (data,
                                     GDK_COLORSPACE_RGB,
                                     FALSE, bits_per_pixel, width, height, stride,
                                     data ? (GdkPixbufDestroyNotify) g_free : NULL, NULL);

  g_object_set (G_OBJECT (priv->camerabin), "post-previews", FALSE, NULL);
  g_signal_emit (self, camera_signals[PHOTO_TAKEN], 0, pixbuf);
  g_object_unref (pixbuf);
}

static void
bus_message_cb (GstBus           *bus,
                GstMessage       *message,
                ClutterGstCamera *self)
{
  ClutterGstCameraPrivate *priv = self->priv;

  switch (GST_MESSAGE_TYPE (message))
    {
    case GST_MESSAGE_ERROR:
      {
        GError *err = NULL;
        gchar *debug = NULL;

        gst_message_parse_error (message, &err, &debug);
        if (err && err->message)
          g_warning ("%s", err->message);
        else
          g_warning ("Unparsable GST_MESSAGE_ERROR message.");

        if (err)
          g_error_free (err);
        g_free (debug);

        priv->is_idle = TRUE;
        g_object_notify (G_OBJECT (self), "idle");
        break;
      }

    case GST_MESSAGE_STATE_CHANGED:
      {
        if (strcmp (GST_MESSAGE_SRC_NAME (message), "camerabin") == 0)
          {
            GstState new;

            gst_message_parse_state_changed (message, NULL, &new, NULL);
            if (new == GST_STATE_PLAYING)
              priv->is_idle = FALSE;
            else
              priv->is_idle = TRUE;
            g_object_notify (G_OBJECT (self), "idle");
          }
        break;
      }

    case GST_MESSAGE_ELEMENT:
      {
        const GstStructure *structure;
        const GValue *image;

        if (strcmp (GST_MESSAGE_SRC_NAME (message), "camera_source") == 0)
          {
            structure = gst_message_get_structure (message);
            if (strcmp (gst_structure_get_name (structure), "preview-image") == 0)
              {
                if (gst_structure_has_field_typed (structure, "sample", GST_TYPE_SAMPLE))
                  {
                    image = gst_structure_get_value (structure, "sample");
                    if (image)
                      {
                        GstSample *sample;

                        sample = gst_value_get_sample (image);
                        parse_photo_data (self, sample);
                      }
                    else
                      g_warning ("Could not get buffer from bus message");
                  }
              }
          }
        else if (strcmp (GST_MESSAGE_SRC_NAME (message), "camerabin") == 0)
          {
            structure = gst_message_get_structure (message);
            if (strcmp (gst_structure_get_name (structure), "image-done") == 0)
              {
                const gchar *filename = gst_structure_get_string (structure, "filename");
                if (priv->photo_filename != NULL && filename != NULL &&
                    (strcmp (priv->photo_filename, filename) == 0))
                  g_signal_emit (self, camera_signals[PHOTO_SAVED], 0);
              }
            else if (strcmp (gst_structure_get_name (structure), "video-done") == 0)
              {
                g_signal_emit (self, camera_signals[VIDEO_SAVED], 0);
                priv->is_recording = FALSE;
              }
          }
        break;
      }

    default:
      break;
    }
}

static void
set_video_profile (ClutterGstCamera *self)
{
  GstEncodingContainerProfile *prof;
  GstEncodingAudioProfile *audio_prof;
  GstEncodingVideoProfile *video_prof;
  GstCaps *caps;

  caps = gst_caps_from_string ("application/ogg");
  prof = gst_encoding_container_profile_new ("Ogg audio/video",
                                             "Standard Ogg/Theora/Vorbis",
                                             caps, NULL);
  gst_caps_unref (caps);

  caps = gst_caps_from_string ("video/x-theora");
  video_prof = gst_encoding_video_profile_new (caps, NULL, NULL, 0);
  gst_encoding_container_profile_add_profile (prof, (GstEncodingProfile*) video_prof);
  gst_caps_unref (caps);

  caps = gst_caps_from_string ("audio/x-vorbis");
  audio_prof = gst_encoding_audio_profile_new (caps, NULL, NULL, 0);
  gst_encoding_container_profile_add_profile (prof, (GstEncodingProfile*) audio_prof);
  gst_caps_unref (caps);

  clutter_gst_camera_set_video_profile (self,
                                        (GstEncodingProfile *) prof);

  gst_encoding_profile_unref (prof);
}

static GstElement *
setup_video_filter_bin (ClutterGstCamera *self)
{
  ClutterGstCameraPrivate *priv = self->priv;
  GstElement *bin;
  GstPad *pad;

  if ((priv->identity = gst_element_factory_make ("identity", "identity")) == NULL)
    goto error;
  if ((priv->valve = gst_element_factory_make ("valve", "valve")) == NULL)
    goto error;
  if ((priv->gamma = gst_element_factory_make ("gamma", "gamma")) == NULL)
    goto error;
  if ((priv->pre_colorspace = gst_element_factory_make ("videoconvert", "pre_colorspace")) == NULL)
    goto error;
  if ((priv->color_balance = gst_element_factory_make ("videobalance", "color_balance")) == NULL)
    goto error;
  if ((priv->post_colorspace = gst_element_factory_make ("videoconvert", "post_colorspace")) == NULL)
    goto error;

  bin = gst_bin_new ("video_filter_bin");
  gst_bin_add_many (GST_BIN (bin),
                    priv->identity, priv->valve, priv->gamma,
                    priv->pre_colorspace, priv->color_balance, priv->post_colorspace,
                    NULL);

  if (!gst_element_link_many (priv->identity, priv->valve, priv->gamma,
                              priv->pre_colorspace, priv->color_balance, priv->post_colorspace,
                              NULL))
    goto error_not_linked;

  pad = gst_element_get_static_pad (priv->post_colorspace, "src");
  gst_element_add_pad (bin, gst_ghost_pad_new ("src", pad));
  gst_object_unref (pad);

  pad = gst_element_get_static_pad (priv->identity, "sink");
  gst_element_add_pad (bin, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (pad);
  return bin;

 error:
  if (priv->identity)
    gst_object_unref (priv->identity);
  if (priv->valve)
    gst_object_unref (priv->valve);
  if (priv->gamma)
    gst_object_unref (priv->gamma);
  if (priv->pre_colorspace)
    gst_object_unref (priv->pre_colorspace);
  if (priv->color_balance)
    gst_object_unref (priv->color_balance);
  if (priv->post_colorspace)
    gst_object_unref (priv->post_colorspace);
  return NULL;

 error_not_linked:
  gst_object_unref (bin);
  return NULL;
}

static GstCaps *
create_caps_for_formats (gint width,
                         gint height)
{
  GstCaps *ret = NULL;
  guint length;
  guint i;

  length = g_strv_length ((gchar **) supported_media_types);
  for (i = 0; i < length; i++)
    {
      GstCaps *caps;

      caps = gst_caps_new_simple (supported_media_types[i],
                                  "width", G_TYPE_INT, width,
                                  "height", G_TYPE_INT, height,
                                  NULL);
      if (!ret)
        ret = caps;
      else
        gst_caps_append (ret, caps);
    }
  return ret;
}

static void
device_capture_resolution_changed (ClutterGstCameraDevice *camera_device,
                                   gint                    width,
                                   gint                    height,
                                   ClutterGstCamera       *self)
{
  ClutterGstCameraPrivate *priv = self->priv;
  GstCaps *caps;

  if (priv->camera_device != camera_device)
    return;

  caps = create_caps_for_formats (width, height);
  g_object_set (G_OBJECT (priv->camerabin), "video-capture-caps", caps, NULL);
  g_object_set (G_OBJECT (priv->camerabin), "image-capture-caps", caps, NULL);
  g_object_set (G_OBJECT (priv->camerabin), "viewfinder-caps", caps, NULL);
  gst_caps_unref (caps);
}

static void
set_device_resolutions (ClutterGstCamera       *self,
                        ClutterGstCameraDevice *device)

{
  gint width;
  gint height;

  clutter_gst_camera_device_get_capture_resolution (device, &width, &height);
  device_capture_resolution_changed (device, width, height, self);
}

static gboolean
setup_camera_source (ClutterGstCamera *self)
{
  ClutterGstCameraPrivate *priv = self->priv;
  GstElement *camera_source;

  if (priv->camera_source)
    return TRUE;

  camera_source = gst_element_factory_make ("wrappercamerabinsrc", "camera_source");
  if (G_UNLIKELY (!camera_source))
    {
      g_critical ("Unable to create wrappercamerabinsrc element");
      return FALSE;
    }

  priv->camera_source = camera_source;
  g_object_set (priv->camerabin, "camera-source", camera_source, NULL);

  g_signal_connect (camera_source, "notify::ready-for-capture",
                    G_CALLBACK (notify_ready_for_capture),
                    self);

  if (priv->video_filter_bin)
    {
      g_object_set (G_OBJECT (camera_source),
                    "video-source-filter", priv->video_filter_bin,
                    NULL);
    }

  return TRUE;
}

static void
_new_frame_from_pipeline (ClutterGstVideoSink *sink, ClutterGstCamera *self)
{
  ClutterGstCameraPrivate *priv = self->priv;

  clutter_gst_player_update_frame (CLUTTER_GST_PLAYER (self),
                                   &priv->current_frame,
                                   clutter_gst_video_sink_get_frame (sink));
}

static void
_ready_from_pipeline (ClutterGstVideoSink *sink, ClutterGstCamera *self)
{
  g_signal_emit_by_name (self, "ready");
}

static void
_pixel_aspect_ratio_changed (ClutterGstVideoSink *sink,
                             GParamSpec          *spec,
                             ClutterGstCamera    *self)
{
  clutter_gst_frame_update_pixel_aspect_ratio (self->priv->current_frame, sink);
}

static gboolean
setup_pipeline (ClutterGstCamera *self)
{
  ClutterGstCameraPrivate *priv = self->priv;
  const GPtrArray *camera_devices =
    clutter_gst_camera_manager_get_camera_devices (clutter_gst_camera_manager_get_default ());



  priv->camerabin = gst_element_factory_make ("camerabin", "camerabin");
  if (G_UNLIKELY (!priv->camerabin))
    {
      g_critical ("Unable to create camerabin element");
      return FALSE;
    }

  priv->video_filter_bin = setup_video_filter_bin (self);
  if (!priv->video_filter_bin)
    g_warning ("Unable to setup video filter, some features will be disabled");

  if (G_UNLIKELY (!setup_camera_source (self)))
    {
      g_critical ("Unable to create camera source element");
      gst_object_unref (priv->camerabin);
      priv->camerabin = 0;
      return FALSE;
    }

  if (camera_devices->len > 0 &&
      !clutter_gst_camera_set_camera_device (self,
                                             g_ptr_array_index (camera_devices, 0)))
    {
      g_critical ("Unable to select capture device");
      gst_object_unref (priv->camerabin);
      priv->camerabin = 0;
      return FALSE;
    }

  priv->video_sink = clutter_gst_video_sink_new ();

  g_signal_connect (priv->video_sink, "new-frame",
                    G_CALLBACK (_new_frame_from_pipeline), self);
  g_signal_connect (priv->video_sink, "pipeline-ready",
                    G_CALLBACK (_ready_from_pipeline), self);
  g_signal_connect (priv->video_sink, "notify::pixel-aspect-ratio",
                    G_CALLBACK (_pixel_aspect_ratio_changed), self);


  g_object_set (priv->camerabin,
                "viewfinder-sink", priv->video_sink,
                NULL);

  set_video_profile (self);

  priv->bus = gst_element_get_bus (priv->camerabin);
  gst_bus_add_signal_watch (priv->bus);

  g_signal_connect (G_OBJECT (priv->bus), "message",
                    G_CALLBACK (bus_message_cb), self);

  return TRUE;
}

static void
clutter_gst_camera_init (ClutterGstCamera *self)
{
  ClutterGstCameraPrivate *priv;

  self->priv = priv =
    G_TYPE_INSTANCE_GET_PRIVATE (self,
                                 CLUTTER_GST_TYPE_CAMERA,
                                 ClutterGstCameraPrivate);

  if (!setup_pipeline (self))
    {
      g_warning ("Failed to initiate suitable elements for pipeline.");
      return;
    }

  priv->current_frame = clutter_gst_create_blank_frame (NULL);

  priv->is_idle = TRUE;
}

/*
 * Public symbols
 */

/**
 * clutter_gst_camera_new:
 *
 * Create a camera actor.
 *
 * <note>This function has to be called from Clutter's main thread. While
 * GStreamer will spawn threads to do its work, we want all the GL calls to
 * happen in the same thread. Clutter-gst knows which thread it is by
 * assuming this constructor is called from the Clutter thread.</note>
 *
 * Return value: the newly created camera actor
 */
ClutterGstCamera *
clutter_gst_camera_new (void)
{
  return g_object_new (CLUTTER_GST_TYPE_CAMERA,
                       NULL);
}

/* /\** */
/*  * clutter_gst_camera_get_pipeline: */
/*  * @self: a #ClutterGstCamera */
/*  * */
/*  * Retrieve the #GstPipeline used by the @self, for direct use with */
/*  * GStreamer API. */
/*  * */
/*  * Return value: (transfer none): the pipeline element used by the camera actor */
/*  *\/ */
/* GstElement * */
/* clutter_gst_camera_get_pipeline (ClutterGstCamera *self) */
/* { */
/*   g_return_val_if_fail (CLUTTER_GST_IS_CAMERA (self), NULL); */

/*   return self->priv->camerabin; */
/* } */

/* /\** */
/*  * clutter_gst_camera_get_camerabin: */
/*  * @self: a #ClutterGstCamera */
/*  * */
/*  * Retrieve the camerabin element used by the @self, for direct use with */
/*  * GStreamer API. */
/*  * */
/*  * Return value: (transfer none): the pipeline element used by the camera actor */
/*  *\/ */
/* GstElement * */
/* clutter_gst_camera_get_camerabin (ClutterGstCamera *self) */
/* { */
/*   g_return_val_if_fail (CLUTTER_GST_IS_CAMERA (self), NULL); */

/*   return self->priv->camerabin; */
/* } */

/**
 * clutter_gst_camera_get_camera_device:
 * @self: a #ClutterGstCamera
 *
 * Retrieve the current selected camera device.
 *
 * Return value: (transfer none): The currently selected camera device
 */
ClutterGstCameraDevice *
clutter_gst_camera_get_camera_device (ClutterGstCamera *self)
{
  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA (self), NULL);

  return self->priv->camera_device;
}

/**
 * clutter_gst_camera_set_camera_device:
 * @self: a #ClutterGstCamera
 * @device: a #ClutterGstCameraDevice
 *
 * Set the new active camera device.
 *
 * Return value: %TRUE on success, %FALSE otherwise
 */
gboolean
clutter_gst_camera_set_camera_device (ClutterGstCamera       *self,
                                      ClutterGstCameraDevice *device)
{
  ClutterGstCameraPrivate *priv;
  GstElementFactory *element_factory;
  GstElement *src;
  gchar *node;
  gboolean was_playing = FALSE;

  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA (self), FALSE);
  g_return_val_if_fail (device != NULL, FALSE);

  priv = self->priv;

  if (!priv->camerabin)
    return FALSE;

  if (priv->is_recording)
    clutter_gst_camera_stop_video_recording (self);

  if (clutter_gst_camera_get_playing (CLUTTER_GST_PLAYER (self)))
    {
      gst_element_set_state (priv->camerabin, GST_STATE_NULL);
      was_playing = TRUE;
    }

  g_object_get (device,
                "element-factory", &element_factory,
                "node", &node,
                NULL);
  src = gst_element_factory_create (element_factory, NULL);
  if (!src)
    {
      g_warning ("Unable to create device source for "
                 "capture device %s (using factory %s)",
                 node, gst_object_get_name (GST_OBJECT (element_factory)));

      return FALSE;
    }

#if 0
  g_print ("Setting active device to %s (using factory %s)\n",
           node,
           gst_object_get_name (GST_OBJECT (element_factory)));
#endif

  gst_object_unref (element_factory);

  if (priv->camera_device)
    {
      g_signal_handlers_disconnect_by_func (priv->camera_device,
                                            device_capture_resolution_changed,
                                            self);
      g_clear_object (&priv->camera_device);
    }

  priv->camera_device = g_object_ref (device);

  g_object_set (G_OBJECT (src), "device", node, NULL);
  g_free (node);
  g_object_set (G_OBJECT (priv->camera_source), "video-source", src, NULL);

  g_signal_connect (device, "capture-resolution-changed",
                    G_CALLBACK (device_capture_resolution_changed),
                    self);

  set_device_resolutions (self, device);

  if (was_playing)
    gst_element_set_state (priv->camerabin, GST_STATE_PLAYING);

  return TRUE;
}

/**
 * clutter_gst_camera_supports_gamma_correction:
 * @self: a #ClutterGstCamera
 *
 * Check whether the @self supports gamma correction.
 *
 * Return value: %TRUE if @self supports gamma correction, %FALSE otherwise
 */
gboolean
clutter_gst_camera_supports_gamma_correction (ClutterGstCamera *self)
{
  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA (self), FALSE);

  return (self->priv->gamma != NULL);
}

/**
 * clutter_gst_camera_get_gamma_range:
 * @self: a #ClutterGstCamera
 * @min_value: Pointer to store the minimum gamma value, or %NULL
 * @max_value: Pointer to store the maximum gamma value, or %NULL
 * @default_value: Pointer to store the default gamma value, or %NULL
 *
 * Retrieve the minimum, maximum and default gamma values.
 *
 * This method will return FALSE if gamma correction is not
 * supported on @self.
 * See clutter_gst_camera_supports_gamma_correction().
 *
 * Return value: %TRUE if successful, %FALSE otherwise
 */
gboolean
clutter_gst_camera_get_gamma_range (ClutterGstCamera *self,
                                    gdouble          *min_value,
                                    gdouble          *max_value,
                                    gdouble          *default_value)
{
  ClutterGstCameraPrivate *priv;
  GParamSpec *pspec;

  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA (self), FALSE);

  priv = self->priv;

  if (!priv->gamma)
    return FALSE;

  pspec = g_object_class_find_property (
                                        G_OBJECT_GET_CLASS (G_OBJECT (priv->gamma)), "gamma");
  /* shouldn't happen */
  g_return_val_if_fail (G_IS_PARAM_SPEC_DOUBLE (pspec), FALSE);

  if (min_value)
    *min_value = G_PARAM_SPEC_DOUBLE (pspec)->minimum;
  if (max_value)
    *max_value = G_PARAM_SPEC_DOUBLE (pspec)->maximum;
  if (default_value)
    *default_value = G_PARAM_SPEC_DOUBLE (pspec)->default_value;
  return TRUE;
}

/**
 * clutter_gst_camera_get_gamma:
 * @self: a #ClutterGstCamera
 * @cur_value: Pointer to store the current gamma value
 *
 * Retrieve the current gamma value.
 *
 * This method will return FALSE if gamma correction is not
 * supported on @self.
 * See clutter_gst_camera_supports_gamma_correction().
 *
 * Return value: %TRUE if successful, %FALSE otherwise
 */
gboolean
clutter_gst_camera_get_gamma (ClutterGstCamera *self,
                              gdouble          *cur_value)
{
  ClutterGstCameraPrivate *priv;

  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA (self), FALSE);
  g_return_val_if_fail (cur_value != NULL, FALSE);

  priv = self->priv;

  if (!priv->gamma)
    return FALSE;

  g_object_get (G_OBJECT (priv->gamma), "gamma", cur_value, NULL);
  return TRUE;
}

/**
 * clutter_gst_camera_set_gamma:
 * @self: a #ClutterGstCamera
 * @value: The value to set
 *
 * Set the gamma value.
 * Allowed values can be retrieved with
 * clutter_gst_camera_get_gamma_range().
 *
 * This method will return FALSE if gamma correction is not
 * supported on @self.
 * See clutter_gst_camera_supports_gamma_correction().
 *
 * Return value: %TRUE if successful, %FALSE otherwise
 */
gboolean
clutter_gst_camera_set_gamma (ClutterGstCamera *self,
                              gdouble           value)
{
  ClutterGstCameraPrivate *priv;

  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA (self), FALSE);

  priv = self->priv;

  if (!priv->gamma)
    return FALSE;

  g_object_set (G_OBJECT (priv->gamma), "gamma", value, NULL);
  return TRUE;
}

/**
 * clutter_gst_camera_supports_color_balance:
 * @self: a #ClutterGstCamera
 *
 * Check whether the @self supports color balance.
 *
 * Return value: %TRUE if @self supports color balance, %FALSE otherwise
 */
gboolean
clutter_gst_camera_supports_color_balance (ClutterGstCamera *self)
{
  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA (self), FALSE);

  return (self->priv->color_balance != NULL);
}

/**
 * clutter_gst_camera_get_color_balance_property_range:
 * @self: a #ClutterGstCamera
 * @property: Property name
 * @min_value: Pointer to store the minimum value of @property, or %NULL
 * @max_value: Pointer to store the maximum value of @property, or %NULL
 * @default_value: Pointer to store the default value of @property, or %NULL
 *
 * Retrieve the minimum, maximum and default values for the color balance property @property,
 *
 * This method will return FALSE if @property does not exist or color balance is not
 * supported on @self.
 * See clutter_gst_camera_supports_color_balance().
 *
 * Return value: %TRUE if successful, %FALSE otherwise
 */
gboolean
clutter_gst_camera_get_color_balance_property_range (ClutterGstCamera *self,
                                                     const gchar      *property,
                                                     gdouble          *min_value,
                                                     gdouble          *max_value,
                                                     gdouble          *default_value)
{
  ClutterGstCameraPrivate *priv;
  GParamSpec *pspec;

  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA (self), FALSE);

  priv = self->priv;

  if (!priv->color_balance)
    return FALSE;

  pspec = g_object_class_find_property (
                                        G_OBJECT_GET_CLASS (G_OBJECT (priv->color_balance)), property);
  g_return_val_if_fail (G_IS_PARAM_SPEC_DOUBLE (pspec), FALSE);

  if (min_value)
    *min_value = G_PARAM_SPEC_DOUBLE (pspec)->minimum;
  if (max_value)
    *max_value = G_PARAM_SPEC_DOUBLE (pspec)->maximum;
  if (default_value)
    *default_value = G_PARAM_SPEC_DOUBLE (pspec)->default_value;
  return TRUE;
}

/**
 * clutter_gst_camera_get_color_balance_property:
 * @self: a #ClutterGstCamera
 * @property: Property name
 * @cur_value: Pointer to store the current value of @property
 *
 * Retrieve the current value for the color balance property @property,
 *
 * This method will return FALSE if @property does not exist or color balance is not
 * supported on @self.
 * See clutter_gst_camera_supports_color_balance().
 *
 * Return value: %TRUE if successful, %FALSE otherwise
 */
gboolean
clutter_gst_camera_get_color_balance_property (ClutterGstCamera *self,
                                               const gchar      *property,
                                               gdouble          *cur_value)
{
  ClutterGstCameraPrivate *priv;
  GParamSpec *pspec;

  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA (self), FALSE);
  g_return_val_if_fail (cur_value != NULL, FALSE);

  priv = self->priv;

  if (!priv->color_balance)
    return FALSE;

  pspec = g_object_class_find_property (
                                        G_OBJECT_GET_CLASS (G_OBJECT (priv->color_balance)), property);
  g_return_val_if_fail (G_IS_PARAM_SPEC_DOUBLE (pspec), FALSE);

  g_object_get (G_OBJECT (priv->color_balance), property, cur_value, NULL);
  return TRUE;
}

/**
 * clutter_gst_camera_set_color_balance_property:
 * @self: a #ClutterGstCamera
 * @property: Property name
 * @value: The value to set
 *
 * Set the value for the color balance property @property to @value.
 * Allowed values can be retrieved with
 * clutter_gst_camera_get_color_balance_property_range().
 *
 * This method will return FALSE if @property does not exist or color balance is not
 * supported on @self.
 * See clutter_gst_camera_supports_color_balance().
 *
 * Return value: %TRUE if successful, %FALSE otherwise
 */
gboolean
clutter_gst_camera_set_color_balance_property (ClutterGstCamera *self,
                                               const gchar      *property,
                                               gdouble           value)
{
  ClutterGstCameraPrivate *priv;
  GParamSpec *pspec;

  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA (self), FALSE);

  priv = self->priv;

  if (!priv->color_balance)
    return FALSE;

  pspec = g_object_class_find_property (
                                        G_OBJECT_GET_CLASS (G_OBJECT (priv->color_balance)), property);
  g_return_val_if_fail (G_IS_PARAM_SPEC_DOUBLE (pspec), FALSE);

  g_object_set (G_OBJECT (priv->color_balance), property, value, NULL);
  return TRUE;
}

gboolean
clutter_gst_camera_get_brightness_range (ClutterGstCamera *self,
                                         gdouble          *min_value,
                                         gdouble          *max_value,
                                         gdouble          *default_value)
{
  return clutter_gst_camera_get_color_balance_property_range (self,
                                                              "brightness", min_value, max_value, default_value);
}

gboolean
clutter_gst_camera_get_brightness (ClutterGstCamera *self,
                                   gdouble          *cur_value)
{
  return clutter_gst_camera_get_color_balance_property (self,
                                                        "brightness", cur_value);
}

gboolean
clutter_gst_camera_set_brightness (ClutterGstCamera *self,
                                   gdouble           value)
{
  return clutter_gst_camera_set_color_balance_property (self,
                                                        "brightness", value);
}

gboolean
clutter_gst_camera_get_contrast_range (ClutterGstCamera *self,
                                       gdouble          *min_value,
                                       gdouble          *max_value,
                                       gdouble          *default_value)
{
  return clutter_gst_camera_get_color_balance_property_range (self,
                                                              "contrast", min_value, max_value, default_value);
}

gboolean
clutter_gst_camera_get_contrast (ClutterGstCamera *self,
                                 gdouble          *cur_value)
{
  return clutter_gst_camera_get_color_balance_property (self,
                                                        "contrast", cur_value);
}

gboolean
clutter_gst_camera_set_contrast (ClutterGstCamera *self,
                                 gdouble           value)
{
  return clutter_gst_camera_set_color_balance_property (self,
                                                        "contrast", value);
}

gboolean
clutter_gst_camera_get_saturation_range (ClutterGstCamera *self,
                                         gdouble          *min_value,
                                         gdouble          *max_value,
                                         gdouble          *default_value)
{
  return clutter_gst_camera_get_color_balance_property_range (self,
                                                              "saturation", min_value, max_value, default_value);
}

gboolean
clutter_gst_camera_get_saturation (ClutterGstCamera *self,
                                   gdouble          *cur_value)
{
  return clutter_gst_camera_get_color_balance_property (self,
                                                        "saturation", cur_value);
}

gboolean
clutter_gst_camera_set_saturation (ClutterGstCamera *self,
                                   gdouble           value)
{
  return clutter_gst_camera_set_color_balance_property (self,
                                                        "saturation", value);
}

gboolean
clutter_gst_camera_get_hue_range (ClutterGstCamera *self,
                                  gdouble          *min_value,
                                  gdouble          *max_value,
                                  gdouble          *default_value)
{
  return clutter_gst_camera_get_color_balance_property_range (self,
                                                              "hue", min_value, max_value, default_value);
}

gboolean
clutter_gst_camera_get_hue (ClutterGstCamera *self,
                            gdouble          *cur_value)
{
  return clutter_gst_camera_get_color_balance_property (self,
                                                        "hue", cur_value);
}

gboolean
clutter_gst_camera_set_hue (ClutterGstCamera *self,
                            gdouble           value)
{
  return clutter_gst_camera_set_color_balance_property (self,
                                                        "hue", value);
}

/**
 * clutter_gst_camera_get_filter:
 * @self: a #ClutterGstCamera
 *
 * Retrieve the current filter being used.
 *
 * Return value: (transfer none): The current filter or %NULL if none is set
 */
GstElement *
clutter_gst_camera_get_filter (ClutterGstCamera  *self)
{
  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA (self), FALSE);

  return self->priv->custom_filter;
}

static GstElement *
create_filter_bin (GstElement *filter)
{
  GstElement *filter_bin = NULL;
  GstElement *pre_filter_colorspace;
  GstElement *post_filter_colorspace;
  GstPad *pad;

  if ((pre_filter_colorspace = gst_element_factory_make ("videoconvert", "pre_filter_colorspace")) == NULL)
    goto err;
  if ((post_filter_colorspace = gst_element_factory_make ("videoconvert", "post_filter_colorspace")) == NULL)
    goto err;

  filter_bin = gst_bin_new ("custom_filter_bin");
  gst_bin_add_many (GST_BIN (filter_bin), pre_filter_colorspace,
                    filter, post_filter_colorspace, NULL);
  if (!gst_element_link_many (pre_filter_colorspace,
                              filter, post_filter_colorspace, NULL))
    goto err_not_linked;

  pad = gst_element_get_static_pad (pre_filter_colorspace, "sink");
  gst_element_add_pad (filter_bin, gst_ghost_pad_new ("sink", pad));
  gst_object_unref (GST_OBJECT (pad));

  pad = gst_element_get_static_pad (post_filter_colorspace, "src");
  gst_element_add_pad (filter_bin, gst_ghost_pad_new ("src", pad));
  gst_object_unref (GST_OBJECT (pad));

 out:
  return filter_bin;

 err:
  if (pre_filter_colorspace)
    gst_object_unref (pre_filter_colorspace);
  if (post_filter_colorspace)
    gst_object_unref (post_filter_colorspace);
  goto out;

 err_not_linked:
  gst_object_unref (filter_bin);
  filter_bin = NULL;
  goto out;
}

/**
 * clutter_gst_camera_set_filter:
 * @self: a #ClutterGstCamera
 * @filter: a #GstElement for the filter
 *
 * Set the filter element to be used.
 * Filters can be used for effects, image processing, etc.
 *
 * Return value: %TRUE on success, %FALSE otherwise
 */
gboolean
clutter_gst_camera_set_filter (ClutterGstCamera *self,
                               GstElement       *filter)
{
  ClutterGstCameraPrivate *priv;
  gboolean ret = FALSE;

  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA (self), FALSE);

  priv = self->priv;

  if (!priv->custom_filter && !filter)
    {
      /* nothing to do here, we don't have a filter and NULL
       * was passed as new filter */
      return TRUE;
    }

  g_object_set (G_OBJECT (priv->valve), "drop", TRUE, NULL);

  if (priv->custom_filter)
    {
      /* remove current filter if any */
      gst_element_unlink_many (priv->valve, priv->custom_filter,
                               priv->gamma, NULL);
      g_object_ref (priv->custom_filter);
      gst_bin_remove (GST_BIN (priv->video_filter_bin), priv->custom_filter);
      gst_element_set_state (priv->custom_filter, GST_STATE_NULL);
      g_object_unref (priv->custom_filter);
      priv->custom_filter = NULL;
    }
  else
    {
      /* we have no current filter,
       * unlink valve and gamma to set the new filter */
      gst_element_unlink (priv->valve, priv->gamma);
    }

  if (filter)
    {
      priv->custom_filter = create_filter_bin (filter);
      if (!priv->custom_filter)
        goto err_restore;

      gst_bin_add (GST_BIN (priv->video_filter_bin), priv->custom_filter);
      if (!gst_element_link_many (priv->valve, priv->custom_filter,
                                  priv->gamma, NULL))
        {
          /* removing will also unref it */
          gst_bin_remove (GST_BIN (priv->video_filter_bin),
                          priv->custom_filter);
          priv->custom_filter = NULL;
          goto err_restore;
        }

      if (clutter_gst_camera_get_playing (CLUTTER_GST_PLAYER (self)))
        gst_element_set_state (priv->custom_filter, GST_STATE_PLAYING);
    }
  else
    gst_element_link (priv->valve, priv->gamma);

  ret = TRUE;

 out:
  g_object_set (G_OBJECT (priv->valve), "drop", FALSE, NULL);
  return ret;

 err_restore:
  ret = FALSE;
  /* restore default pipeline, should always work */
  gst_element_link (priv->valve, priv->gamma);
  goto out;
}

/**
 * clutter_gst_camera_remove_filter:
 * @self: a #ClutterGstCamera
 *
 * Remove the current filter, if any.
 *
 * Return value: %TRUE on success, %FALSE otherwise
 */
gboolean
clutter_gst_camera_remove_filter (ClutterGstCamera *self)
{
  return clutter_gst_camera_set_filter (self, NULL);
}

/**
 * clutter_gst_camera_is_ready_for_capture:
 * @self: a #ClutterGstCamera
 *
 * Check whether the @self is ready for video/photo capture.
 *
 * Return value: %TRUE if @self is ready for capture, %FALSE otherwise
 */
gboolean
clutter_gst_camera_is_ready_for_capture (ClutterGstCamera *self)
{
  ClutterGstCameraPrivate *priv;
  gboolean ready_for_capture;

  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA (self), FALSE);

  priv = self->priv;

  g_object_get (priv->camera_source, "ready-for-capture", &ready_for_capture, NULL);

  return ready_for_capture;
}

/**
 * clutter_gst_camera_is_recording_video:
 * @self: a #ClutterGstCamera
 *
 * Check whether the @self is recording video.
 *
 * Return value: %TRUE if @self is recording video, %FALSE otherwise
 */
gboolean
clutter_gst_camera_is_recording_video (ClutterGstCamera *self)
{
  ClutterGstCameraPrivate *priv;

  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA (self), FALSE);

  priv = self->priv;

  return priv->is_recording;
}

/**
 * clutter_gst_camera_set_video_profile:
 * @self: a #ClutterGstCamera
 * @profile: A #GstEncodingProfile to be used for video recording.
 *
 * Set the encoding profile to be used for video recording.
 * The default profile saves videos as Ogg/Theora videos.
 */
void
clutter_gst_camera_set_video_profile (ClutterGstCamera   *self,
                                      GstEncodingProfile *profile)
{
  ClutterGstCameraPrivate *priv;

  g_return_if_fail (CLUTTER_GST_IS_CAMERA (self));

  priv = self->priv;

  if (!priv->camerabin)
    return;

  g_object_set (priv->camerabin, "video-profile", profile, NULL);
}

/**
 * clutter_gst_camera_start_video_recording:
 * @self: a #ClutterGstCamera
 * @filename: (type filename): the name of the video file to where the
 * recording will be saved
 *
 * Start a video recording with the @self and save it to @filename.
 * This method requires that @self is playing and ready for capture.
 *
 * The ::video-saved signal will be emitted when the video is saved.
 *
 * Return value: %TRUE if the video recording was successfully started, %FALSE otherwise
 */
gboolean
clutter_gst_camera_start_video_recording (ClutterGstCamera *self,
                                          const gchar           *filename)
{
  ClutterGstCameraPrivate *priv;

  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA (self), FALSE);

  priv = self->priv;

  if (!priv->camerabin)
    return FALSE;

  if (priv->is_recording)
    return TRUE;

  if (!clutter_gst_camera_get_playing (CLUTTER_GST_PLAYER (self)))
    return FALSE;

  if (!clutter_gst_camera_is_ready_for_capture (self))
    return FALSE;

  g_object_set (priv->camerabin, "mode", CAPTURE_MODE_VIDEO, NULL);
  g_object_set (priv->camerabin, "location", filename, NULL);
  g_signal_emit_by_name (priv->camerabin, "start-capture");
  priv->is_recording = TRUE;
  return TRUE;
}

/**
 * clutter_gst_camera_stop_video_recording:
 * @self: a #ClutterGstCamera
 *
 * Stop recording video on the @self.
 */
void
clutter_gst_camera_stop_video_recording (ClutterGstCamera *self)
{
  ClutterGstCameraPrivate *priv;
  GstState state;

  g_return_if_fail (CLUTTER_GST_IS_CAMERA (self));

  priv = self->priv;

  if (!priv->camerabin)
    return;

  if (!priv->is_recording)
    return;

  if (!clutter_gst_camera_get_playing (CLUTTER_GST_PLAYER (self)))
    return;

  gst_element_get_state (priv->camerabin, &state, NULL, 0);

  if (state == GST_STATE_PLAYING)
    g_signal_emit_by_name (priv->camerabin, "stop-capture");
  else if (priv->is_recording)
    {
      g_warning ("Cannot cleanly shutdown recording pipeline, forcing");

      gst_element_set_state (priv->camerabin, GST_STATE_NULL);
      gst_element_set_state (priv->camerabin, GST_STATE_PLAYING);
      priv->is_recording = FALSE;
    }
}

/**
 * clutter_gst_camera_set_photo_profile:
 * @self: a #ClutterGstCamera
 * @profile: A #GstEncodingProfile to be used for photo captures.
 *
 * Set the encoding profile to be used for photo captures.
 * The default profile saves photos as JPEG images.
 */
void
clutter_gst_camera_set_photo_profile (ClutterGstCamera *self,
                                      GstEncodingProfile    *profile)
{
  ClutterGstCameraPrivate *priv;

  g_return_if_fail (CLUTTER_GST_IS_CAMERA (self));

  priv = self->priv;

  if (!priv->camerabin)
    return;

  g_object_set (priv->camerabin, "image-profile", profile, NULL);
}

/**
 * clutter_gst_camera_take_photo:
 * @self: a #ClutterGstCamera
 * @filename: (type filename): the name of the file to where the
 * photo will be saved
 *
 * Take a photo with the @self and save it to @filename.
 * This method requires that @self is playing and ready for capture.
 *
 * The ::photo-saved signal will be emitted when the video is saved.
 *
 * Return value: %TRUE if the photo was successfully captured, %FALSE otherwise
 */
gboolean
clutter_gst_camera_take_photo (ClutterGstCamera *self,
                               const gchar      *filename)
{
  ClutterGstCameraPrivate *priv;

  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA (self), FALSE);
  g_return_val_if_fail (filename != NULL, FALSE);

  priv = self->priv;

  if (!priv->camerabin)
    return FALSE;

  if (!clutter_gst_camera_get_playing (CLUTTER_GST_PLAYER (self)))
    return FALSE;

  if (!clutter_gst_camera_is_ready_for_capture (self))
    return FALSE;

  g_free (priv->photo_filename);
  priv->photo_filename = g_strdup (filename);

  /* Take the photo */
  g_object_set (priv->camerabin, "location", filename, NULL);
  g_object_set (priv->camerabin, "mode", CAPTURE_MODE_IMAGE, NULL);
  g_signal_emit_by_name (priv->camerabin, "start-capture");
  return TRUE;
}

/**
 * clutter_gst_camera_take_photo_pixbuf:
 * @self: a #ClutterGstCamera
 *
 * Take a photo with the @self and emit it in the ::photo-taken signal as a
 * #GdkPixbuf.
 * This method requires that @self is playing and ready for capture.
 *
 * Return value: %TRUE if the photo was successfully captured, %FALSE otherwise
 */
gboolean
clutter_gst_camera_take_photo_pixbuf (ClutterGstCamera *self)
{
  ClutterGstCameraPrivate *priv;
  GstCaps *caps;

  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA (self), FALSE);

  priv = self->priv;

  if (!priv->camerabin)
    return FALSE;

  if (!clutter_gst_camera_get_playing (CLUTTER_GST_PLAYER (self)))
    return FALSE;

  if (!clutter_gst_camera_is_ready_for_capture (self))
    return FALSE;

  caps = gst_caps_new_simple ("video/x-raw",
                              "bpp", G_TYPE_INT, 24,
                              "depth", G_TYPE_INT, 24,
                              NULL);
  g_object_set (G_OBJECT (priv->camerabin), "post-previews", TRUE, NULL);
  g_object_set (G_OBJECT (priv->camerabin), "preview-caps", caps, NULL);
  gst_caps_unref (caps);

  g_free (priv->photo_filename);
  priv->photo_filename = NULL;

  /* Take the photo */
  g_object_set (priv->camerabin, "location", NULL, NULL);
  g_object_set (priv->camerabin, "mode", CAPTURE_MODE_IMAGE, NULL);
  g_signal_emit_by_name (priv->camerabin, "start-capture");
  return TRUE;
}
