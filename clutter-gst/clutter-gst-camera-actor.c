/*
 * Clutter-GStreamer.
 *
 * GStreamer integration library for Clutter.
 *
 * clutter-gst-camera-actor.c - ClutterActor using GStreamer to display/manipulate a
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

/**
 * SECTION:clutter-gst-camera-actor
 * @short_description: Actor for playback of camera streams.
 *
 * #ClutterGstCameraActor is a #ClutterActor that plays camera streams.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <glib.h>
#include <gio/gio.h>
#include <gst/base/gstbasesink.h>
#include <gst/video/video.h>
#ifdef HAVE_GUDEV
#include <gudev/gudev.h>
#endif

#include "clutter-gst-camera-actor.h"
#include "clutter-gst-debug.h"
#include "clutter-gst-enum-types.h"
#include "clutter-gst-marshal.h"
#include "clutter-gst-private.h"

static const gchar *supported_media_types[] = {
  "video/x-raw",
  NULL
};

struct _ClutterGstCameraActorPrivate
{
  GPtrArray *camera_devices;
  ClutterGstCameraDevice *camera_device;

  GstBus *bus;
  GstElement *camerabin;
  GstElement *camera_source;

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
  READY_FOR_CAPTURE,
  PHOTO_SAVED,
  PHOTO_TAKEN,
  VIDEO_SAVED,
  LAST_SIGNAL
};

static int camera_actor_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (ClutterGstCameraActor,
               clutter_gst_camera_actor,
               CLUTTER_GST_TYPE_ACTOR);

/*
 * ClutterGstActor implementation
 */

static gboolean
clutter_gst_camera_actor_is_idle (ClutterGstActor *actor)
{
  ClutterGstCameraActorPrivate *priv = CLUTTER_GST_CAMERA_ACTOR (actor)->priv;

  return priv->is_idle;
}

/*
 * GObject implementation
 */

static void
clutter_gst_camera_actor_dispose (GObject *object)
{
  ClutterGstCameraActor *camera_actor = CLUTTER_GST_CAMERA_ACTOR (object);
  ClutterGstCameraActorPrivate *priv = camera_actor->priv;

  g_free (priv->photo_filename);
  priv->photo_filename = NULL;

  if (priv->camera_devices)
    {
      g_ptr_array_unref (priv->camera_devices);
      priv->camera_devices = NULL;
    }

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

  G_OBJECT_CLASS (clutter_gst_camera_actor_parent_class)->dispose (object);
}

static void
clutter_gst_camera_actor_class_init (ClutterGstCameraActorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterGstActorClass *gst_actor_class = CLUTTER_GST_ACTOR_CLASS (klass);

  g_type_class_add_private (klass, sizeof (ClutterGstCameraActorPrivate));

  object_class->dispose = clutter_gst_camera_actor_dispose;

  gst_actor_class->is_idle = clutter_gst_camera_actor_is_idle;

  /**
   * ClutterGstCameraActor::ready-for-capture:
   * @camera_actor: the actor which received the signal
   * @ready: whether the @camera_actor is ready for a new capture
   *
   * The ::ready-for-capture signal is emitted whenever the value of
   * clutter_gst_camera_actor_is_ready_for_capture changes.
   */
  camera_actor_signals[READY_FOR_CAPTURE] =
    g_signal_new ("ready-for-capture",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterGstCameraActorClass, ready_for_capture),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOOLEAN,
                  G_TYPE_NONE, 1,
                  G_TYPE_BOOLEAN);
  /**
   * ClutterGstCameraActor::photo-saved:
   * @camera_actor: the actor which received the signal
   *
   * The ::photo-saved signal is emitted when a photo was saved to disk.
   */
  camera_actor_signals[PHOTO_SAVED] =
      g_signal_new ("photo-saved",
                    G_TYPE_FROM_CLASS (object_class),
                    G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                    G_STRUCT_OFFSET (ClutterGstCameraActorClass, photo_saved),
                    NULL, NULL,
                    g_cclosure_marshal_VOID__VOID,
                    G_TYPE_NONE, 0);
  /**
   * ClutterGstCameraActor::photo-taken:
   * @camera_actor: the actor which received the signal
   * @pixbuf: the photo taken as a #GdkPixbuf
   *
   * The ::photo-taken signal is emitted when a photo was taken.
   */
  camera_actor_signals[PHOTO_TAKEN] =
      g_signal_new ("photo-taken",
                    G_TYPE_FROM_CLASS (object_class),
                    G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                    G_STRUCT_OFFSET (ClutterGstCameraActorClass, photo_taken),
                    NULL, NULL,
                    g_cclosure_marshal_VOID__OBJECT,
                    G_TYPE_NONE, 1, GDK_TYPE_PIXBUF);
  /**
   * ClutterGstCameraActor::video-saved:
   * @camera_actor: the actor which received the signal
   *
   * The ::video-saved signal is emitted when a video was saved to disk.
   */
  camera_actor_signals[VIDEO_SAVED] =
      g_signal_new ("video-saved",
                    G_TYPE_FROM_CLASS (object_class),
                    G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                    G_STRUCT_OFFSET (ClutterGstCameraActorClass, video_saved),
                    NULL, NULL,
                    g_cclosure_marshal_VOID__VOID,
                    G_TYPE_NONE, 0);
}

static void
notify_ready_for_capture (GObject               *object,
                          GParamSpec            *pspec,
                          ClutterGstCameraActor *camera_actor)
{
  ClutterGstCameraActorPrivate *priv = camera_actor->priv;
  gboolean ready_for_capture;

  g_object_get (priv->camera_source, "ready-for-capture",
                &ready_for_capture, NULL);
  g_signal_emit (camera_actor, camera_actor_signals[READY_FOR_CAPTURE],
                 0, ready_for_capture);
}

static void
parse_photo_data (ClutterGstCameraActor *camera_actor,
                  GstSample             *sample)
{
  ClutterGstCameraActorPrivate *priv = camera_actor->priv;
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
  g_signal_emit (camera_actor, camera_actor_signals[PHOTO_TAKEN], 0, pixbuf);
  g_object_unref (pixbuf);
}

static void
bus_message_cb (GstBus                *bus,
                GstMessage            *message,
                ClutterGstCameraActor *camera_actor)
{
  ClutterGstCameraActorPrivate *priv = camera_actor->priv;

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
          g_object_notify (G_OBJECT (camera_actor), "idle");
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
              g_object_notify (G_OBJECT (camera_actor), "idle");
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
                          parse_photo_data (camera_actor, sample);
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
                    g_signal_emit (camera_actor, camera_actor_signals[PHOTO_SAVED], 0);
                }
              else if (strcmp (gst_structure_get_name (structure), "video-done") == 0)
                {
                  g_signal_emit (camera_actor, camera_actor_signals[VIDEO_SAVED], 0);
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
set_video_profile (ClutterGstCameraActor *camera_actor)
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

  clutter_gst_camera_actor_set_video_profile (camera_actor,
                                              (GstEncodingProfile *) prof);

  gst_encoding_profile_unref (prof);
}

static GstElement *
setup_video_filter_bin (ClutterGstCameraActor *camera_actor)
{
  ClutterGstCameraActorPrivate *priv = camera_actor->priv;
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
create_caps_for_formats (gint          width,
                         gint          height)
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
                                   ClutterGstCameraActor  *camera_actor)
{
  ClutterGstCameraActorPrivate *priv = camera_actor->priv;
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
set_device_resolutions (ClutterGstCameraActor  *camera_actor,
                        ClutterGstCameraDevice *camera_device)

{
  gint width;
  gint height;

  clutter_gst_camera_device_get_capture_resolution (camera_device, &width, &height);
  device_capture_resolution_changed (camera_device, width, height, camera_actor);
}

static void
add_device (ClutterGstCameraActor *camera_actor,
            GstElementFactory     *element_factory,
            const gchar           *device_node,
            const gchar           *device_name)
{
  ClutterGstCameraActorPrivate *priv = camera_actor->priv;
  ClutterGstCameraDevice *camera_device;

  if (!priv->camera_devices)
    priv->camera_devices =
        g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

  camera_device = g_object_new (CLUTTER_GST_TYPE_CAMERA_DEVICE,
                                "element-factory", element_factory,
                                "node", device_node,
                                "name", device_name,
                                NULL);
  g_signal_connect (camera_device, "capture-resolution-changed",
                    G_CALLBACK (device_capture_resolution_changed),
                    camera_actor);
  g_ptr_array_add (priv->camera_devices, camera_device);
}

static gboolean
probe_camera_devices (ClutterGstCameraActor *camera_actor)
{
  ClutterGstCameraActorPrivate *priv = camera_actor->priv;
  GstElement *videosrc;
  GstElementFactory *element_factory;
  GParamSpec *pspec;
  gchar *device_node;
  gchar *device_name;
#ifdef HAVE_GUDEV
  GUdevClient *udev_client;
  GList *udevices, *l;
  GUdevDevice *udevice;
#endif

  videosrc = gst_element_factory_make ("v4l2src", "v4l2src");
  if (!videosrc)
    {
      g_warning ("Unable to get available camera devices, "
                 "v4l2src element missing");
      return FALSE;
    }

  pspec = g_object_class_find_property (
    G_OBJECT_GET_CLASS (G_OBJECT (videosrc)), "device");
  if (!G_IS_PARAM_SPEC_STRING (pspec))
    {
      g_warning ("Unable to get available camera devices, "
                 "v4l2src has no 'device' property");
      goto out;
    }

  element_factory = gst_element_get_factory (videosrc);

#ifdef HAVE_GUDEV
  udev_client = g_udev_client_new (NULL);
  udevices = g_udev_client_query_by_subsystem (udev_client, "video4linux");
  for (l = udevices; l != NULL; l = l->next)
    {
      gint v4l_version;

      udevice = (GUdevDevice *) l->data;
      v4l_version = g_udev_device_get_property_as_int (udevice, "ID_V4L_VERSION");
      if (v4l_version == 2)
        {
          const char *caps;

          caps = g_udev_device_get_property (udevice, "ID_V4L_CAPABILITIES");
          if (caps == NULL || strstr (caps, ":capture:") == NULL)
            continue;

          device_node = (gchar *) g_udev_device_get_device_file (udevice);
          device_name = (gchar *) g_udev_device_get_property (udevice, "ID_V4L_PRODUCT");

          add_device (camera_actor, element_factory, device_node, device_name);
        }

      g_object_unref (udevice);
    }
  g_list_free (udevices);
  g_object_unref (udev_client);
#else
  /* GStreamer 1.0 does not support property probe, adding default detected
   * device as only known device */
  g_object_get (videosrc, "device", &device_node, NULL);
  g_object_get (videosrc, "device-name", &device_name, NULL);
  add_device (camera_actor, element_factory, device_node, device_name);

  g_free (device_node);
  g_free (device_name);
#endif

out:
  gst_object_unref (videosrc);
  return (priv->camera_devices != NULL);
}

static gboolean
setup_camera_source (ClutterGstCameraActor *camera_actor)
{
  ClutterGstCameraActorPrivate *priv = camera_actor->priv;
  GstElement *camera_source;
  GstElement *old_camera_source = NULL;

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
                    camera_actor);

  if (priv->video_filter_bin)
    {
      g_object_set (G_OBJECT (camera_source),
                    "video-source-filter", priv->video_filter_bin,
                    NULL);
    }

  return TRUE;
}

static gboolean
setup_pipeline (ClutterGstCameraActor *camera_actor)
{
  ClutterGstCameraActorPrivate *priv = camera_actor->priv;
  GstElement *camera_sink;

  if (!probe_camera_devices (camera_actor))
    {
      g_critical ("Unable to find any suitable capture device");
      return FALSE;
    }

  priv->camerabin = gst_element_factory_make ("camerabin", "camerabin");
  if (G_UNLIKELY (!priv->camerabin))
    {
      g_critical ("Unable to create camerabin element");
      return FALSE;
    }

  priv->video_filter_bin = setup_video_filter_bin (camera_actor);
  if (!priv->video_filter_bin)
    g_warning ("Unable to setup video filter, some features will be disabled");

  if (G_UNLIKELY (!setup_camera_source (camera_actor)))
    {
      g_critical ("Unable to create camera source element");
      gst_object_unref (priv->camerabin);
      priv->camerabin = 0;
      return FALSE;
    }

  if (!clutter_gst_camera_actor_set_camera_device (camera_actor,
           g_ptr_array_index (priv->camera_devices, 0)))
    {
      g_critical ("Unable to select capture device");
      gst_object_unref (priv->camerabin);
      priv->camerabin = 0;
      return FALSE;
    }

  camera_sink = gst_element_factory_make ("cluttersink", NULL);
  g_object_set (camera_sink,
                "actor", CLUTTER_GST_ACTOR (camera_actor),
                NULL);
  g_object_set (priv->camerabin,
                "viewfinder-sink", camera_sink,
                NULL);

  set_video_profile (camera_actor);

  priv->bus = gst_element_get_bus (priv->camerabin);
  gst_bus_add_signal_watch (priv->bus);

  g_signal_connect (G_OBJECT (priv->bus), "message",
                    G_CALLBACK (bus_message_cb), camera_actor);

  return TRUE;
}

static void
clutter_gst_camera_actor_init (ClutterGstCameraActor *camera_actor)
{
  ClutterGstCameraActorPrivate *priv;

  camera_actor->priv = priv =
    G_TYPE_INSTANCE_GET_PRIVATE (camera_actor,
                                 CLUTTER_GST_TYPE_CAMERA_ACTOR,
                                 ClutterGstCameraActorPrivate);

  if (!setup_pipeline (camera_actor))
    {
      g_warning ("Failed to initiate suitable elements for pipeline.");
      return;
    }

  priv->is_idle = TRUE;
}

/*
 * Public symbols
 */

/**
 * clutter_gst_camera_actor_new:
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
ClutterActor*
clutter_gst_camera_actor_new (void)
{
  return g_object_new (CLUTTER_GST_TYPE_CAMERA_ACTOR,
                       NULL);
}

/**
 * clutter_gst_camera_actor_get_pipeline:
 * @camera_actor: a #ClutterGstCameraActor
 *
 * Retrieve the #GstPipeline used by the @camera_actor, for direct use with
 * GStreamer API.
 *
 * Return value: (transfer none): the pipeline element used by the camera actor
 */
GstElement *
clutter_gst_camera_actor_get_pipeline (ClutterGstCameraActor *camera_actor)
{
  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA_ACTOR (camera_actor), NULL);

  return camera_actor->priv->camerabin;
}

/**
 * clutter_gst_camera_actor_get_camerabin:
 * @camera_actor: a #ClutterGstCameraActor
 *
 * Retrieve the camerabin element used by the @camera_actor, for direct use with
 * GStreamer API.
 *
 * Return value: (transfer none): the pipeline element used by the camera actor
 */
GstElement *
clutter_gst_camera_actor_get_camerabin (ClutterGstCameraActor *camera_actor)
{
  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA_ACTOR (camera_actor), NULL);

  return camera_actor->priv->camerabin;
}

/**
 * clutter_gst_camera_actor_get_camera_devices:
 * @camera_actor: a #ClutterGstCameraActor
 *
 * Retrieve an array of supported camera devices.
 *
 * Return value: (transfer none) (element-type ClutterGst.CameraDevice): An array of #ClutterGstCameraDevice representing
 *                                the supported camera devices
 */
const GPtrArray *
clutter_gst_camera_actor_get_camera_devices (ClutterGstCameraActor *camera_actor)
{
  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA_ACTOR (camera_actor), NULL);

  return camera_actor->priv->camera_devices;
}

/**
 * clutter_gst_camera_actor_get_camera_device:
 * @camera_actor: a #ClutterGstCameraActor
 *
 * Retrieve the current selected camera device.
 *
 * Return value: (transfer none): The currently selected camera device
 */
ClutterGstCameraDevice *
clutter_gst_camera_actor_get_camera_device (ClutterGstCameraActor *camera_actor)
{
  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA_ACTOR (camera_actor), NULL);

  return camera_actor->priv->camera_device;
}

/**
 * clutter_gst_camera_actor_set_camera_device:
 * @camera_actor: a #ClutterGstCameraActor
 * @camera_device: a #ClutterGstCameraDevice
 *
 * Set the new active camera device.
 *
 * Return value: %TRUE on success, %FALSE otherwise
 */
gboolean
clutter_gst_camera_actor_set_camera_device (ClutterGstCameraActor  *camera_actor,
                                            ClutterGstCameraDevice *camera_device)
{
  ClutterGstCameraActorPrivate *priv;
  GstElementFactory *element_factory;
  GstElement *src;
  gchar *node;
  gboolean was_playing = FALSE;

  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA_ACTOR (camera_actor), FALSE);
  g_return_val_if_fail (camera_device != NULL, FALSE);

  priv = camera_actor->priv;

  if (!priv->camerabin)
    return FALSE;

  if (priv->is_recording)
    clutter_gst_camera_actor_stop_video_recording (camera_actor);

  if (clutter_gst_camera_actor_is_playing (camera_actor))
    {
      gst_element_set_state (priv->camerabin, GST_STATE_NULL);
      was_playing = TRUE;
    }

  g_object_get (camera_device,
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

  priv->camera_device = camera_device;

  g_object_set (G_OBJECT (src), "device", node, NULL);
  g_free (node);
  g_object_set (G_OBJECT (priv->camera_source), "video-source", src, NULL);

  set_device_resolutions (camera_actor, camera_device);

  if (was_playing)
    gst_element_set_state (priv->camerabin, GST_STATE_PLAYING);

  return TRUE;
}

/**
 * clutter_gst_camera_actor_supports_gamma_correction:
 * @camera_actor: a #ClutterGstCameraActor
 *
 * Check whether the @camera_actor supports gamma correction.
 *
 * Return value: %TRUE if @camera_actor supports gamma correction, %FALSE otherwise
 */
gboolean
clutter_gst_camera_actor_supports_gamma_correction (ClutterGstCameraActor *camera_actor)
{
  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA_ACTOR (camera_actor), FALSE);

  return (camera_actor->priv->gamma != NULL);
}

/**
 * clutter_gst_camera_actor_get_gamma_range:
 * @camera_actor: a #ClutterGstCameraActor
 * @min_value: Pointer to store the minimum gamma value, or %NULL
 * @max_value: Pointer to store the maximum gamma value, or %NULL
 * @default_value: Pointer to store the default gamma value, or %NULL
 *
 * Retrieve the minimum, maximum and default gamma values.
 *
 * This method will return FALSE if gamma correction is not
 * supported on @camera_actor.
 * See clutter_gst_camera_actor_supports_gamma_correction().
 *
 * Return value: %TRUE if successful, %FALSE otherwise
 */
gboolean
clutter_gst_camera_actor_get_gamma_range (ClutterGstCameraActor *camera_actor,
                                          gdouble               *min_value,
                                          gdouble               *max_value,
                                          gdouble               *default_value)
{
  ClutterGstCameraActorPrivate *priv;
  GParamSpec *pspec;

  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA_ACTOR (camera_actor), FALSE);

  priv = camera_actor->priv;

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
 * clutter_gst_camera_actor_get_gamma:
 * @camera_actor: a #ClutterGstCameraActor
 * @cur_value: Pointer to store the current gamma value
 *
 * Retrieve the current gamma value.
 *
 * This method will return FALSE if gamma correction is not
 * supported on @camera_actor.
 * See clutter_gst_camera_actor_supports_gamma_correction().
 *
 * Return value: %TRUE if successful, %FALSE otherwise
 */
gboolean
clutter_gst_camera_actor_get_gamma (ClutterGstCameraActor *camera_actor,
                                    gdouble               *cur_value)
{
  ClutterGstCameraActorPrivate *priv;

  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA_ACTOR (camera_actor), FALSE);
  g_return_val_if_fail (cur_value != NULL, FALSE);

  priv = camera_actor->priv;

  if (!priv->gamma)
    return FALSE;

  g_object_get (G_OBJECT (priv->gamma), "gamma", cur_value, NULL);
  return TRUE;
}

/**
 * clutter_gst_camera_actor_set_gamma:
 * @camera_actor: a #ClutterGstCameraActor
 * @value: The value to set
 *
 * Set the gamma value.
 * Allowed values can be retrieved with
 * clutter_gst_camera_actor_get_gamma_range().
 *
 * This method will return FALSE if gamma correction is not
 * supported on @camera_actor.
 * See clutter_gst_camera_actor_supports_gamma_correction().
 *
 * Return value: %TRUE if successful, %FALSE otherwise
 */
gboolean
clutter_gst_camera_actor_set_gamma (ClutterGstCameraActor *camera_actor,
                                    gdouble                value)
{
  ClutterGstCameraActorPrivate *priv;

  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA_ACTOR (camera_actor), FALSE);

  priv = camera_actor->priv;

  if (!priv->gamma)
    return FALSE;

  g_object_set (G_OBJECT (priv->gamma), "gamma", value, NULL);
  return TRUE;
}

/**
 * clutter_gst_camera_actor_supports_color_balance:
 * @camera_actor: a #ClutterGstCameraActor
 *
 * Check whether the @camera_actor supports color balance.
 *
 * Return value: %TRUE if @camera_actor supports color balance, %FALSE otherwise
 */
gboolean
clutter_gst_camera_actor_supports_color_balance (ClutterGstCameraActor *camera_actor)
{
  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA_ACTOR (camera_actor), FALSE);

  return (camera_actor->priv->color_balance != NULL);
}

/**
 * clutter_gst_camera_actor_get_color_balance_property_range:
 * @camera_actor: a #ClutterGstCameraActor
 * @property: Property name
 * @min_value: Pointer to store the minimum value of @property, or %NULL
 * @max_value: Pointer to store the maximum value of @property, or %NULL
 * @default_value: Pointer to store the default value of @property, or %NULL
 *
 * Retrieve the minimum, maximum and default values for the color balance property @property,
 *
 * This method will return FALSE if @property does not exist or color balance is not
 * supported on @camera_actor.
 * See clutter_gst_camera_actor_supports_color_balance().
 *
 * Return value: %TRUE if successful, %FALSE otherwise
 */
gboolean
clutter_gst_camera_actor_get_color_balance_property_range (ClutterGstCameraActor *camera_actor,
                                                           const gchar           *property,
                                                           gdouble               *min_value,
                                                           gdouble               *max_value,
                                                           gdouble               *default_value)
{
  ClutterGstCameraActorPrivate *priv;
  GParamSpec *pspec;

  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA_ACTOR (camera_actor), FALSE);

  priv = camera_actor->priv;

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
 * clutter_gst_camera_actor_get_color_balance_property:
 * @camera_actor: a #ClutterGstCameraActor
 * @property: Property name
 * @cur_value: Pointer to store the current value of @property
 *
 * Retrieve the current value for the color balance property @property,
 *
 * This method will return FALSE if @property does not exist or color balance is not
 * supported on @camera_actor.
 * See clutter_gst_camera_actor_supports_color_balance().
 *
 * Return value: %TRUE if successful, %FALSE otherwise
 */
gboolean
clutter_gst_camera_actor_get_color_balance_property (ClutterGstCameraActor *camera_actor,
                                                     const gchar           *property,
                                                     gdouble               *cur_value)
{
  ClutterGstCameraActorPrivate *priv;
  GParamSpec *pspec;

  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA_ACTOR (camera_actor), FALSE);
  g_return_val_if_fail (cur_value != NULL, FALSE);

  priv = camera_actor->priv;

  if (!priv->color_balance)
    return FALSE;

  pspec = g_object_class_find_property (
    G_OBJECT_GET_CLASS (G_OBJECT (priv->color_balance)), property);
  g_return_val_if_fail (G_IS_PARAM_SPEC_DOUBLE (pspec), FALSE);

  g_object_get (G_OBJECT (priv->color_balance), property, cur_value, NULL);
  return TRUE;
}

/**
 * clutter_gst_camera_actor_set_color_balance_property:
 * @camera_actor: a #ClutterGstCameraActor
 * @property: Property name
 * @value: The value to set
 *
 * Set the value for the color balance property @property to @value.
 * Allowed values can be retrieved with
 * clutter_gst_camera_actor_get_color_balance_property_range().
 *
 * This method will return FALSE if @property does not exist or color balance is not
 * supported on @camera_actor.
 * See clutter_gst_camera_actor_supports_color_balance().
 *
 * Return value: %TRUE if successful, %FALSE otherwise
 */
gboolean
clutter_gst_camera_actor_set_color_balance_property (ClutterGstCameraActor *camera_actor,
                                                     const gchar           *property,
                                                     gdouble                value)
{
  ClutterGstCameraActorPrivate *priv;
  GParamSpec *pspec;

  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA_ACTOR (camera_actor), FALSE);

  priv = camera_actor->priv;

  if (!priv->color_balance)
    return FALSE;

  pspec = g_object_class_find_property (
    G_OBJECT_GET_CLASS (G_OBJECT (priv->color_balance)), property);
  g_return_val_if_fail (G_IS_PARAM_SPEC_DOUBLE (pspec), FALSE);

  g_object_set (G_OBJECT (priv->color_balance), property, value, NULL);
  return TRUE;
}

gboolean
clutter_gst_camera_actor_get_brightness_range (ClutterGstCameraActor *camera_actor,
                                               gdouble               *min_value,
                                               gdouble               *max_value,
                                               gdouble               *default_value)
{
  return clutter_gst_camera_actor_get_color_balance_property_range (camera_actor,
             "brightness", min_value, max_value, default_value);
}

gboolean
clutter_gst_camera_actor_get_brightness (ClutterGstCameraActor *camera_actor,
                                         gdouble               *cur_value)
{
  return clutter_gst_camera_actor_get_color_balance_property (camera_actor,
             "brightness", cur_value);
}

gboolean
clutter_gst_camera_actor_set_brightness (ClutterGstCameraActor *camera_actor,
                                         gdouble                value)
{
  return clutter_gst_camera_actor_set_color_balance_property (camera_actor,
             "brightness", value);
}

gboolean
clutter_gst_camera_actor_get_contrast_range (ClutterGstCameraActor *camera_actor,
                                             gdouble               *min_value,
                                             gdouble               *max_value,
                                             gdouble               *default_value)
{
  return clutter_gst_camera_actor_get_color_balance_property_range (camera_actor,
             "contrast", min_value, max_value, default_value);
}

gboolean
clutter_gst_camera_actor_get_contrast (ClutterGstCameraActor *camera_actor,
                                       gdouble               *cur_value)
{
  return clutter_gst_camera_actor_get_color_balance_property (camera_actor,
             "contrast", cur_value);
}

gboolean
clutter_gst_camera_actor_set_contrast (ClutterGstCameraActor *camera_actor,
                                       gdouble                value)
{
  return clutter_gst_camera_actor_set_color_balance_property (camera_actor,
             "contrast", value);
}

gboolean
clutter_gst_camera_actor_get_saturation_range (ClutterGstCameraActor *camera_actor,
                                               gdouble               *min_value,
                                               gdouble               *max_value,
                                               gdouble               *default_value)
{
  return clutter_gst_camera_actor_get_color_balance_property_range (camera_actor,
             "saturation", min_value, max_value, default_value);
}

gboolean
clutter_gst_camera_actor_get_saturation (ClutterGstCameraActor *camera_actor,
                                         gdouble               *cur_value)
{
  return clutter_gst_camera_actor_get_color_balance_property (camera_actor,
             "saturation", cur_value);
}

gboolean
clutter_gst_camera_actor_set_saturation (ClutterGstCameraActor *camera_actor,
                                         gdouble                value)
{
  return clutter_gst_camera_actor_set_color_balance_property (camera_actor,
             "saturation", value);
}

gboolean
clutter_gst_camera_actor_get_hue_range (ClutterGstCameraActor *camera_actor,
                                        gdouble               *min_value,
                                        gdouble               *max_value,
                                        gdouble               *default_value)
{
  return clutter_gst_camera_actor_get_color_balance_property_range (camera_actor,
             "hue", min_value, max_value, default_value);
}

gboolean
clutter_gst_camera_actor_get_hue (ClutterGstCameraActor *camera_actor,
                                  gdouble               *cur_value)
{
  return clutter_gst_camera_actor_get_color_balance_property (camera_actor,
             "hue", cur_value);
}

gboolean
clutter_gst_camera_actor_set_hue (ClutterGstCameraActor *camera_actor,
                                  gdouble                value)
{
  return clutter_gst_camera_actor_set_color_balance_property (camera_actor,
             "hue", value);
}

/**
 * clutter_gst_camera_actor_get_filter:
 * @camera_actor: a #ClutterGstCameraActor
 *
 * Retrieve the current filter being used.
 *
 * Return value: (transfer none): The current filter or %NULL if none is set
 */
GstElement *
clutter_gst_camera_actor_get_filter (ClutterGstCameraActor  *camera_actor)
{
  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA_ACTOR (camera_actor), FALSE);

  return camera_actor->priv->custom_filter;
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
 * clutter_gst_camera_actor_set_filter:
 * @camera_actor: a #ClutterGstCameraActor
 * @filter: a #GstElement for the filter
 *
 * Set the filter element to be used.
 * Filters can be used for effects, image processing, etc.
 *
 * Return value: %TRUE on success, %FALSE otherwise
 */
gboolean
clutter_gst_camera_actor_set_filter (ClutterGstCameraActor  *camera_actor,
                                     GstElement             *filter)
{
  ClutterGstCameraActorPrivate *priv;
  gboolean ret = FALSE;

  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA_ACTOR (camera_actor), FALSE);

  priv = camera_actor->priv;

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

      if (clutter_gst_camera_actor_is_playing (camera_actor))
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
 * clutter_gst_camera_actor_remove_filter:
 * @camera_actor: a #ClutterGstCameraActor
 *
 * Remove the current filter, if any.
 *
 * Return value: %TRUE on success, %FALSE otherwise
 */
gboolean
clutter_gst_camera_actor_remove_filter (ClutterGstCameraActor *camera_actor)
{
  return clutter_gst_camera_actor_set_filter (camera_actor, NULL);
}

/**
 * clutter_gst_camera_actor_is_playing:
 * @camera_actor: a #ClutterGstCameraActor
 *
 * Retrieve whether the @camera_actor is playing.
 *
 * Return value: %TRUE if playing, %FALSE otherwise
 */
gboolean
clutter_gst_camera_actor_is_playing (ClutterGstCameraActor *camera_actor)
{
  ClutterGstCameraActorPrivate *priv;
  GstState state, pending;
  gboolean playing;

  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA_ACTOR (camera_actor), FALSE);

  priv = camera_actor->priv;
  if (!priv->camerabin)
    return FALSE;

  gst_element_get_state (priv->camerabin, &state, &pending, 0);

  if (pending)
    playing = (pending == GST_STATE_PLAYING);
  else
    playing = (state == GST_STATE_PLAYING);

  return playing;
}

/**
 * clutter_gst_camera_actor_set_playing:
 * @camera_actor: a #ClutterGstCameraActor
 * @playing: %TRUE to start playback
 *
 * Starts or stops playback.
 */
void
clutter_gst_camera_actor_set_playing (ClutterGstCameraActor *camera_actor,
                                      gboolean               playing)
{
  ClutterGstCameraActorPrivate *priv;
  GstState target_state;

  g_return_if_fail (CLUTTER_GST_IS_CAMERA_ACTOR (camera_actor));

  priv = camera_actor->priv;

  if (!priv->camerabin)
    return;

  target_state = playing ? GST_STATE_PLAYING : GST_STATE_NULL;

  gst_element_set_state (priv->camerabin, target_state);
}

/**
 * clutter_gst_camera_actor_is_ready_for_capture:
 * @camera_actor: a #ClutterGstCameraActor
 *
 * Check whether the @camera_actor is ready for video/photo capture.
 *
 * Return value: %TRUE if @camera_actor is ready for capture, %FALSE otherwise
 */
gboolean
clutter_gst_camera_actor_is_ready_for_capture (ClutterGstCameraActor *camera_actor)
{
  ClutterGstCameraActorPrivate *priv;
  gboolean ready_for_capture;

  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA_ACTOR (camera_actor), FALSE);

  priv = camera_actor->priv;

  g_object_get (priv->camera_source, "ready-for-capture", &ready_for_capture, NULL);

  return ready_for_capture;
}

/**
 * clutter_gst_camera_actor_is_recording_video:
 * @camera_actor: a #ClutterGstCameraActor
 *
 * Check whether the @camera_actor is recording video.
 *
 * Return value: %TRUE if @camera_actor is recording video, %FALSE otherwise
 */
gboolean
clutter_gst_camera_actor_is_recording_video (ClutterGstCameraActor *camera_actor)
{
  ClutterGstCameraActorPrivate *priv;

  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA_ACTOR (camera_actor), FALSE);

  priv = camera_actor->priv;

  return priv->is_recording;
}

/**
 * clutter_gst_camera_actor_set_video_profile:
 * @camera_actor: a #ClutterGstCameraActor
 * @profile: A #GstEncodingProfile to be used for video recording.
 *
 * Set the encoding profile to be used for video recording.
 * The default profile saves videos as Ogg/Theora videos.
 */
void
clutter_gst_camera_actor_set_video_profile (ClutterGstCameraActor *camera_actor,
                                            GstEncodingProfile    *profile)
{
  ClutterGstCameraActorPrivate *priv;

  g_return_if_fail (CLUTTER_GST_IS_CAMERA_ACTOR (camera_actor));

  priv = camera_actor->priv;

  if (!priv->camerabin)
    return;

  g_object_set (priv->camerabin, "video-profile", profile, NULL);
}

/**
 * clutter_gst_camera_actor_start_video_recording:
 * @camera_actor: a #ClutterGstCameraActor
 * @filename: (type filename): the name of the video file to where the
 * recording will be saved
 *
 * Start a video recording with the @camera_actor and save it to @filename.
 * This method requires that @camera_actor is playing and ready for capture.
 *
 * The ::video-saved signal will be emitted when the video is saved.
 *
 * Return value: %TRUE if the video recording was successfully started, %FALSE otherwise
 */
gboolean
clutter_gst_camera_actor_start_video_recording (ClutterGstCameraActor *camera_actor,
                                                const gchar           *filename)
{
  ClutterGstCameraActorPrivate *priv;

  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA_ACTOR (camera_actor), FALSE);

  priv = camera_actor->priv;

  if (!priv->camerabin)
    return FALSE;

  if (priv->is_recording)
    return TRUE;

  if (!clutter_gst_camera_actor_is_playing (camera_actor))
    return FALSE;

  if (!clutter_gst_camera_actor_is_ready_for_capture (camera_actor))
    return FALSE;

  g_object_set (priv->camerabin, "mode", CAPTURE_MODE_VIDEO, NULL);
  g_object_set (priv->camerabin, "location", filename, NULL);
  g_signal_emit_by_name (priv->camerabin, "start-capture", 0);
  priv->is_recording = TRUE;
  return TRUE;
}

/**
 * clutter_gst_camera_stop_video_recording:
 * @camera_actor: a #ClutterGstCameraActor
 *
 * Stop recording video on the @camera_actor.
 */
void
clutter_gst_camera_actor_stop_video_recording (ClutterGstCameraActor *camera_actor)
{
  ClutterGstCameraActorPrivate *priv;
  GstState state;

  g_return_if_fail (CLUTTER_GST_IS_CAMERA_ACTOR (camera_actor));

  priv = camera_actor->priv;

  if (!priv->camerabin)
    return;

  if (!priv->is_recording)
    return;

  if (!clutter_gst_camera_actor_is_playing (camera_actor))
    return;

  gst_element_get_state (priv->camerabin, &state, NULL, 0);

  if (state == GST_STATE_PLAYING)
    g_signal_emit_by_name (priv->camerabin, "stop-capture", 0);
  else if (priv->is_recording)
    {
      g_warning ("Cannot cleanly shutdown recording pipeline, forcing");

      gst_element_set_state (priv->camerabin, GST_STATE_NULL);
      gst_element_set_state (priv->camerabin, GST_STATE_PLAYING);
      priv->is_recording = FALSE;
    }
}

/**
 * clutter_gst_camera_actor_set_photo_profile:
 * @camera_actor: a #ClutterGstCameraActor
 * @profile: A #GstEncodingProfile to be used for photo captures.
 *
 * Set the encoding profile to be used for photo captures.
 * The default profile saves photos as JPEG images.
 */
void
clutter_gst_camera_actor_set_photo_profile (ClutterGstCameraActor *camera_actor,
                                            GstEncodingProfile    *profile)
{
  ClutterGstCameraActorPrivate *priv;

  g_return_if_fail (CLUTTER_GST_IS_CAMERA_ACTOR (camera_actor));

  priv = camera_actor->priv;

  if (!priv->camerabin)
    return;

  g_object_set (priv->camerabin, "image-profile", profile, NULL);
}

/**
 * clutter_gst_camera_actor_take_photo:
 * @camera_actor: a #ClutterGstCameraActor
 * @filename: (type filename): the name of the file to where the
 * photo will be saved
 *
 * Take a photo with the @camera_actor and save it to @filename.
 * This method requires that @camera_actor is playing and ready for capture.
 *
 * The ::photo-saved signal will be emitted when the video is saved.
 *
 * Return value: %TRUE if the photo was successfully captured, %FALSE otherwise
 */
gboolean
clutter_gst_camera_actor_take_photo (ClutterGstCameraActor *camera_actor,
                                     const gchar           *filename)
{
  ClutterGstCameraActorPrivate *priv;

  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA_ACTOR (camera_actor), FALSE);
  g_return_val_if_fail (filename != NULL, FALSE);

  priv = camera_actor->priv;

  if (!priv->camerabin)
    return FALSE;

  if (!clutter_gst_camera_actor_is_playing (camera_actor))
    return FALSE;

  if (!clutter_gst_camera_actor_is_ready_for_capture (camera_actor))
    return FALSE;

  g_free (priv->photo_filename);
  priv->photo_filename = g_strdup (filename);

  /* Take the photo */
  g_object_set (priv->camerabin, "location", filename, NULL);
  g_object_set (priv->camerabin, "mode", CAPTURE_MODE_IMAGE, NULL);
  g_signal_emit_by_name (priv->camerabin, "start-capture", 0);
  return TRUE;
}

/**
 * clutter_gst_camera_take_photo_pixbuf:
 * @camera_actor: a #ClutterGstCameraActor
 *
 * Take a photo with the @camera_actor and emit it in the ::photo-taken signal as a
 * #GdkPixbuf.
 * This method requires that @camera_actor is playing and ready for capture.
 *
 * Return value: %TRUE if the photo was successfully captured, %FALSE otherwise
 */
gboolean
clutter_gst_camera_actor_take_photo_pixbuf (ClutterGstCameraActor *camera_actor)
{
  ClutterGstCameraActorPrivate *priv;
  GstCaps *caps;

  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA_ACTOR (camera_actor), FALSE);

  priv = camera_actor->priv;

  if (!priv->camerabin)
    return FALSE;

  if (!clutter_gst_camera_actor_is_playing (camera_actor))
    return FALSE;

  if (!clutter_gst_camera_actor_is_ready_for_capture (camera_actor))
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
  g_signal_emit_by_name (priv->camerabin, "start-capture", 0);
  return TRUE;
}
