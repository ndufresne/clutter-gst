/*
 * Clutter-GStreamer.
 *
 * GStreamer integration library for Clutter.
 *
 * camera-player.c - A simple camera player.
 *
 * Copyright (C) 2007,2008 OpenedHand
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

#include <stdlib.h>

#include <clutter/clutter.h>
#include <clutter-gst/clutter-gst.h>

typedef struct _CameraApp
{
  ClutterActor *stage;
  ClutterActor *camera_actor;
  ClutterGstCamera *camera_player;
  const GPtrArray *camera_devices;
  guint selected_camera_device;
  gboolean decrease_selected;
} CameraApp;

static gboolean opt_fullscreen = FALSE;

static GOptionEntry options[] =
{
  { "fullscreen",
    'f', 0,
    G_OPTION_ARG_NONE,
    &opt_fullscreen,
    "Start the player in fullscreen",
    NULL },

  { NULL }
};

static void
update_gamma (CameraApp *app)
{
  gdouble min, max, cur;

  if (!clutter_gst_camera_supports_gamma_correction (app->camera_player))
    {
      g_print ("Cannot update gamma, not supported\n");
      return;
    }

  if (!clutter_gst_camera_get_gamma_range (app->camera_player,
                                           &min, &max, NULL))
    {
      g_print ("Cannot update gamma, unable to get allowed range\n");
      return;
    }

  if (!clutter_gst_camera_get_gamma (app->camera_player, &cur))
    {
      g_print ("Cannot update gamma, unable to get current value\n");
      return;
    }

  g_print ("Updating gamma:\n");
  g_print ("\tmin value: %0.2f\n", min);
  g_print ("\tmax value: %0.2f\n", max);
  g_print ("\tcur value: %0.2f\n", cur);
  if (app->decrease_selected)
    {
      cur -= 0.1;
      if (cur < min)
        cur = min;
    }
  else
    {
      cur += 0.1;
      if (cur > max)
        cur = max;
    }

  g_print ("\tnew value: %0.2f\n", cur);
  clutter_gst_camera_set_gamma (app->camera_player, cur);
}

static void
update_color_balance (CameraApp *app,
                      const gchar *property)
{
  gdouble min, max, cur;

  if (!clutter_gst_camera_supports_color_balance (app->camera_player))
    {
      g_print ("Cannot update color balance property %s, "
               "not supported\n",
               property);
      return;
    }

  if (!clutter_gst_camera_get_color_balance_property_range (app->camera_player,
                                                            property, &min, &max, NULL))
    {
      g_print ("Cannot update color balance property %s, "
               "unable to get allowed range\n",
               property);
      return;
    }

  if (!clutter_gst_camera_get_color_balance_property (app->camera_player,
                                                      property, &cur))
    {
      g_print ("Cannot update color balance property %s, "
               "unable to get current value\n",
               property);
      return;
    }

  g_print ("Updating color balance property %s:\n", property);
  g_print ("\tmin value: %0.2f\n", min);
  g_print ("\tmax value: %0.2f\n", max);
  g_print ("\tcur value: %0.2f\n", cur);
  if (app->decrease_selected)
    {
      cur -= 0.1;
      if (cur < min)
        cur = min;
    }
  else
    {
      cur += 0.1;
      if (cur > max)
        cur = max;
    }

  g_print ("\tnew value: %0.2f\n", cur);
  clutter_gst_camera_set_color_balance_property (app->camera_player,
                                                 property, cur);
}

static gboolean
input_cb (ClutterStage *stage,
          ClutterEvent *event,
          gpointer      user_data)
{
  CameraApp *app = (CameraApp*)user_data;
  gboolean handled = FALSE;

  switch (event->type)
    {
    case CLUTTER_KEY_PRESS:
      {
        switch (clutter_event_get_key_symbol (event))
          {
          case CLUTTER_minus:
            app->decrease_selected = TRUE;
            break;
          case CLUTTER_plus:
            app->decrease_selected = FALSE;
            break;
          case CLUTTER_b:
            update_color_balance (app, "brightness");
            break;
          case CLUTTER_c:
            update_color_balance (app, "contrast");
            break;
          case CLUTTER_s:
            update_color_balance (app, "saturation");
            break;
          case CLUTTER_h:
            update_color_balance (app, "hue");
            break;
          case CLUTTER_g:
            update_gamma (app);
            break;

          case CLUTTER_d:
            {
              ClutterGstCameraDevice *device;

              if (app->camera_devices->len == 1)
                break;

              app->selected_camera_device++;
              if (app->selected_camera_device >= app->camera_devices->len)
                app->selected_camera_device = 0;
              device = g_ptr_array_index (app->camera_devices, app->selected_camera_device);
              g_print ("Selecting device %s (node=%s)\n",
                       clutter_gst_camera_device_get_name (device),
                       clutter_gst_camera_device_get_node (device));
              clutter_gst_camera_set_camera_device (app->camera_player, device);
              break;
            }

          case CLUTTER_q:
          case CLUTTER_Escape:
            clutter_main_quit ();
            break;

          case CLUTTER_v:
            {
              gchar *filename;
              static guint photos_cnt = 0;

              if (clutter_gst_camera_is_recording_video (app->camera_player))
                {
                  g_print ("Stopping video recording\n");

                  clutter_gst_camera_stop_video_recording (app->camera_player);
                }
              else if (!clutter_gst_camera_is_ready_for_capture (app->camera_player))
                g_print ("Unable to record video as the camera is not ready for capture\n");
              else
                {
                  g_print ("Recording video!\n");

                  filename = g_strdup_printf ("camera-video-%d.ogv", photos_cnt++);

                  clutter_gst_camera_start_video_recording (app->camera_player,
                                                            filename);
                  g_free (filename);
                }
              break;
            }

          case CLUTTER_p:
            {
              gchar *filename;
              static guint photos_cnt = 0;

              if (clutter_gst_camera_is_recording_video (app->camera_player))
                g_print ("Unable to take photo as the camera is recording video\n");
              else if (!clutter_gst_camera_is_ready_for_capture (app->camera_player))
                g_print ("Unable to take photo as the camera is not ready for capture\n");
              else
                {
                  g_print ("Taking picture!\n");

                  filename = g_strdup_printf ("camera-photo-%d.jpg", photos_cnt++);

                  clutter_gst_camera_take_photo (app->camera_player,
                                                 filename);
                  g_free (filename);
                }
              break;
            }

          case CLUTTER_e:
            {
              GstElement *filter;
              gboolean ret;

              filter = gst_element_factory_make ("dicetv", NULL);
              if (!filter)
                {
                  g_print ("ERROR: Unable to create 'dicetv' element, cannot set filter\n");
                }
              ret = clutter_gst_camera_set_filter (app->camera_player, filter);
              if (ret)
                g_print ("Filter set successfully\n");
              else
                g_print ("ERROR: Unable to set filter\n");

              break;
            }

          case CLUTTER_r:
            clutter_gst_camera_remove_filter (app->camera_player);
            break;

          default:
            break;
          }
      }
    default:
      break;
    }

  return handled;
}

static void
ready_for_capture (ClutterGstCamera *camera_player,
                   gboolean ready)
{
  if (ready)
    g_print ("Ready for capture!\n");
}

static void
photo_saved (ClutterGstCamera *camera_player)
{
  g_print ("Photo saved!\n");
}

static void
video_saved (ClutterGstCamera *camera_player)
{
  g_print ("Video saved!\n");
}

static void
size_change (ClutterGstPlayer *player,
             gint              base_width,
             gint              base_height,
             CameraApp        *app)
{
  ClutterActor *stage = app->stage;
  gfloat new_x, new_y, new_width, new_height;
  gfloat stage_width, stage_height;
  gfloat frame_width, frame_height;

  clutter_actor_get_size (stage, &stage_width, &stage_height);

  /* base_width and base_height are the actual dimensions of the buffers before
   * taking the pixel aspect ratio into account. We need to get the actual
   * size of the texture to display */
  clutter_actor_get_preferred_size (app->camera_actor,
                                    NULL, NULL,
                                    &frame_width, &frame_height);

  new_height = (frame_height * stage_width) / frame_width;
  if (new_height <= stage_height)
    {
      new_width = stage_width;

      new_x = 0;
      new_y = (stage_height - new_height) / 2;
    }
  else
    {
      new_width  = (frame_width * stage_height) / frame_height;
      new_height = stage_height;

      new_x = (stage_width - new_width) / 2;
      new_y = 0;
    }

  clutter_actor_set_position (app->camera_actor, new_x, new_y);
  clutter_actor_set_size (app->camera_actor, new_width, new_height);
}

int
main (int argc, char *argv[])
{
  CameraApp    *app = NULL;
  ClutterActor *stage;
  ClutterColor  stage_color = { 0x00, 0x00, 0x00, 0x00 };
  GError       *error = NULL;
  guint         i;

  clutter_gst_init_with_args (&argc,
                              &argv,
                              " - A simple camera player",
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
  clutter_actor_set_background_color (stage, &stage_color);
  clutter_actor_set_size (stage, 768, 576);
  clutter_stage_set_minimum_size (CLUTTER_STAGE (stage), 640, 480);
  if (opt_fullscreen)
    clutter_stage_set_fullscreen (CLUTTER_STAGE (stage), TRUE);

  app = g_new0(CameraApp, 1);
  app->stage = stage;
  app->camera_actor = clutter_gst_aspectratio_new ();

  app->camera_player = clutter_gst_camera_new ();
  if (app->camera_player == NULL)
    {
      g_error ("failed to create camera player");
      return EXIT_FAILURE;
    }

  app->camera_devices = clutter_gst_camera_get_camera_devices (app->camera_player);
  if (!app->camera_devices)
    {
      g_error ("no suitable camera device available");
      return EXIT_FAILURE;
    }
  g_print ("Available camera devices:\n");
  for (i = 0; i < app->camera_devices->len; ++i)
    {
      ClutterGstCameraDevice *device;

      device = g_ptr_array_index (app->camera_devices, i);
      g_print ("\tdevice %s (node=%s)\n",
               clutter_gst_camera_device_get_name (device),
               clutter_gst_camera_device_get_node (device));

      clutter_gst_camera_device_set_capture_resolution (device, 800, 600);
    }
  app->selected_camera_device = 0;

  g_signal_connect (app->camera_player, "ready-for-capture",
                    G_CALLBACK (ready_for_capture),
                    app);
  g_signal_connect (app->camera_player, "photo-saved",
                    G_CALLBACK (photo_saved),
                    app);
  g_signal_connect (app->camera_player, "video-saved",
                    G_CALLBACK (video_saved),
                    app);
  /* Handle it ourselves so can scale up for fullscreen better */
  g_signal_connect_after (app->camera_player,
                          "size-change",
                          G_CALLBACK (size_change), app);


  clutter_gst_actor_set_player (CLUTTER_GST_ACTOR (app->camera_actor),
                                CLUTTER_GST_PLAYER (app->camera_player));

  /* Add control UI to stage */
  clutter_actor_add_child (stage, app->camera_actor);

  clutter_stage_hide_cursor (CLUTTER_STAGE (stage));

  /* Hook up other events */
  g_signal_connect (stage, "event", G_CALLBACK (input_cb), app);

  clutter_gst_player_set_playing (CLUTTER_GST_PLAYER (app->camera_player), TRUE);

  clutter_actor_show (stage);

  clutter_main ();

  return EXIT_SUCCESS;
}
