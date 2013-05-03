/*
 * Clutter-GStreamer.
 *
 * GStreamer integration library for Clutter.
 *
 * Copyright (C) 2013 Bastian Winkler <buz@netbuz.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <stdlib.h>
#include <cogl-gst/cogl-gst.h>
#include <clutter-gst/clutter-gst.h>


static const struct {
  ClutterContentGravity gravity;
  const char *name;
} gravities[] = {
  { CLUTTER_CONTENT_GRAVITY_TOP_LEFT, "Top Left" },
  { CLUTTER_CONTENT_GRAVITY_TOP, "Top" },
  { CLUTTER_CONTENT_GRAVITY_TOP_RIGHT, "Top Right" },
  { CLUTTER_CONTENT_GRAVITY_LEFT, "Left" },
  { CLUTTER_CONTENT_GRAVITY_CENTER, "Center" },
  { CLUTTER_CONTENT_GRAVITY_RIGHT, "Right" },
  { CLUTTER_CONTENT_GRAVITY_BOTTOM_LEFT, "Bottom Left" },
  { CLUTTER_CONTENT_GRAVITY_BOTTOM, "Bottom" },
  { CLUTTER_CONTENT_GRAVITY_BOTTOM_RIGHT, "Bottom Right" },
  { CLUTTER_CONTENT_GRAVITY_RESIZE_FILL, "Resize Fill" },
  { CLUTTER_CONTENT_GRAVITY_RESIZE_ASPECT, "Resize Aspect" },
};

static int n_gravities = G_N_ELEMENTS (gravities);
static int cur_gravity = 0;

static const struct {
    ClutterContentRepeat repeat;
    const gchar *name;
} repeats[] = {
  { CLUTTER_REPEAT_NONE, "None" },
  { CLUTTER_REPEAT_X_AXIS, "X-Axis" },
  { CLUTTER_REPEAT_Y_AXIS, "Y-Axis" },
  { CLUTTER_REPEAT_BOTH, "Both" },
};

static int n_repeats = G_N_ELEMENTS (repeats);
static int cur_repeat = 0;


static GstElement *pipeline = NULL;

static gboolean
on_key_press (ClutterActor *stage,
              ClutterEvent *event,
              ClutterActor *actor)
{

  switch (clutter_event_get_key_symbol (event))
    {
    case CLUTTER_KEY_r:
      clutter_actor_set_content_repeat (actor, repeats[cur_repeat].repeat);
      g_print ("Content repeat: %s\n", repeats[cur_repeat].name);
      cur_repeat += 1;
      if (cur_repeat >= n_repeats)
        cur_repeat = 0;
      break;

    case CLUTTER_KEY_q:
      clutter_main_quit ();
      break;

    case CLUTTER_KEY_g:
      clutter_actor_save_easing_state (actor);
      clutter_actor_set_content_gravity (actor, gravities[cur_gravity].gravity);
      clutter_actor_restore_easing_state (actor);
      g_print ("Content gravity: %s\n", gravities[cur_gravity].name);
      cur_gravity += 1;

      if (cur_gravity >= n_gravities)
        cur_gravity = 0;
      break;

    case CLUTTER_KEY_Left:
        {
          gint64 pos, dur;

          if (!gst_element_query_duration (pipeline, GST_FORMAT_TIME, &dur))
            break;
          if (!gst_element_query_position (pipeline, GST_FORMAT_TIME, &pos))
            break;

          gst_element_seek_simple (pipeline, GST_FORMAT_TIME,
                                   GST_SEEK_FLAG_FLUSH,
                                   CLAMP (pos - GST_SECOND * 10, 0, dur));
          break;
        }

    case CLUTTER_KEY_Right:
        {
          gint64 pos, dur;

          if (!gst_element_query_duration (pipeline, GST_FORMAT_TIME, &dur))
            break;
          if (!gst_element_query_position (pipeline, GST_FORMAT_TIME, &pos))
            break;

          gst_element_seek_simple (pipeline, GST_FORMAT_TIME,
                                   GST_SEEK_FLAG_FLUSH,
                                   CLAMP (pos + GST_SECOND * 10, 0, dur));
          break;
        }

    default:
      return CLUTTER_EVENT_PROPAGATE;
    }

  return CLUTTER_EVENT_STOP;
}


int
main (int argc, char *argv[])
{
  ClutterActor *stage, *actor;
  ClutterContent *video;
  CoglGstVideoSink *video_sink;

  if (clutter_init (&argc, &argv) != CLUTTER_INIT_SUCCESS)
    return EXIT_FAILURE;
  gst_init (&argc, &argv);

  stage = clutter_stage_new ();
  g_signal_connect (stage, "destroy",
                    G_CALLBACK (clutter_main_quit), NULL);
  clutter_stage_set_fullscreen (CLUTTER_STAGE (stage), TRUE);

  video = clutter_gst_content_new ();
  video_sink = clutter_gst_content_get_sink (CLUTTER_GST_CONTENT (video));

  actor = clutter_actor_new ();
  clutter_actor_set_reactive (actor, TRUE);
  clutter_actor_set_background_color (actor, CLUTTER_COLOR_Black);
  clutter_actor_add_constraint (actor, clutter_bind_constraint_new (stage, CLUTTER_BIND_SIZE, 0.f));
  clutter_actor_set_content_gravity (actor, gravities[n_gravities - 1].gravity);
  clutter_actor_set_content (actor, video);
  clutter_actor_add_child (stage, actor);

  g_signal_connect (stage, "key-press-event",
                    G_CALLBACK (on_key_press), actor);

  pipeline = gst_element_factory_make ("playbin", NULL);
  g_object_set (pipeline, "uri", argv[1], "video-sink", video_sink, NULL);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  clutter_actor_show (stage);
  clutter_main ();

  return 0;
}
