/*
 * Clutter-GStreamer.
 *
 * GStreamer integration library for Clutter.
 *
 * test-yuv-upload.c - Feed YUV frames to a cluttersink.
 *
 * Authored by Damien Lespiau  <damien.lespiau@intel.com>
 *
 * Copyright (C) 2009 Intel Corporation
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

#include <stdlib.h>
#include <string.h>

#include <glib/gprintf.h>
#include <clutter-gst/clutter-gst.h>

static gint   opt_framerate = 30;
static gchar *opt_fourcc    = "I420";

static GOptionEntry options[] =
{
  { "framerate",
    'f', 0,
    G_OPTION_ARG_INT,
    &opt_framerate,
    "Number of frames per second",
    NULL },
  { "fourcc",
    'o', 0,
    G_OPTION_ARG_STRING,
    &opt_fourcc,
    "Fourcc of the wanted YUV format",
    NULL },

  { NULL }
};

int
main (int argc, char *argv[])
{
  GError           *error = NULL;
  gboolean          result;
  ClutterActor     *stage;
  ClutterActor     *actor;
  GstPipeline      *pipeline;
  GstElement       *src;
  GstElement       *capsfilter;
  GstElement       *sink;
  GstCaps          *caps;
  gchar            *capsstr;

  result = clutter_gst_init_with_args (&argc,
                                       &argv,
                                       " - Test YUV frames uploading",
                                       options,
                                       NULL,
                                       &error);

  if (error)
    {
      g_print ("%s\n", error->message);
      g_error_free (error);
      return EXIT_FAILURE;
    }

  stage = clutter_stage_new ();
  clutter_actor_set_size (CLUTTER_ACTOR (stage), 320.0f, 240.0f);


  /* Set up pipeline */
  pipeline = GST_PIPELINE(gst_pipeline_new (NULL));

  src = gst_element_factory_make ("videotestsrc", NULL);
  capsfilter = gst_element_factory_make ("capsfilter", NULL);
  sink = clutter_gst_create_video_sink ();

  /* Video actor */
  actor = g_object_new (CLUTTER_TYPE_ACTOR,
                        "content", g_object_new (CLUTTER_GST_TYPE_CONTENT,
                                                 "sink", sink,
                                                 NULL),
                        "width", clutter_actor_get_width (stage),
                        "height", clutter_actor_get_height (stage),
                        NULL);

  /* make videotestsrc spit the format we want */
  caps = gst_caps_new_simple ("video/x-raw",
                              "format", G_TYPE_STRING, opt_fourcc,
                              "framerate", GST_TYPE_FRACTION, opt_framerate, 1,
                              NULL);
  g_object_set (capsfilter, "caps", caps, NULL);

  capsstr = gst_caps_to_string (caps);
  g_printf ("%s: [caps] %s\n", __FILE__, capsstr);
  g_free (capsstr);
  gst_bin_add_many (GST_BIN (pipeline), src, capsfilter, sink, NULL);
  result = gst_element_link_many (src, capsfilter, sink, NULL);
  if (result == FALSE)
    g_critical("Could not link elements");
  gst_element_set_state (GST_ELEMENT(pipeline), GST_STATE_PLAYING);

  clutter_actor_add_child (stage, actor);
  clutter_actor_show (stage);

  clutter_main();

  return EXIT_SUCCESS;
}
