/*
 * Clutter-GStreamer.
 *
 * GStreamer integration library for Clutter.
 *
 * video-sink.c - A small example around the videotestsrc ! capsfilter !
 *                navigationtest ! videoconvert ! cluttersink pipeline.
 *
 * Copyright (C) 2007,2008 OpenedHand
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

#include <clutter-gst/clutter-gst.h>

int
main (int argc, char *argv[])
{
  ClutterTimeline  *timeline;
  ClutterActor     *stage;
  ClutterActor     *actor;
  ClutterConstraint *constraint;
  GstPipeline      *pipeline;
  GstElement       *src;
  GstElement       *filter;
  GstElement       *test;
  GstElement       *colorspace;
  GstElement       *sink;

  if (argc < 1)
    {
      g_error ("Usage: %s", argv[0]);
      return EXIT_FAILURE;
    }

  if (clutter_init (&argc, &argv) != CLUTTER_INIT_SUCCESS)
    {
      g_error ("Failed to initialize clutter\n");
      return EXIT_FAILURE;
    }
  gst_init (&argc, &argv);

  stage = clutter_stage_new ();
  clutter_stage_set_user_resizable (CLUTTER_STAGE (stage), TRUE);

  /* Make a timeline */
  timeline = clutter_timeline_new (1000);
  g_object_set(timeline, "loop", TRUE, NULL);

  actor = g_object_new (CLUTTER_TYPE_ACTOR,
                        "content", clutter_gst_content_new (),
                        NULL);

  /* Set up pipeline */
  pipeline = GST_PIPELINE(gst_pipeline_new (NULL));

  src = gst_parse_launch ("videotestsrc", NULL);
  filter = gst_parse_launch ("capsfilter caps=video/x-raw,pixel-aspect-ratio=1/4", NULL);

  test = gst_element_factory_make ("navigationtest", NULL);
  colorspace = gst_element_factory_make ("videoconvert", NULL);

  sink = gst_element_factory_make ("clutterautovideosink", NULL);
  g_object_set (sink, "content", clutter_actor_get_content (actor), NULL);

  // g_object_set (src , "pattern", 10, NULL);

  gst_bin_add_many (GST_BIN (pipeline), src, filter, test, colorspace, sink, NULL);
  gst_element_link_many (src, filter, test, colorspace,
                         GST_ELEMENT (clutter_gst_content_get_sink (CLUTTER_GST_CONTENT (clutter_actor_get_content (actor)))),
                         NULL);
  gst_element_set_state (GST_ELEMENT(pipeline), GST_STATE_PLAYING);

  /* Resize with the window */
  constraint = clutter_bind_constraint_new (stage, CLUTTER_BIND_SIZE, 0.0);
  clutter_actor_add_constraint_with_name (actor, "size", constraint);

  /* Rotate a bit */
  clutter_actor_set_pivot_point (actor, 0.5, 0.5);
  clutter_actor_set_rotation_angle (actor, CLUTTER_Z_AXIS, 45.0);

  /* start the timeline */
  clutter_timeline_start (timeline);

  clutter_actor_add_child (stage, actor);
  // clutter_actor_set_opacity (texture, 0x11);
  clutter_actor_show (stage);

  clutter_main();

  return EXIT_SUCCESS;
}
