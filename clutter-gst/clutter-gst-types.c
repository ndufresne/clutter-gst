/*
 * Clutter-GStreamer.
 *
 * GStreamer integration library for Clutter.
 *
 * clutter-gst-types.c - Some basic types.
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

#include "clutter-gst-types.h"

ClutterGstFrame *
clutter_gst_frame_new (void)
{
  return g_slice_new0 (ClutterGstFrame);
}

static gpointer
clutter_gst_frame_copy (gpointer data)
{
  if (G_LIKELY (data))
    {
      ClutterGstFrame *frame = g_slice_dup (ClutterGstFrame, data);

      if (frame->pipeline != COGL_INVALID_HANDLE)
        frame->pipeline = cogl_pipeline_copy (frame->pipeline);

      return frame;
    }

  return NULL;
}

static void
clutter_gst_frame_free (gpointer data)
{
  if (G_LIKELY (data))
    {
      ClutterGstFrame *frame = (ClutterGstFrame *) data;

      if (frame->pipeline != COGL_INVALID_HANDLE)
        {
          cogl_object_unref (frame->pipeline);
          frame->pipeline = COGL_INVALID_HANDLE;
        }
      g_slice_free (ClutterGstFrame, frame);
    }
}

G_DEFINE_BOXED_TYPE (ClutterGstFrame,
                     clutter_gst_frame,
                     clutter_gst_frame_copy,
                     clutter_gst_frame_free);


ClutterGstOverlay *
clutter_gst_overlay_new (void)
{
  return g_slice_new0 (ClutterGstOverlay);
}

static gpointer
clutter_gst_overlay_copy (gpointer data)
{
  if (G_LIKELY (data))
    {
      ClutterGstOverlay *overlay = g_slice_dup (ClutterGstOverlay, data);

      if (overlay->pipeline != COGL_INVALID_HANDLE)
        overlay->pipeline = cogl_object_ref (overlay->pipeline);

      return overlay;
    }

  return NULL;
}

static void
clutter_gst_overlay_free (gpointer data)
{
  if (G_LIKELY (data))
    {
      ClutterGstOverlay *overlay = (ClutterGstOverlay *) data;

      if (overlay->pipeline != COGL_INVALID_HANDLE)
        {
          cogl_object_unref (overlay->pipeline);
          overlay->pipeline = COGL_INVALID_HANDLE;
        }
      g_slice_free (ClutterGstOverlay, overlay);
    }
}

G_DEFINE_BOXED_TYPE (ClutterGstOverlay,
                     clutter_gst_overlay,
                     clutter_gst_overlay_copy,
                     clutter_gst_overlay_free);

ClutterGstOverlays *
clutter_gst_overlays_new (void)
{
  ClutterGstOverlays *overlays = g_slice_new0 (ClutterGstOverlays);

  overlays->overlays = g_ptr_array_new_with_free_func (clutter_gst_overlay_free);

  return overlays;
}

static gpointer
clutter_gst_overlays_copy (gpointer data)
{
  if (G_LIKELY (data))
    {
      ClutterGstOverlays *overlays = clutter_gst_overlays_new ();
      GPtrArray *src_overlays = ((ClutterGstOverlays *) data)->overlays;
      guint i;

      for (i = 0; i < src_overlays->len; i++)
        g_ptr_array_add (overlays->overlays,
                         clutter_gst_overlay_copy (g_ptr_array_index (src_overlays,
                                                                      i)));
      return overlays;
    }

  return NULL;
}

static void
clutter_gst_overlays_free (gpointer data)
{
  if (G_LIKELY (data))
    {
      ClutterGstOverlays *overlays = (ClutterGstOverlays *) data;

      g_ptr_array_unref (overlays->overlays);

      g_slice_free (ClutterGstOverlays, overlays);
    }
}

G_DEFINE_BOXED_TYPE (ClutterGstOverlays,
                     clutter_gst_overlays,
                     clutter_gst_overlays_copy,
                     clutter_gst_overlays_free);

static ClutterGstBox *
clutter_gst_box_copy (const ClutterGstBox *box)
{
  if (G_LIKELY (box != NULL))
    return g_slice_dup (ClutterGstBox, box);

  return NULL;
}

static void
clutter_gst_box_free (ClutterGstBox *box)
{
  if (G_LIKELY (box != NULL))
    g_slice_free (ClutterGstBox, box);
}

G_DEFINE_BOXED_TYPE (ClutterGstBox,
                     clutter_gst_box,
                     clutter_gst_box_copy,
                     clutter_gst_box_free);

/**
 * clutter_gst_box_get_width:
 * @box: a #ClutterGstBox
 *
 * Retrieves the width of the @box
 *
 * Return value: the width of the box
 *
 * Since: 3.0
 */
gfloat
clutter_gst_box_get_width (const ClutterGstBox *box)
{
  g_return_val_if_fail (box != NULL, 0.);

  return box->x2 - box->x1;
}

/**
 * clutter_gst_box_get_height:
 * @box: a #ClutterGstBox
 *
 * Retrieves the height of the @box
 *
 * Return value: the height of the box
 *
 * Since: 3.0
 */
gfloat
clutter_gst_box_get_height (const ClutterGstBox *box)
{
  g_return_val_if_fail (box != NULL, 0.);

  return box->y2 - box->y1;
}
