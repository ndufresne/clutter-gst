/*
 * Clutter-GStreamer.
 *
 * GStreamer integration library for Clutter.
 *
 * clutter-gst-aspectratio.c - An actor rendering a video with respect
 * to its aspect ratio.
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

#include "clutter-gst-crop.h"
#include "clutter-gst-private.h"

G_DEFINE_TYPE (ClutterGstCrop, clutter_gst_crop, CLUTTER_GST_TYPE_ACTOR)

#define CROP_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), CLUTTER_GST_TYPE_CROP, ClutterGstCropPrivate))

struct _ClutterGstCropPrivate
{
  ClutterActorBox input_region;
  ClutterActorBox output_region;

  gboolean paint_borders;
};

enum
{
  PROP_0,

  PROP_PAINT_BORDERS,
  PROP_INPUT_REGION,
  PROP_OUTPUT_REGION
};

/**/

static void
clutter_gst_crop_paint_frame (ClutterGstActor *self,
                              ClutterGstFrame *frame)
{
  ClutterGstCropPrivate *priv = CLUTTER_GST_CROP (self)->priv;
  guint8 paint_opacity;
  ClutterActorBox box;
  gfloat box_width, box_height;

  clutter_actor_get_allocation_box (CLUTTER_ACTOR (self), &box);
  box_width = clutter_actor_box_get_width (&box);
  box_height = clutter_actor_box_get_height (&box);

  paint_opacity = clutter_actor_get_paint_opacity (CLUTTER_ACTOR (self));
  cogl_pipeline_set_color4ub (frame->pipeline,
                              paint_opacity,
                              paint_opacity,
                              paint_opacity,
                              paint_opacity);
  cogl_set_source (frame->pipeline);

  cogl_rectangle_with_texture_coords (priv->output_region.x1 * box_width,
                                      priv->output_region.y1 * box_height,
                                      priv->output_region.x2 * box_width,
                                      priv->output_region.y2 * box_height,
                                      priv->input_region.x1,
                                      priv->input_region.y1,
                                      priv->input_region.x2,
                                      priv->input_region.y2);

  if (priv->paint_borders &&
      (priv->output_region.x1 > 0 ||
       priv->output_region.x2 < 1 ||
       priv->output_region.y1 > 0 ||
       priv->output_region.y2 < 1))
    {
      ClutterColor bg_color;

      clutter_actor_get_background_color (CLUTTER_ACTOR (self), &bg_color);

      cogl_set_source_color4ub (bg_color.red,
                                bg_color.green,
                                bg_color.blue,
                                paint_opacity);

      if (priv->output_region.x1 > 0)
        cogl_rectangle (0, 0, priv->output_region.x1 * box_width, 1);
      if (priv->output_region.x2 < 1)
        cogl_rectangle (priv->output_region.x2 * box_width, 0, 1, 1);
      if (priv->output_region.y1 > 0)
        cogl_rectangle (priv->output_region.x1 * box_width,
                        0,
                        priv->output_region.x2 * box_width,
                        priv->output_region.y1 * box_height);
      if (priv->output_region.y2 < 1)
        cogl_rectangle (priv->output_region.x1 * box_width,
                        priv->output_region.y2 * box_height,
                        priv->output_region.x2 * box_width,
                        0);
    }
}

static gboolean
_validate_box (ClutterActorBox *box)
{
  if (box->x1 >= 0 &&
      box->x1 <= 1 &&
      box->y1 >= 0 &&
      box->y1 <= 1 &&
      box->x2 >= 0 &&
      box->x2 <= 1 &&
      box->y2 >= 0 &&
      box->y2 <= 1)
    return TRUE;

  return FALSE;
}

static void
clutter_gst_crop_get_property (GObject    *object,
                               guint       property_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  ClutterGstCropPrivate *priv = CLUTTER_GST_CROP (object)->priv;
  ClutterActorBox *box;

  switch (property_id)
    {
    case PROP_PAINT_BORDERS:
      g_value_set_boolean (value, priv->paint_borders);
      break;
    case PROP_INPUT_REGION:
      box = (ClutterActorBox *) g_value_get_boxed (value);
      *box = priv->input_region;
      break;
    case PROP_OUTPUT_REGION:
      box = (ClutterActorBox *) g_value_get_boxed (value);
      *box = priv->output_region;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
clutter_gst_crop_set_property (GObject      *object,
                               guint         property_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  ClutterGstCropPrivate *priv = CLUTTER_GST_CROP (object)->priv;
  ClutterActorBox *box;

  switch (property_id)
    {
    case PROP_PAINT_BORDERS:
      priv->paint_borders = g_value_get_boolean (value);
      break;
    case PROP_INPUT_REGION:
      box = (ClutterActorBox *) g_value_get_boxed (value);
      if (_validate_box (box))
        priv->input_region = *box;
      else
        g_warning ("Input region must be given in [0, 1] values.");
      break;
    case PROP_OUTPUT_REGION:
      box = (ClutterActorBox *) g_value_get_boxed (value);
      if (_validate_box (box))
        priv->output_region = *box;
      else
        g_warning ("Output region must be given in [0, 1] values.");
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
clutter_gst_crop_dispose (GObject *object)
{
  G_OBJECT_CLASS (clutter_gst_crop_parent_class)->dispose (object);
}

static void
clutter_gst_crop_finalize (GObject *object)
{
  G_OBJECT_CLASS (clutter_gst_crop_parent_class)->finalize (object);
}

static void
clutter_gst_crop_class_init (ClutterGstCropClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterGstActorClass *gst_actor_class = CLUTTER_GST_ACTOR_CLASS (klass);
  GParamSpec *pspec;

  g_type_class_add_private (klass, sizeof (ClutterGstCropPrivate));

  object_class->get_property = clutter_gst_crop_get_property;
  object_class->set_property = clutter_gst_crop_set_property;
  object_class->dispose = clutter_gst_crop_dispose;
  object_class->finalize = clutter_gst_crop_finalize;

  gst_actor_class->paint_frame = clutter_gst_crop_paint_frame;

  /**
   * ClutterGstCrop:paint-borders:
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

  /**
   * ClutterGstCrop:input-region:
   *
   * Input region in the video frame (all values between 0 and 1).
   *
   * Since: 3.0
   */
  pspec = g_param_spec_boxed ("input-region",
                              "Input Region",
                              "Input Region",
                              CLUTTER_TYPE_ACTOR_BOX,
                              CLUTTER_GST_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_INPUT_REGION, pspec);

  /**
   * ClutterGstCrop:output-region:
   *
   * Output region in the actor's allocation (all values between 0 and 1).
   *
   * Since: 3.0
   */
  pspec = g_param_spec_boxed ("output-region",
                              "Output Region",
                              "Output Region",
                              CLUTTER_TYPE_ACTOR_BOX,
                              CLUTTER_GST_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_OUTPUT_REGION, pspec);
}

static void
clutter_gst_crop_init (ClutterGstCrop *self)
{
  ClutterGstCropPrivate *priv;

  priv = self->priv = CROP_PRIVATE (self);

  priv->input_region.x1 = 0;
  priv->input_region.y1 = 0;
  priv->input_region.x2 = 1;
  priv->input_region.y2 = 1;

  priv->output_region = priv->input_region;
}

ClutterActor *
clutter_gst_crop_new (void)
{
  return g_object_new (CLUTTER_GST_TYPE_CROP, NULL);
}
