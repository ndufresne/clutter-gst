/*
 * Clutter-GStreamer.
 *
 * GStreamer integration library for Clutter.
 *
 * video-sink.c - A small example around the videotestsrc ! warptv !
 *                videoconvert ! cluttersink pipeline.
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

void
size_change (ClutterGstPlayer *player,
             gint              width,
             gint              height,
             ClutterActor     *actor)
{
  ClutterActor *stage;
  gfloat new_x, new_y, new_width, new_height;
  gfloat stage_width, stage_height;

  stage = clutter_actor_get_stage (actor);
  if (stage == NULL)
    return;

  clutter_actor_get_size (stage, &stage_width, &stage_height);

  new_height = (height * stage_width) / width;
  if (new_height <= stage_height)
    {
      new_width = stage_width;

      new_x = 0;
      new_y = (stage_height - new_height) / 2;
    }
  else
    {
      new_width  = (width * stage_height) / height;
      new_height = stage_height;

      new_x = (stage_width - new_width) / 2;
      new_y = 0;
    }

  clutter_actor_set_position (actor, new_x, new_y);
  clutter_actor_set_size (actor, new_width, new_height);
}

int
main (int argc, char *argv[])
{
  ClutterActor *stage;
  ClutterActor *actor;
  GstElement   *src;
  GstElement   *warp;
  GstElement   *sink;
  GstElement   *pipeline;

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
  g_object_set (stage,
                "layout-manager",
                clutter_bin_layout_new (CLUTTER_BIN_ALIGNMENT_FILL,
                                        CLUTTER_BIN_ALIGNMENT_FILL),
                NULL);

  sink = clutter_gst_create_video_sink ();
  actor = g_object_new (CLUTTER_TYPE_ACTOR,
                        "content",
                        g_object_new (CLUTTER_GST_TYPE_CONTENT,
                                      "sink", sink, NULL),
                        "width", 200.0,
                        "height", 200.0,
                        NULL);

  /* Set up pipeline */
  pipeline = gst_pipeline_new ("warptv");

  src = gst_element_factory_make ("videotestsrc", NULL);
  warp = gst_element_factory_make ("warptv", NULL);

  gst_bin_add_many (GST_BIN (pipeline), src, warp, sink, NULL);
  gst_element_link_many (src, warp, sink, NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  clutter_actor_add_child (stage, actor);
  clutter_actor_show (stage);

  clutter_main();

  return EXIT_SUCCESS;
}
