/*
 * Clutter-GStreamer.
 *
 * GStreamer integration library for Clutter.
 *
 * clutter-gst-actor.c - ClutterActor using GStreamer
 *
 * Authored By Matthew Allum     <mallum@openedhand.com>
 *             Damien Lespiau    <damien.lespiau@intel.com>
 *             Lionel Landwerlin <lionel.g.landwerlin@linux.intel.com>
 *             Andre Moreira Magalhaes <andre.magalhaes@collabora.co.uk>
 *
 * Copyright (C) 2006 OpenedHand
 * Copyright (C) 2010, 2011 Intel Corporation
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
 * SECTION:clutter-gst-video-actor
 * @short_description: Actor for playback of video files.
 *
 * #ClutterGstActor is a #ClutterActor that plays video files.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <glib.h>
#include <gio/gio.h>
#include <gst/base/gstbasesink.h>
#include <gst/video/video.h>

#include "clutter-gst-actor.h"
#include "clutter-gst-debug.h"
#include "clutter-gst-enum-types.h"
#include "clutter-gst-marshal.h"
#include "clutter-gst-private.h"

struct _ClutterGstActorPrivate
{
  CoglPipeline *pipeline;

  /* width / height (in pixels) of the frame data before applying the pixel
   * aspect ratio */
  gint buffer_width;
  gint buffer_height;

  /* Pixel aspect ration is par_n / par_d. this is set by the sink */
  guint par_n, par_d;

  /* natural width / height (in pixels) of the actor (after par applied) */
  guint texture_width;
  guint texture_height;

  CoglHandle idle_material;
  CoglColor idle_color_unpre;
};

static CoglPipeline *texture_template_pipeline = NULL;

enum {
  PROP_0,
  PROP_TEXTURE,
  PROP_MATERIAL,
  PROP_IDLE,
  PROP_IDLE_MATERIAL,
  PROP_PAR
};

enum
{
  SIZE_CHANGE,
  LAST_SIGNAL
};

static int actor_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (ClutterGstActor, clutter_gst_actor, CLUTTER_TYPE_ACTOR)

/* Clutter 1.4 has this symbol, we don't want to depend on 1.4 just for that
 * just yet */
static void
_cogl_color_unpremultiply (CoglColor *color)
{
  gfloat alpha;

  alpha = cogl_color_get_alpha (color);

  if (alpha != 0)
    {
      gfloat red, green, blue;

      red = cogl_color_get_red (color);
      green = cogl_color_get_green (color);
      blue = cogl_color_get_blue (color);

      red = red / alpha;
      green = green / alpha;
      blue = blue / alpha;

      cogl_color_set_from_4f (color, red, green, blue, alpha);
    }
}

/* Clutter 1.4 has this symbol, we don't want to depend on 1.4 just for that
 * just yet */
static void
_cogl_color_set_alpha_byte (CoglColor     *color,
                            unsigned char  alpha)
{
  unsigned char red, green, blue;

  red = cogl_color_get_red_byte (color);
  green = cogl_color_get_green_byte (color);
  blue = cogl_color_get_blue_byte (color);

  cogl_color_set_from_4ub (color, red, green, blue, alpha);
}

static void
texture_free_gl_resources (ClutterGstActor *actor)
{
  ClutterGstActorPrivate *priv = actor->priv;

  if (priv->pipeline != NULL)
    {
      /* We want to keep the layer so that the filter settings will
         remain but we want to free its resources so we clear the
         texture handle */
      cogl_pipeline_set_layer_texture (priv->pipeline, 0, NULL);
    }
}

static void
gen_texcoords_and_draw_cogl_rectangle (ClutterActor *actor)
{
  ClutterActorBox box;

  clutter_actor_get_allocation_box (actor, &box);

  cogl_rectangle_with_texture_coords (0, 0,
                                      box.x2 - box.x1,
                                      box.y2 - box.y1,
                                      0, 0, 1.0, 1.0);
}

static void
create_black_idle_material (ClutterGstActor *actor)
{
  ClutterGstActorPrivate *priv = actor->priv;

  priv->idle_material = cogl_material_new ();
  cogl_color_set_from_4ub (&priv->idle_color_unpre, 0, 0, 0, 0xff);
  cogl_material_set_color (priv->idle_material, &priv->idle_color_unpre);
}

/*
 * ClutterActor implementation
 */

static gboolean
clutter_gst_actor_get_paint_volume (ClutterActor       *actor,
                                    ClutterPaintVolume *volume)
{
  ClutterGstActorPrivate *priv = CLUTTER_GST_ACTOR (actor)->priv;
  ClutterActorBox box;

  if (priv->pipeline == NULL)
    return FALSE;

  if (priv->buffer_width == 0 || priv->buffer_height == 0)
    return FALSE;

  /* calling clutter_actor_get_allocation_* can potentially be very
   * expensive, as it can result in a synchronous full stage relayout
   * and redraw
   */
  if (!clutter_actor_has_allocation (actor))
    return FALSE;

  clutter_actor_get_allocation_box (actor, &box);

  /* we only set the width and height, as the paint volume is defined
   * to be relative to the actor's modelview, which means that the
   * allocation's origin has already been applied
   */
  clutter_paint_volume_set_width (volume, box.x2 - box.x1);
  clutter_paint_volume_set_height (volume, box.y2 - box.y1);

  return TRUE;
}

static void
clutter_gst_actor_get_natural_size (ClutterGstActor *actor,
                                    gfloat          *width,
                                    gfloat          *height)
{
  ClutterGstActorPrivate *priv = actor->priv;
  guint dar_n, dar_d;
  gboolean ret;

  /* we cache texture_width and texture_height */

  if (G_UNLIKELY (priv->buffer_width == 0 || priv->buffer_height == 0))
    {
      /* we don't know the size of the frames yet default to 0,0 */
      priv->texture_width = 0;
      priv->texture_height = 0;
    }
  else if (G_UNLIKELY (priv->texture_width == 0 || priv->texture_height == 0))
    {
      CLUTTER_GST_NOTE (ASPECT_RATIO, "frame is %dx%d with par %d/%d",
                        priv->buffer_width, priv->buffer_height,
                        priv->par_n, priv->par_d);

      ret = gst_video_calculate_display_ratio (&dar_n, &dar_d,
                                               priv->buffer_width,
                                               priv->buffer_height,
                                               priv->par_n, priv->par_d,
                                               1, 1);
      if (ret == FALSE)
        dar_n = dar_d = 1;

      if (priv->buffer_height % dar_d == 0)
        {
          priv->texture_width = gst_util_uint64_scale (priv->buffer_height,
                                                       dar_n, dar_d);
          priv->texture_height = priv->buffer_height;
        }
      else if (priv->buffer_width % dar_n == 0)
        {
          priv->texture_width = priv->buffer_width;
          priv->texture_height = gst_util_uint64_scale (priv->buffer_width,
                                                        dar_d, dar_n);

        }
      else
        {
          priv->texture_width = gst_util_uint64_scale (priv->buffer_height,
                                                       dar_n, dar_d);
          priv->texture_height = priv->buffer_height;
        }

      CLUTTER_GST_NOTE (ASPECT_RATIO,
                        "final size is %dx%d (calculated par is %d/%d)",
                        priv->texture_width, priv->texture_height,
                        dar_n, dar_d);
    }

  if (width)
    *width = (gfloat)priv->texture_width;

  if (height)
    *height = (gfloat)priv->texture_height;
}

static void
clutter_gst_actor_get_preferred_width (ClutterActor *self,
                                       gfloat        for_height,
                                       gfloat       *min_width_p,
                                       gfloat       *natural_width_p)
{
  ClutterGstActor *actor = CLUTTER_GST_ACTOR (self);
  ClutterGstActorPrivate *priv = actor->priv;
  gfloat natural_width, natural_height;

  /* Min request is always 0 since we can scale down or clip */
  if (min_width_p)
    *min_width_p = 0;

  if (natural_width_p)
    {
      clutter_gst_actor_get_natural_size (actor,
                                          &natural_width,
                                          &natural_height);

      if (for_height < 0 ||
          priv->buffer_height <= 0)
        {
          *natural_width_p = natural_width;
        }
      else
        {
          gfloat ratio =  natural_width /  natural_height;

          *natural_width_p = ratio * for_height;
        }
    }
}

static void
clutter_gst_actor_get_preferred_height (ClutterActor *self,
                                        gfloat        for_width,
                                        gfloat       *min_height_p,
                                        gfloat       *natural_height_p)
{
  ClutterGstActor *actor = CLUTTER_GST_ACTOR (self);
  ClutterGstActorPrivate *priv = actor->priv;
  gfloat natural_width, natural_height;

  /* Min request is always 0 since we can scale down or clip */
  if (min_height_p)
    *min_height_p = 0;

  if (natural_height_p)
    {
      clutter_gst_actor_get_natural_size (actor,
                                          &natural_width,
                                          &natural_height);

      if (for_width < 0 ||
          priv->buffer_width <= 0)
        {
          *natural_height_p = natural_height;
        }
      else
        {
          gfloat ratio = natural_height / natural_width;

          *natural_height_p = ratio * for_width;
        }
    }
}

static void
clutter_gst_actor_paint (ClutterActor *actor)
{
  ClutterGstActorPrivate *priv = CLUTTER_GST_ACTOR (actor)->priv;
  gboolean is_idle;

  is_idle = clutter_gst_actor_is_idle (CLUTTER_GST_ACTOR (actor));
  if (G_UNLIKELY (is_idle) || !priv->pipeline)
    {
      CoglColor *color;
      gfloat alpha;

      /* blend the alpha of the idle material with the actor's opacity */
      color = cogl_color_copy (&priv->idle_color_unpre);
      alpha = clutter_actor_get_paint_opacity (actor) *
              cogl_color_get_alpha_byte (color) / 0xff;
      _cogl_color_set_alpha_byte (color, alpha);
      cogl_color_premultiply (color);
      cogl_material_set_color (priv->idle_material, color);

      cogl_set_source (priv->idle_material);

      /* draw */
      gen_texcoords_and_draw_cogl_rectangle (actor);
    }
  else
    {
      guint8 paint_opacity;

      paint_opacity = clutter_actor_get_paint_opacity (actor);
      cogl_pipeline_set_color4ub (priv->pipeline,
                                  paint_opacity,
                                  paint_opacity,
                                  paint_opacity,
                                  paint_opacity);
      cogl_set_source (priv->pipeline);

      gen_texcoords_and_draw_cogl_rectangle (actor);
    }
}

/*
 * ClutterGstActor implementation
 */

static gboolean
clutter_gst_actor_is_idle_impl (ClutterGstActor *actor)
{
  ClutterGstActorPrivate *priv = actor->priv;

  return !priv->pipeline;
}

/*
 * GObject implementation
 */

static void
clutter_gst_actor_finalize (GObject *object)
{
  ClutterGstActor *actor = CLUTTER_GST_ACTOR (object);
  ClutterGstActorPrivate *priv = actor->priv;

  texture_free_gl_resources (actor);

  if (priv->pipeline != NULL)
    {
      cogl_object_unref (priv->pipeline);
      priv->pipeline = NULL;
    }

  if (priv->idle_material != COGL_INVALID_HANDLE)
    cogl_handle_unref (priv->idle_material);

  G_OBJECT_CLASS (clutter_gst_actor_parent_class)->finalize (object);
}

static void
clutter_gst_actor_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  ClutterGstActor *actor = CLUTTER_GST_ACTOR (object);
  ClutterGstActorPrivate *priv = actor->priv;

  switch (property_id)
    {
    case PROP_TEXTURE:
      g_value_set_boxed (value, clutter_gst_actor_get_cogl_texture (actor));
      break;
    case PROP_MATERIAL:
      g_value_set_boxed (value, clutter_gst_actor_get_cogl_material (actor));
      break;
    case PROP_IDLE:
      g_value_set_boolean (value, clutter_gst_actor_is_idle (actor));
      break;
    case PROP_IDLE_MATERIAL:
      g_value_set_boxed (value, priv->idle_material);
      break;
    case PROP_PAR:
      gst_value_set_fraction (value, priv->par_n, priv->par_d);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
clutter_gst_actor_set_property (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  ClutterGstActor *actor = CLUTTER_GST_ACTOR (object);
  ClutterGstActorPrivate *priv = actor->priv;

  switch (property_id)
    {
    case PROP_TEXTURE:
      clutter_gst_actor_set_cogl_texture (actor, g_value_get_boxed (value));
      break;
    case PROP_MATERIAL:
      clutter_gst_actor_set_cogl_material (actor, g_value_get_boxed (value));
      break;
    case PROP_IDLE_MATERIAL:
      clutter_gst_actor_set_idle_material (actor, g_value_get_boxed (value));
      break;
    case PROP_PAR:
      priv->par_n = gst_value_get_fraction_numerator (value);
      priv->par_d = gst_value_get_fraction_denominator (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
clutter_gst_actor_class_init (ClutterGstActorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  GParamSpec *pspec;

  g_type_class_add_private (klass, sizeof (ClutterGstActorPrivate));

  klass->is_idle = clutter_gst_actor_is_idle_impl;

  object_class->finalize = clutter_gst_actor_finalize;
  object_class->set_property = clutter_gst_actor_set_property;
  object_class->get_property = clutter_gst_actor_get_property;

  actor_class->get_paint_volume = clutter_gst_actor_get_paint_volume;
  actor_class->get_preferred_width = clutter_gst_actor_get_preferred_width;
  actor_class->get_preferred_height = clutter_gst_actor_get_preferred_height;
  actor_class->paint = clutter_gst_actor_paint;

  /**
   * ClutterGstActor:idle:
   *
   * Whether the #ClutterGstActor is in idle mode.
   */
  pspec = g_param_spec_boolean ("idle",
                                "Idle",
                                "Idle state of the actor",
                                TRUE,
                                CLUTTER_GST_PARAM_READABLE);
  g_object_class_install_property (object_class, PROP_IDLE, pspec);

  /**
   * ClutterGstActor:texture:
   *
   * Texture to use for drawing when not in idle.
   */
  pspec = g_param_spec_boxed ("texture",
                              "Texture",
                              "Texture to use for drawing when not in idle",
                              COGL_TYPE_HANDLE,
                              CLUTTER_GST_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_TEXTURE, pspec);

  /**
   * ClutterGstActor:material:
   *
   * Material to use for drawing when not in idle.
   */
  pspec = g_param_spec_boxed ("material",
                              "Material",
                              "Material to use for drawing when not in idle",
                              COGL_TYPE_HANDLE,
                              CLUTTER_GST_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_MATERIAL, pspec);

  /**
   * ClutterGstActor:idle-material:
   *
   * Material to use for drawing when in idle.
   */
  pspec = g_param_spec_boxed ("idle-material",
                              "Idle material",
                              "Material to use for drawing when in idle",
                              COGL_TYPE_HANDLE,
                              CLUTTER_GST_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_IDLE_MATERIAL, pspec);

  pspec = gst_param_spec_fraction ("pixel-aspect-ratio",
                                   "Pixel Aspect Ratio",
                                   "Pixel aspect ratio of incoming frames",
                                   1, 100, 100, 1, 1, 1,
                                   CLUTTER_GST_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_PAR, pspec);

  /**
   * ClutterGstActor::size-change:
   * @actor: the actor which received the signal
   * @width: the width of the new actor
   * @height: the height of the new actor
   *
   * The ::size-change signal is emitted each time the size of the
   * pixbuf used by @actor changes. The new size is given as
   * argument to the callback.
   */
  actor_signals[SIZE_CHANGE] =
    g_signal_new ("size-change",
                  G_TYPE_FROM_CLASS (object_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (ClutterGstActorClass, size_change),
                  NULL, NULL,
                  _clutter_gst_marshal_VOID__INT_INT,
                  G_TYPE_NONE, 2,
                  G_TYPE_INT,
                  G_TYPE_INT);
}

static void
idle_cb (ClutterGstActor *actor,
         GParamSpec      *pspec,
         gpointer         data)
{
  /* restore the idle material so we don't just display the last frame */
  clutter_actor_queue_redraw (CLUTTER_ACTOR (actor));
}

static void
clutter_gst_actor_init (ClutterGstActor *actor)
{
  ClutterGstActorPrivate *priv;

  actor->priv = priv =
    G_TYPE_INSTANCE_GET_PRIVATE (actor,
                                 CLUTTER_GST_TYPE_ACTOR,
                                 ClutterGstActorPrivate);

  create_black_idle_material (actor);

  if (G_UNLIKELY (texture_template_pipeline == NULL))
    {
      CoglPipeline *pipeline;
      CoglContext *ctx =
        clutter_backend_get_cogl_context (clutter_get_default_backend ());

      texture_template_pipeline = cogl_pipeline_new (ctx);
      pipeline = COGL_PIPELINE (texture_template_pipeline);
      cogl_pipeline_set_layer_null_texture (pipeline,
                                            0, /* layer_index */
                                            COGL_TEXTURE_TYPE_2D);
    }

  g_assert (texture_template_pipeline != NULL);
  priv->pipeline = cogl_pipeline_copy (texture_template_pipeline);

  priv->par_n = priv->par_d = 1;

  g_signal_connect (actor, "notify::idle",
                    G_CALLBACK (idle_cb),
                    NULL);
}

typedef struct _GetLayerState
{
  gboolean has_layer;
  int first_layer;
} GetLayerState;

static gboolean
layer_cb (CoglPipeline *pipeline, int layer, void *user_data)
{
  GetLayerState *state = user_data;

  state->has_layer = TRUE;
  state->first_layer = layer;

  /* We only care about the first layer. */
  return FALSE;
}

static gboolean
get_first_layer_index (CoglPipeline *pipeline, int *layer_index)
{
  GetLayerState state = { FALSE };
  cogl_pipeline_foreach_layer (pipeline, layer_cb, &state);
  if (state.has_layer)
    *layer_index = state.first_layer;

  return state.has_layer;
}

/*
 * Public symbols
 */

/**
 * clutter_gst_actor_get_cogl_texture:
 * @actor: A #ClutterActor
 *
 * Retrieves the handle to the underlying COGL texture used for drawing
 * the actor. No extra reference is taken so if you need to keep the
 * handle then you should call cogl_handle_ref() on it.
 *
 * The texture handle returned is the first layer of the material
 * handle used by the #ClutterActor. If you need to access the other
 * layers you should use clutter_gst_actor_get_cogl_material() instead
 * and use the #CoglMaterial API.
 *
 * Return value: (transfer none): a #CoglHandle for the texture. The returned
 *   handle is owned by the #ClutterActor and it should not be unreferenced
 */
CoglHandle
clutter_gst_actor_get_cogl_texture (ClutterGstActor *actor)
{
  ClutterGstActorPrivate *priv;
  int layer_index;

  g_return_val_if_fail (CLUTTER_GST_IS_ACTOR (actor), NULL);

  priv = actor->priv;

  if (get_first_layer_index (priv->pipeline, &layer_index))
    return cogl_pipeline_get_layer_texture (priv->pipeline, layer_index);

  return NULL;
}

/**
 * clutter_gst_actor_set_cogl_texture:
 * @actor: A #ClutterActor
 * @cogl_tex: A CoglHandle for a texture
 *
 * Replaces the underlying COGL texture drawn by this actor with
 * @cogl_tex. A reference to the texture is taken so if the handle is
 * no longer needed it should be deref'd with cogl_handle_unref.
 */
void
clutter_gst_actor_set_cogl_texture (ClutterGstActor *actor,
                                    CoglHandle       cogl_tex)
{
  ClutterGstActorPrivate  *priv;
  gboolean size_changed;
  guint width, height;

  g_return_if_fail (CLUTTER_GST_IS_ACTOR (actor));
  g_return_if_fail (cogl_is_texture (cogl_tex));

  /* This function can set the texture without the actor being
     realized. This is ok because Clutter requires that the GL context
     always be current so there is no point in waiting to realization
     to set the texture. */

  priv = actor->priv;

  width = cogl_texture_get_width (cogl_tex);
  height = cogl_texture_get_height (cogl_tex);

  /* Reference the new texture now in case it is the same one we are
     already using */
  cogl_object_ref (cogl_tex);

  /* Remove old texture */
  texture_free_gl_resources (actor);

  /* Use the new texture */
  if (priv->pipeline == NULL)
    priv->pipeline = cogl_pipeline_copy (texture_template_pipeline);

  g_assert (priv->pipeline != NULL);
  cogl_pipeline_set_layer_texture (priv->pipeline, 0, cogl_tex);

  /* The pipeline now holds a reference to the texture so we can
     safely release the reference we claimed above */
  cogl_object_unref (cogl_tex);

  size_changed = (width != (guint) priv->buffer_width || height != (guint) priv->buffer_height);
  priv->buffer_width = width;
  priv->buffer_height = height;

  if (size_changed)
    {
      priv->texture_width = priv->texture_height = 0;

      /* queue a relayout to ask containers/layout manager to ask for
       * the preferred size again */
      clutter_actor_queue_relayout (CLUTTER_ACTOR (actor));

      g_signal_emit (actor, actor_signals[SIZE_CHANGE], 0,
                     priv->buffer_width,
                     priv->buffer_height);
    }

  /* If resized actor may need resizing but paint() will do this */
  clutter_actor_queue_redraw (CLUTTER_ACTOR (actor));

  g_object_notify (G_OBJECT (actor), "texture");
}

/**
 * clutter_gst_actor_get_cogl_material:
 * @actor: A #ClutterActor
 *
 * Returns a handle to the underlying COGL material used for drawing
 * the actor.
 *
 * Return value: (transfer none): a handle for a #CoglMaterial. The
 *   material is owned by the #ClutterActor and it should not be
 *   unreferenced
 */
CoglHandle
clutter_gst_actor_get_cogl_material (ClutterGstActor *actor)
{
  g_return_val_if_fail (CLUTTER_GST_IS_ACTOR (actor), NULL);

  return actor->priv->pipeline;
}

/**
 * clutter_gst_actor_set_cogl_material:
 * @actor: A #ClutterActor
 * @cogl_material: A CoglHandle for a material
 *
 * Replaces the underlying Cogl material drawn by this actor with
 * @cogl_material. A reference to the material is taken so if the
 * handle is no longer needed it should be deref'd with
 * cogl_handle_unref. Texture data is attached to the material so
 * calling this function also replaces the Cogl
 * texture. #ClutterActor requires that the material have a texture
 * layer so you should set one on the material before calling this
 * function.
 */
void
clutter_gst_actor_set_cogl_material (ClutterGstActor *actor,
                                     CoglHandle       cogl_material)
{
  CoglPipeline *cogl_pipeline = cogl_material;
  CoglHandle cogl_texture;

  g_return_if_fail (CLUTTER_GST_IS_ACTOR (actor));

  cogl_object_ref (cogl_pipeline);

  if (actor->priv->pipeline)
    cogl_object_unref (actor->priv->pipeline);

  actor->priv->pipeline = cogl_pipeline;

  /* XXX: We are re-asserting the first layer of the new pipeline to ensure the
   * priv state is in sync with the contents of the pipeline. */
  cogl_texture = clutter_gst_actor_get_cogl_texture (actor);
  clutter_gst_actor_set_cogl_texture (actor, cogl_texture);
  /* XXX: If we add support for more pipeline layers, this will need
   * extending */

  g_object_notify (G_OBJECT (actor), "material");
}

/**
 * clutter_gst_actor_is_idle:
 * @actor: a #ClutterGstActor
 *
 * Get the idle state of actor.
 *
 * Return value: TRUE if the actor is in idle, FALSE otherwise.
 */
gboolean
clutter_gst_actor_is_idle (ClutterGstActor *actor)
{
  ClutterGstActorClass *klass;

  g_return_val_if_fail (CLUTTER_GST_IS_ACTOR (actor), TRUE);

  klass = CLUTTER_GST_ACTOR_GET_CLASS (actor);

  return klass->is_idle (actor);
}

/**
 * clutter_gst_actor_get_idle_material:
 * @actor: a #ClutterGstActor
 *
 * Retrieves the material used to draw when the actor is idle.
 *
 * Return value: (transfer none): the #CoglHandle of the idle material
 */
CoglHandle
clutter_gst_actor_get_idle_material (ClutterGstActor *actor)
{
  g_return_val_if_fail (CLUTTER_GST_IS_ACTOR (actor),
                        COGL_INVALID_HANDLE);

  return actor->priv->idle_material;
}

/**
 * clutter_gst_actor_set_idle_material:
 * @actor: a #ClutterGstActor
 * @cogl_material: the handle of a Cogl material
 *
 * Sets a material to use to draw when the actor is idle. The
 * #ClutterGstActor holds a reference of the @material.
 *
 * The default idle material will paint the #ClutterGstActor in black.
 * If %COGL_INVALID_HANDLE is given as @cogl_material to this function, this
 * default idle material will be used.
 */
void
clutter_gst_actor_set_idle_material (ClutterGstActor *actor,
                                     CoglHandle       cogl_material)
{
  ClutterGstActorPrivate *priv;

  g_return_if_fail (CLUTTER_GST_IS_ACTOR (actor));

  priv = actor->priv;
  /* priv->idle_material always has a valid material */
  cogl_handle_unref (priv->idle_material);

  if (cogl_material != COGL_INVALID_HANDLE)
    {
      priv->idle_material = cogl_handle_ref (cogl_material);
      cogl_material_get_color (cogl_material, &priv->idle_color_unpre);
      _cogl_color_unpremultiply (&priv->idle_color_unpre);
    }
  else
    {
      create_black_idle_material (actor);
    }

  g_object_notify (G_OBJECT (actor), "idle-material");
}
