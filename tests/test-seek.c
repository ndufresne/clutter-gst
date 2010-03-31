/*
 * Clutter-GStreamer.
 *
 * GStreamer integration library for Clutter.
 *
 * video-player.c - A simple fullscreen video player with an OSD.
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

#include <clutter/clutter.h>
#include <clutter-gst/clutter-gst.h>

static gboolean
random_seek (ClutterGstVideoTexture  *video_texture)
{
  double progress;

  progress = g_random_double ();

  printf ("progress %lf\n", progress);
  clutter_media_set_progress (CLUTTER_MEDIA (video_texture), progress);

  return TRUE;
}

static void
on_playing (ClutterGstVideoTexture *video_texture,
            GParamSpec             *pspec,
            gpointer                data)
{
  g_timeout_add (250, (GSourceFunc) random_seek, video_texture);
}

static gboolean
input_cb (ClutterStage *stage,
          ClutterEvent *event,
          gpointer      user_data)
{
  switch (event->type)
    {
    case CLUTTER_KEY_PRESS:
      switch (clutter_event_get_key_symbol (event))
        {
      case CLUTTER_q:
      case CLUTTER_Escape:
        clutter_main_quit ();
        break;
        }
    default:
      break;
    }

  return FALSE;
}

static void
size_change (ClutterTexture *texture,
             gint            width,
             gint            height,
             ClutterActor   *stage)
{
  gfloat new_x, new_y, new_width, new_height;
  gfloat stage_width, stage_height;

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

  clutter_actor_set_position (CLUTTER_ACTOR (texture), new_x, new_y);
  clutter_actor_set_size (CLUTTER_ACTOR (texture), new_width, new_height);
}

int
main (int argc, char *argv[])
{
  ClutterActor *stage, *video_texture;
  ClutterColor stage_color = { 0x00, 0x00, 0x00, 0x00 };
  GError *error = NULL;

  if (!g_thread_supported ())
    g_thread_init (NULL);

  clutter_gst_init_with_args (&argc,
                              &argv,
                              " - Test seeking with ClutterGstVideoTexture",
                              NULL,
                              NULL,
                              &error);

  if (argc < 2)
    {
      g_print ("Usage: %s [OPTIONS] <video file>\n", argv[0]);
      return EXIT_FAILURE;
    }

  stage = clutter_stage_get_default ();
  clutter_stage_set_color (CLUTTER_STAGE (stage), &stage_color);

  video_texture = clutter_gst_video_texture_new ();
  g_assert (video_texture);

  g_object_set (G_OBJECT(video_texture), "sync-size", FALSE, NULL);

  /* Handle it ourselves so can scale up for fullscreen better */
  g_signal_connect (CLUTTER_TEXTURE (video_texture),
                    "size-change",
                    G_CALLBACK (size_change), stage);

  /* Load up out video texture */
  clutter_media_set_filename (CLUTTER_MEDIA (video_texture), argv[1]);
  clutter_media_set_audio_volume (CLUTTER_MEDIA (video_texture), 0.5);
  g_signal_connect (video_texture,
                    "notify::duration",
                    G_CALLBACK (on_playing), NULL);

  /* Add control UI to stage */
  clutter_container_add_actor (CLUTTER_CONTAINER (stage), video_texture);

  clutter_stage_hide_cursor (CLUTTER_STAGE (stage));

  /* Hook up other events */
  g_signal_connect (stage, "event", G_CALLBACK (input_cb), stage);

  clutter_media_set_playing (CLUTTER_MEDIA (video_texture), TRUE);

  clutter_actor_show (stage);

  clutter_main ();

  return 0;
}
