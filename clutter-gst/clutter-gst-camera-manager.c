/*
 * Clutter-GStreamer.
 *
 * GStreamer integration library for Clutter.
 *
 * clutter-gst-camera-manager.c - a component to list available cameras
 *
 * Authored By Lionel Landwerlin <lionel.g.landwerlin@linux.intel.com>
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

/**
 * SECTION:clutter-gst-camera-manager
 * @short_description: A component to list available cameras.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gst/gst.h>
#ifdef HAVE_GUDEV
#include <gudev/gudev.h>
#endif

#include "clutter-gst-camera-manager.h"
#include "clutter-gst-camera-device.h"

G_DEFINE_TYPE (ClutterGstCameraManager, clutter_gst_camera_manager, G_TYPE_OBJECT)

#define GST_CAMERA_MANAGER_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), CLUTTER_GST_TYPE_CAMERA_MANAGER, ClutterGstCameraManagerPrivate))

struct _ClutterGstCameraManagerPrivate
{
  GPtrArray *camera_devices;
};


static void
clutter_gst_camera_manager_get_property (GObject    *object,
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
clutter_gst_camera_manager_set_property (GObject      *object,
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
clutter_gst_camera_manager_dispose (GObject *object)
{
  ClutterGstCameraManagerPrivate *priv = CLUTTER_GST_CAMERA_MANAGER (object)->priv;

  if (priv->camera_devices)
    {
      g_ptr_array_unref (priv->camera_devices);
      priv->camera_devices = NULL;
    }


  G_OBJECT_CLASS (clutter_gst_camera_manager_parent_class)->dispose (object);
}

static void
clutter_gst_camera_manager_finalize (GObject *object)
{
  G_OBJECT_CLASS (clutter_gst_camera_manager_parent_class)->finalize (object);
}

static void
clutter_gst_camera_manager_class_init (ClutterGstCameraManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (ClutterGstCameraManagerPrivate));

  object_class->get_property = clutter_gst_camera_manager_get_property;
  object_class->set_property = clutter_gst_camera_manager_set_property;
  object_class->dispose = clutter_gst_camera_manager_dispose;
  object_class->finalize = clutter_gst_camera_manager_finalize;
}

static void
add_device (ClutterGstCameraManager *self,
            GstElementFactory       *element_factory,
            const gchar             *device_node,
            const gchar             *device_name)
{
  ClutterGstCameraManagerPrivate *priv = self->priv;
  ClutterGstCameraDevice *device;

  if (!priv->camera_devices)
    priv->camera_devices =
      g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);

  device = g_object_new (CLUTTER_GST_TYPE_CAMERA_DEVICE,
                         "element-factory", element_factory,
                         "node", device_node,
                         "name", device_name,
                         NULL);
  g_ptr_array_add (priv->camera_devices, device);
}

static gboolean
probe_camera_devices (ClutterGstCameraManager *self)
{
  ClutterGstCameraManagerPrivate *priv = self->priv;
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

          add_device (self, element_factory, device_node, device_name);
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
  add_device (self, element_factory, device_node, device_name);

  g_free (device_node);
  g_free (device_name);
#endif

 out:
  gst_object_unref (videosrc);
  return (priv->camera_devices != NULL);
}

static void
clutter_gst_camera_manager_init (ClutterGstCameraManager *self)
{
  self->priv = GST_CAMERA_MANAGER_PRIVATE (self);
}

/**
 * clutter_gst_camera_manager_get_default:
 *
 * Get the camera manager.
 *
 * <note>This function has to be called from Clutter's main
 * thread.</note>
 *
 * Return value: (transfer none): the default camera manager.
 */
ClutterGstCameraManager *
clutter_gst_camera_manager_get_default (void)
{
  static ClutterGstCameraManager *manager = NULL;

  if (G_UNLIKELY (manager == NULL))
    manager = g_object_new (CLUTTER_GST_TYPE_CAMERA_MANAGER, NULL);

  return manager;
}

/**
 * clutter_gst_camera_manager_get_camera_devices:
 * @self: a #ClutterGstCameraManager
 *
 * Retrieve an array of supported camera devices.
 *
 * Return value: (transfer none) (element-type ClutterGst.CameraDevice): An array of #ClutterGstCameraDevice representing
 *                                the supported camera devices
 */
const GPtrArray *
clutter_gst_camera_manager_get_camera_devices (ClutterGstCameraManager *self)
{
  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA_MANAGER (self), NULL);

  return self->priv->camera_devices;
}
