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
clutter_gst_frame_new (CoglPipeline *pipeline)
{
  ClutterGstFrame *frame = g_slice_new0 (ClutterGstFrame);
  CoglTexture *texture;

  frame->pipeline = cogl_object_ref (pipeline);
  texture = cogl_pipeline_get_layer_texture (pipeline, 0);

  frame->resolution.width = cogl_texture_get_width (texture);
  frame->resolution.height = cogl_texture_get_height (texture);
}

static gpointer
clutter_gst_frame_copy (gpointer data)
{
  if (G_LIKELY (data))
    {
      ClutterGstFrame *frame = g_slice_dup (ClutterGstFrame, data);

      if (frame->pipeline != COGL_INVALID_HANDLE)
        frame->pipeline = cogl_handle_ref (frame->pipeline);

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
        cogl_handle_unref (frame->pipeline);
      g_slice_free (ClutterGstFrame, frame);
    }
}

G_DEFINE_BOXED_TYPE (ClutterGstFrame,
                     clutter_gst_frame,
                     clutter_gst_frame_copy,
                     clutter_gst_frame_free);
