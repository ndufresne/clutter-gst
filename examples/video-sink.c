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
  ClutterTimeline    *timeline;
  ClutterActor       *stage;
  ClutterActor       *actor;
  GstElement         *src;
  GstElement         *warp;
  GstElement         *bin;
  GstElement         *pipeline;
  ClutterGstPlayback *player;

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

  /* Make a timeline */
  timeline = clutter_timeline_new (1000);
  g_object_set(timeline, "loop", TRUE, NULL);

  actor = g_object_new (CLUTTER_GST_TYPE_ACTOR, NULL);

  /* Set up pipeline */
  player = clutter_gst_playback_new ();
  pipeline = clutter_gst_player_get_pipeline (CLUTTER_GST_PLAYER (player));

  g_signal_connect (player, "size-change",
                    G_CALLBACK (size_change), actor);

  src = gst_element_factory_make ("videotestsrc", NULL);
  warp = gst_element_factory_make ("warptv", NULL);
  bin = gst_bin_new ("video-test-source");

  gst_bin_add_many (GST_BIN (bin), src, warp, NULL);
  gst_element_link_many (src, warp, NULL);

  g_object_set (pipeline, "source", bin);

  clutter_gst_player_set_playing (CLUTTER_GST_PLAYER (player), TRUE);

  /* start the timeline */
  clutter_timeline_start (timeline);

  clutter_actor_add_child (stage, actor);
  // clutter_actor_set_opacity (texture, 0x11);
  clutter_actor_show (stage);

  clutter_main();

  return EXIT_SUCCESS;
}
