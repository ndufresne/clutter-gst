/*
 * Clutter-GStreamer.
 *
 * GStreamer integration library for Clutter.
 *
 * clutter-gst-camera-device.c - GObject representing a camera device using GStreamer.
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
 * SECTION:clutter-gst-camera-device
 * @short_description: GObject representing a camera device using GStreamer.
 *
 * #ClutterGstCameraDevice is a #GObject representing a camera device using GStreamer.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <glib.h>
#include <gst/gst.h>

#include "clutter-gst-camera-device.h"
#include "clutter-gst-debug.h"
#include "clutter-gst-enum-types.h"
#include "clutter-gst-marshal.h"
#include "clutter-gst-private.h"
#include "clutter-gst-types.h"

struct _ClutterGstCameraDevicePrivate
{
  GstElementFactory *element_factory;
  gchar *node;
  gchar *name;

  GPtrArray *supported_resolutions;
  gint capture_width;
  gint capture_height;
};

enum {
  PROP_0,
  PROP_ELEMENT_FACTORY,
  PROP_NODE,
  PROP_NAME
};

enum
{
  CAPTURE_RESOLUTION_CHANGED,
  LAST_SIGNAL
};

static int camera_device_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (ClutterGstCameraDevice,
               clutter_gst_camera_device,
               G_TYPE_OBJECT);

static void
free_resolution (ClutterGstVideoResolution *resolution)
{
  g_slice_free (ClutterGstVideoResolution, resolution);
}

static void
add_supported_resolution (ClutterGstCameraDevice *device,
                          gint                    width,
                          gint                    height)
{
  ClutterGstCameraDevicePrivate *priv = device->priv;
  ClutterGstVideoResolution *resolution;
  guint i;

  /* check if we already have this resolution set */
  for (i = 0; i < priv->supported_resolutions->len; ++i)
    {
      ClutterGstVideoResolution *other_res;

      other_res = g_ptr_array_index (priv->supported_resolutions, i);
      if (other_res->width == width && other_res->height == height)
        return;
    }

  resolution = g_slice_new0 (ClutterGstVideoResolution);
  resolution->width = width;
  resolution->height = height;
  g_ptr_array_add (priv->supported_resolutions, resolution);
}

static void
parse_device_caps (ClutterGstCameraDevice *device,
                   GstCaps                *caps)
{
  guint size;
  guint i;

  size = gst_caps_get_size (caps);
  for (i = 0; i < size; ++i)
    {
      GstStructure *structure;
      const GValue *width, *height;

      structure = gst_caps_get_structure (caps, i);
      width = gst_structure_get_value (structure, "width");
      height = gst_structure_get_value (structure, "height");

      if (G_VALUE_HOLDS_INT (width) && G_VALUE_HOLDS_INT (height))
        add_supported_resolution (device,
                                  g_value_get_int (width),
                                  g_value_get_int (height));
      else if (GST_VALUE_HOLDS_INT_RANGE (width) &&
               GST_VALUE_HOLDS_INT_RANGE (height))
        {
          gint min_width;
          gint max_width;
          gint min_height;
          gint max_height;

          min_width = gst_value_get_int_range_min (width);
          max_width = gst_value_get_int_range_max (width);
          min_height = gst_value_get_int_range_min (height);
          max_height = gst_value_get_int_range_max (height);

          /* TODO: how to improve this? */
          add_supported_resolution (device, min_width, min_height);
          add_supported_resolution (device, max_width, max_height);
        }
    }
}

static gint
compare_resolution (ClutterGstVideoResolution **a, ClutterGstVideoResolution **b)
{
 return (((*b)->width * (*b)->height) - ((*a)->width * (*a)->height));
}

#if 0
static void
debug_resolutions (ClutterGstCameraDevice *device)
{
  ClutterGstCameraDevicePrivate *priv = device->priv;
  guint i;

  g_print ("Supported resolutions for device %s (node=%s):\n",
           priv->name, priv->node);
  for (i = 0; i < priv->supported_resolutions->len; ++i)
    {
      ClutterGstVideoResolution *res;

      res = g_ptr_array_index (priv->supported_resolutions, i);
      g_print ("\t%dx%d:\n", res->width, res->height);
    }
}
#endif

static void
probe_supported_resolutions (ClutterGstCameraDevice *device,
                             GstElement             *element)
{
  ClutterGstCameraDevicePrivate *priv = device->priv;
  GstPad *pad;
  GstCaps *caps;

  priv->supported_resolutions = g_ptr_array_new_with_free_func (
          (GDestroyNotify) free_resolution);

  if (gst_element_set_state (element, GST_STATE_READY) != GST_STATE_CHANGE_SUCCESS)
    {
      g_warning ("Unable to detect supported resolutions for camera device %s (node=%s)",
                 priv->name, priv->node);
      return;
    }

  pad = gst_element_get_static_pad (element, "src");
  caps = gst_pad_query_caps (pad, NULL);
  parse_device_caps (device, caps);
  gst_caps_unref (caps);
  gst_object_unref (pad);

  gst_element_set_state (element, GST_STATE_NULL);

  g_ptr_array_sort (priv->supported_resolutions,
                    (GCompareFunc) compare_resolution);
}

/*
 * GObject implementation
 */

static void
clutter_gst_camera_device_constructed (GObject *object)
{
  ClutterGstCameraDevice *device = CLUTTER_GST_CAMERA_DEVICE (object);
  ClutterGstCameraDevicePrivate *priv = device->priv;
  GstElement *element;

  if (!priv->element_factory || !priv->node || !priv->name)
    {
      g_critical ("Unable to setup device without element factory, "
                  "node and name set %p %p %p", priv->element_factory,
                  priv->node, priv->name);
      return;
    }

  element = gst_element_factory_create (priv->element_factory, NULL);
  if (!element)
    {
      g_warning ("Unable to create source for camera device %s (node=%s)",
                 priv->name, priv->node);
      return;
    }
  g_object_set (G_OBJECT (element), "device", priv->node, NULL);

  probe_supported_resolutions (device, element);
  if (priv->supported_resolutions->len > 0)
    {
      ClutterGstVideoResolution *resolution;
      gint width;
      gint height;

      resolution = g_ptr_array_index (priv->supported_resolutions, 0);
      width = resolution->width;
      height = resolution->height;
      clutter_gst_camera_device_set_capture_resolution (device, width, height);
    }

  gst_object_unref (element);
}

static void
clutter_gst_camera_device_dispose (GObject *object)
{
  ClutterGstCameraDevice *device = CLUTTER_GST_CAMERA_DEVICE (object);
  ClutterGstCameraDevicePrivate *priv = device->priv;

  if (priv->element_factory)
    {
      gst_object_unref (priv->element_factory);
      priv->element_factory = NULL;
    }

  g_free (priv->node);
  priv->node = NULL;
  g_free (priv->name);
  priv->name = NULL;

  if (priv->supported_resolutions)
    {
      g_ptr_array_unref (priv->supported_resolutions);
      priv->supported_resolutions = NULL;
    }

  G_OBJECT_CLASS (clutter_gst_camera_device_parent_class)->dispose (object);
}

static void
clutter_gst_camera_device_get_property (GObject    *object,
                                        guint       property_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  ClutterGstCameraDevice *actor = CLUTTER_GST_CAMERA_DEVICE (object);
  ClutterGstCameraDevicePrivate *priv = actor->priv;

  switch (property_id)
    {
    case PROP_ELEMENT_FACTORY:
      g_value_set_object (value, priv->element_factory);
      break;
    case PROP_NODE:
      g_value_set_string (value, priv->node);
      break;
    case PROP_NAME:
      g_value_set_string (value, priv->name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
clutter_gst_camera_device_set_property (GObject      *object,
                                        guint         property_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  ClutterGstCameraDevice *actor = CLUTTER_GST_CAMERA_DEVICE (object);
  ClutterGstCameraDevicePrivate *priv = actor->priv;

  switch (property_id)
    {
    case PROP_ELEMENT_FACTORY:
      if (priv->element_factory)
        gst_object_unref (priv->element_factory);
      priv->element_factory = gst_object_ref (GST_OBJECT (g_value_get_object (value)));
      break;
    case PROP_NODE:
      g_free (priv->node);
      priv->node = g_value_dup_string (value);
      break;
    case PROP_NAME:
      g_free (priv->name);
      priv->name = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
clutter_gst_camera_device_class_init (ClutterGstCameraDeviceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *pspec;

  g_type_class_add_private (klass, sizeof (ClutterGstCameraDevicePrivate));

  object_class->constructed = clutter_gst_camera_device_constructed;
  object_class->dispose = clutter_gst_camera_device_dispose;
  object_class->set_property = clutter_gst_camera_device_set_property;
  object_class->get_property = clutter_gst_camera_device_get_property;

  /**
   * ClutterGstCameraDevice:element-factory:
   *
   * The GstElementFactory for this device.
   */
  pspec = g_param_spec_object ("element-factory",
                               "ElementFactory",
                               "The GstElementFactory for this device",
                               GST_TYPE_ELEMENT_FACTORY,
                               CLUTTER_GST_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class, PROP_ELEMENT_FACTORY, pspec);

  /**
   * ClutterGstCameraDevice:node:
   *
   * The device node.
   */
  pspec = g_param_spec_string ("node",
                               "Node",
                               "The device node",
                               NULL,
                               CLUTTER_GST_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class, PROP_NODE, pspec);

  /**
   * ClutterGstCameraDevice:name:
   *
   * The device name.
   */
  pspec = g_param_spec_string ("name",
                               "Name",
                               "The device name",
                               NULL,
                               CLUTTER_GST_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class, PROP_NAME, pspec);

  /**
   * ClutterGstCameraDevice::capture-resolution-changed:
   * @device: the device which received the signal
   * @width: The new width
   * @width: The new height
   *
   * The ::capture-resolution-changed signal is emitted whenever the value of
   * clutter_gst_camera_device_get_capture_resolution changes.
   */
  camera_device_signals[CAPTURE_RESOLUTION_CHANGED] =
    g_signal_new ("capture-resolution-changed",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterGstCameraDeviceClass, capture_resolution_changed),
                  NULL, NULL,
                  _clutter_gst_marshal_VOID__INT_INT,
                  G_TYPE_NONE, 2,
                  G_TYPE_INT,
                  G_TYPE_INT);
}

static void
clutter_gst_camera_device_init (ClutterGstCameraDevice *device)
{
  ClutterGstCameraDevicePrivate *priv;

  device->priv = priv =
    G_TYPE_INSTANCE_GET_PRIVATE (device,
                                 CLUTTER_GST_TYPE_CAMERA_DEVICE,
                                 ClutterGstCameraDevicePrivate);
}

/*
 * Public symbols
 */

/**
 * clutter_gst_camera_device_get_node:
 * @device: a #ClutterGstCameraDevice
 *
 * Retrieve the node (location) of the @device.
 *
 * Return value: (transfer none): the device node.
 */
const gchar *
clutter_gst_camera_device_get_node (ClutterGstCameraDevice *device)
{
  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA_DEVICE (device), NULL);

  return device->priv->node;
}

/**
 * clutter_gst_camera_device_get_name:
 * @device: a #ClutterGstCameraDevice
 *
 * Retrieve the name of the @device.
 *
 * Return value: (transfer none): the device name.
 */
const gchar *
clutter_gst_camera_device_get_name (ClutterGstCameraDevice *device)
{
  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA_DEVICE (device), NULL);

  return device->priv->name;
}

/**
 * clutter_gst_camera_device_get_supported_resolutions:
 * @device: a #ClutterGstCameraDevice
 *
 * Retrieve the supported resolutions of the @device.
 *
 * Return value: (transfer none): an array of #ClutterGstVideoResolution with the
 *                                supported resolutions.
 */
const GPtrArray *
clutter_gst_camera_device_get_supported_resolutions (ClutterGstCameraDevice *device)
{
  g_return_val_if_fail (CLUTTER_GST_IS_CAMERA_DEVICE (device), NULL);

  return device->priv->supported_resolutions;
}

/**
 * clutter_gst_camera_device_get_capture_resolution:
 * @device: a #ClutterGstCameraDevice
 * @width: Pointer to store the current capture resolution width
 * @height: Pointer to store the current capture resolution height
 *
 * Retrieve the current capture resolution being used by @device.
 */
void
clutter_gst_camera_device_get_capture_resolution (ClutterGstCameraDevice *device,
                                                  gint                   *width,
                                                  gint                   *height)
{
  ClutterGstCameraDevicePrivate *priv;

  g_return_if_fail (CLUTTER_GST_IS_CAMERA_DEVICE (device));

  priv = device->priv;

  if (width)
    *width = priv->capture_width;
  if (height)
    *height = priv->capture_height;
}

/**
 * clutter_gst_camera_device_set_capture_resolution:
 * @device: a #ClutterGstCameraDevice
 * @width: The new capture resolution width to use
 * @height: The new capture resolution height to use
 *
 * Set the capture resolution to be used by @device.
 */
void
clutter_gst_camera_device_set_capture_resolution (ClutterGstCameraDevice *device,
                                                gint                    width,
                                                gint                    height)
{
  ClutterGstCameraDevicePrivate *priv;

  g_return_if_fail (CLUTTER_GST_IS_CAMERA_DEVICE (device));

  priv = device->priv;
  priv->capture_width = width;
  priv->capture_height = height;
  g_signal_emit (device,
                 camera_device_signals[CAPTURE_RESOLUTION_CHANGED], 0,
                 width, height);
}
