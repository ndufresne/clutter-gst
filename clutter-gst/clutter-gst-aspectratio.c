/*
 * Clutter-GStreamer.
 *
 * GStreamer integration library for Clutter.
 *
 * clutter-gst-aspectratio.c - An object implementing the
 * ClutterContent interface to render a video with respect to its
 * aspect ratio.
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

/**
 * SECTION:clutter-gst-aspectratio
 * @short_description: A #ClutterContent for displaying video frames with respect to their aspect ratio.
 *
 * #ClutterGstContent implements the #ClutterContent interface.
 */

#include "clutter-gst-aspectratio.h"
#include "clutter-gst-private.h"

static void content_iface_init (ClutterContentIface *iface);

G_DEFINE_TYPE_WITH_CODE (ClutterGstAspectratio,
                         clutter_gst_aspectratio,
                         CLUTTER_GST_TYPE_CONTENT,
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_CONTENT,
                                                content_iface_init))

#define ASPECTRATIO_PRIVATE(o)                                  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o),                            \
                                CLUTTER_GST_TYPE_ASPECTRATIO,   \
                                ClutterGstAspectratioPrivate))

struct _ClutterGstAspectratioPrivate
{
  gboolean paint_borders;
};

enum
{
  PROP_0,

  PROP_PAINT_BORDERS
};

/**/

static void
clutter_gst_aspectratio_get_frame_box (ClutterGstAspectratio *self,
                                       ClutterGstBox         *paint_box,
                                       ClutterActorBox       *content_box,
                                       ClutterGstFrame       *frame)
{
  gfloat actor_width, actor_height;
  gdouble new_width, new_height;
  gdouble frame_aspect, actor_aspect;

  actor_width = clutter_actor_box_get_width (content_box);
  actor_height = clutter_actor_box_get_height (content_box);

  if (actor_width <= 0 || actor_height <= 0)
    return;

  frame_aspect = (gdouble) frame->resolution.width / (gdouble) frame->resolution.height;
  actor_aspect = actor_width / actor_height;

  if (actor_aspect < frame_aspect)
    {
      new_width = actor_width;
      new_height = actor_width / frame_aspect;
    }
  else
    {
      new_height = actor_height;
      new_width = actor_height * frame_aspect;
    }

  paint_box->x1 = (actor_width - new_width) / 2;
  paint_box->y1 = (actor_height - new_height) / 2;
  paint_box->x2 = paint_box->x1 + new_width;
  paint_box->y2 = paint_box->y1 + new_height;
}

/**/

static gboolean
clutter_gst_aspectratio_get_preferred_size (ClutterContent *content,
                                            gfloat         *width,
                                            gfloat         *height)
{
  ClutterGstFrame *frame =
    clutter_gst_content_get_frame (CLUTTER_GST_CONTENT (content));

  if (!frame)
    return FALSE;

  if (width)
    *width = frame->resolution.width;
  if (height)
    *height = frame->resolution.height;

  return TRUE;
}

static void
clutter_gst_aspectratio_paint_content (ClutterContent   *content,
                                       ClutterActor     *actor,
                                       ClutterPaintNode *root)
{
  ClutterGstAspectratio *self = CLUTTER_GST_ASPECTRATIO (content);
  ClutterGstAspectratioPrivate *priv = self->priv;
  ClutterGstFrame *frame =
    clutter_gst_content_get_frame (CLUTTER_GST_CONTENT (content));
  ClutterGstBox paint_box;
  ClutterActorBox content_box;
  ClutterPaintNode *node;
  guint8 paint_opacity = clutter_actor_get_paint_opacity (actor);
  ClutterColor color;

  clutter_actor_get_content_box (actor, &content_box);

  if (!frame)
    {
      /* No frame to paint, just paint the background color of the
         actor. */
      if (priv->paint_borders)
        {
          clutter_actor_get_background_color (actor, &color);
          color.alpha = paint_opacity;

          node = clutter_color_node_new (&color);
          clutter_paint_node_set_name (node, "BlankVideoFrame");

          clutter_paint_node_add_rectangle_custom (node,
                                                   content_box.x1, content_box.y1,
                                                   content_box.x2, content_box.y2);
          clutter_paint_node_add_child (root, node);
          clutter_paint_node_unref (node);
        }

      return;
    }

  clutter_gst_aspectratio_get_frame_box (self, &paint_box, &content_box, frame);

  if (priv->paint_borders)
    {
      clutter_actor_get_background_color (actor, &color);
      color.alpha = paint_opacity;

      node = clutter_color_node_new (&color);
      clutter_paint_node_set_name (node, "AspectRatioVideoBorders");

      if (clutter_actor_box_get_width (&content_box) !=
          clutter_gst_box_get_width (&paint_box))
        {
          clutter_paint_node_add_rectangle_custom (node,
                                                   content_box.x1, content_box.y1,
                                                   paint_box.x1, content_box.y2);
          clutter_paint_node_add_rectangle_custom (node,
                                                   paint_box.x2, content_box.y1,
                                                   content_box.x2, content_box.y2);
        }
      if (clutter_actor_box_get_height (&content_box) !=
          clutter_gst_box_get_height (&paint_box))
        {
          clutter_paint_node_add_rectangle_custom (node,
                                                   content_box.x1, content_box.y1,
                                                   content_box.x2, paint_box.y1);
          clutter_paint_node_add_rectangle_custom (node,
                                                   content_box.x1, paint_box.y2,
                                                   content_box.x2, content_box.y2);
        }

      clutter_paint_node_add_child (root, node);
      clutter_paint_node_unref (node);
    }

  cogl_pipeline_set_color4ub (frame->pipeline,
                              paint_opacity, paint_opacity,
                              paint_opacity, paint_opacity);

  node = clutter_pipeline_node_new (frame->pipeline);
  clutter_paint_node_set_name (node, "AspectRatioVideoFrame");

  clutter_paint_node_add_rectangle_custom (node,
                                           paint_box.x1, paint_box.y1,
                                           paint_box.x2, paint_box.y2);

  clutter_paint_node_add_child (root, node);
  clutter_paint_node_unref (node);
}

static void
content_iface_init (ClutterContentIface *iface)
{
  iface->get_preferred_size = clutter_gst_aspectratio_get_preferred_size;
  iface->paint_content = clutter_gst_aspectratio_paint_content;
}

/**/

static void
clutter_gst_aspectratio_get_property (GObject    *object,
                                      guint       property_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  ClutterGstAspectratioPrivate *priv = CLUTTER_GST_ASPECTRATIO (object)->priv;

  switch (property_id)
    {
    case PROP_PAINT_BORDERS:
      g_value_set_boolean (value, priv->paint_borders);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
clutter_gst_aspectratio_set_property (GObject      *object,
                                      guint         property_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  ClutterGstAspectratioPrivate *priv = CLUTTER_GST_ASPECTRATIO (object)->priv;

  switch (property_id)
    {
    case PROP_PAINT_BORDERS:
      if (priv->paint_borders != g_value_get_boolean (value))
        {
          priv->paint_borders = g_value_get_boolean (value);
          clutter_content_invalidate (CLUTTER_CONTENT (object));
        }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
clutter_gst_aspectratio_dispose (GObject *object)
{
  G_OBJECT_CLASS (clutter_gst_aspectratio_parent_class)->dispose (object);
}

static void
clutter_gst_aspectratio_finalize (GObject *object)
{
  G_OBJECT_CLASS (clutter_gst_aspectratio_parent_class)->finalize (object);
}

static void
clutter_gst_aspectratio_class_init (ClutterGstAspectratioClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *pspec;

  g_type_class_add_private (klass, sizeof (ClutterGstAspectratioPrivate));

  object_class->get_property = clutter_gst_aspectratio_get_property;
  object_class->set_property = clutter_gst_aspectratio_set_property;
  object_class->dispose = clutter_gst_aspectratio_dispose;
  object_class->finalize = clutter_gst_aspectratio_finalize;

  /**
   * ClutterGstAspectratio:paint-borders:
   *
   * Whether or not paint borders on the sides of the video
   *
   * Since: 3.0
   */
  pspec = g_param_spec_boolean ("paint-borders",
                                "Paint borders",
                                "Paint borders on side of video",
                                FALSE,
                                CLUTTER_GST_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_PAINT_BORDERS, pspec);
}

static void
clutter_gst_aspectratio_init (ClutterGstAspectratio *self)
{
  self->priv = ASPECTRATIO_PRIVATE (self);
}

/**
 * clutter_gst_aspectratio_new:
 *
 * Returns: (transfer full): a new #ClutterGstAspectratio instance
 */
ClutterContent *
clutter_gst_aspectratio_new (void)
{
  return g_object_new (CLUTTER_GST_TYPE_ASPECTRATIO, NULL);
}
