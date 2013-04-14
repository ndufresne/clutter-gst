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

#if !defined(__CLUTTER_GST_H_INSIDE__) && !defined(CLUTTER_GST_COMPILATION)
#error "Only <clutter-gst/clutter-gst.h> can be include directly."
#endif

#ifndef __CLUTTER_GST_PIPELINE_H__
#define __CLUTTER_GST_PIPELINE_H__

#include <glib-object.h>

#include <cogl-gst/cogl-gst.h>

G_BEGIN_DECLS

#define CLUTTER_GST_TYPE_PIPELINE clutter_gst_pipeline_get_type()

#define CLUTTER_GST_PIPELINE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  CLUTTER_GST_TYPE_PIPELINE, ClutterGstPipeline))

#define CLUTTER_GST_PIPELINE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  CLUTTER_GST_TYPE_PIPELINE, ClutterGstPipelineClass))

#define CLUTTER_GST_IS_PIPELINE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  CLUTTER_GST_TYPE_PIPELINE))

#define CLUTTER_GST_IS_PIPELINE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  CLUTTER_GST_TYPE_PIPELINE))

#define CLUTTER_GST_PIPELINE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  CLUTTER_GST_TYPE_PIPELINE, ClutterGstPipelineClass))

typedef struct _ClutterGstPipeline ClutterGstPipeline;
typedef struct _ClutterGstPipelineClass ClutterGstPipelineClass;
typedef struct _ClutterGstPipelinePrivate ClutterGstPipelinePrivate;

struct _ClutterGstPipeline
{
  GObject parent;

  ClutterGstPipelinePrivate *priv;
};

struct _ClutterGstPipelineClass
{
  GObjectClass parent_class;
};

GType clutter_gst_pipeline_get_type (void) G_GNUC_CONST;

ClutterGstPipeline *clutter_gst_pipeline_new (void);

CoglGstVideoSink   *clutter_gst_pipeline_get_video_sink (ClutterGstPipeline *self);
void                clutter_gst_pipeline_set_video_sink (ClutterGstPipeline *self,
                                                         CoglGstVideoSink   *sink);

G_END_DECLS

#endif /* __CLUTTER_GST_PIPELINE_H__ */
