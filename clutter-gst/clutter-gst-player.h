/*
 * Clutter-GStreamer.
 *
 * GStreamer integration library for Clutter.
 *
 * clutter-gst-player.h - Wrap some convenience functions around playbin2
 *
 * Authored By Damien Lespiau    <damien.lespiau@intel.com>
 *             Lionel Landwerlin <lionel.g.landwerlin@linux.intel.com>
 *             Matthew Allum     <mallum@openedhand.com>
 *             Emmanuele Bassi   <ebassi@linux.intel.com>
 *             Andre Moreira Magalhaes <andre.magalhaes@collabora.co.uk>
 *
 * Copyright (C) 2006 OpenedHand
 * Copyright (C) 2009-2011 Intel Corporation
 * Copyright (C) 2011 Intel Corporation
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

#ifndef __CLUTTER_GST_PLAYER_H__
#define __CLUTTER_GST_PLAYER_H__

#include <glib-object.h>

#include <clutter/clutter.h>

#include <clutter-gst/clutter-gst-types.h>

G_BEGIN_DECLS

#define CLUTTER_GST_TYPE_PLAYER clutter_gst_player_get_type()

#define CLUTTER_GST_PLAYER(obj)                                         \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj),                                   \
                               CLUTTER_GST_TYPE_PLAYER,                 \
                               ClutterGstPlayer))
#define CLUTTER_GST_IS_PLAYER(obj)                              \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj),                           \
                               CLUTTER_GST_TYPE_PLAYER))
#define CLUTTER_GST_PLAYER_GET_INTERFACE(obj)                           \
  (G_TYPE_INSTANCE_GET_INTERFACE ((obj),                                \
                                  CLUTTER_GST_TYPE_PLAYER,              \
                                  ClutterGstPlayerIface))

typedef struct _ClutterGstPlayer ClutterGstPlayer;
typedef struct _ClutterGstPlayerIface ClutterGstPlayerIface;
typedef struct _ClutterGstPlayerIfacePrivate ClutterGstPlayerIfacePrivate;


/**
 * ClutterGstPlayer:
 *
 * #ClutterGstPlayer is an opaque structure whose members cannot be
 * directly accessed
 *
 * Since: 1.4
 */

/**
 * ClutterGstPlayerIface:
 * @download_buffering: handler for the #ClutterGstPlayer::download-buffering
 * signal
 *
 * Interface vtable for #ClutterGstPlayer implementations
 *
 * Since: 1.4
 */
struct _ClutterGstPlayerIface
{
  /*< private >*/
  GTypeInterface base_iface;

  ClutterGstPlayerIfacePrivate *priv;

  /*< public >*/
  GstElement * (* get_pipeline)   (ClutterGstPlayer *player);

  gchar * (* get_user_agent) (ClutterGstPlayer *player);
  void    (* set_user_agent) (ClutterGstPlayer *player,
                              const gchar      *user_agent);

  ClutterGstSeekFlags (* get_seek_flags) (ClutterGstPlayer *player);
  void                (* set_seek_flags) (ClutterGstPlayer    *player,
                                          ClutterGstSeekFlags  flags);

  ClutterGstBufferingMode (* get_buffering_mode) (ClutterGstPlayer *player);
  void                    (* set_buffering_mode) (ClutterGstPlayer        *player,
                                                  ClutterGstBufferingMode  mode);

  GList * (* get_audio_streams) (ClutterGstPlayer *player);
  gint    (* get_audio_stream)  (ClutterGstPlayer *player);
  void    (* set_audio_stream)  (ClutterGstPlayer *player,
                                 gint              index_);

  GList * (* get_subtitle_tracks) (ClutterGstPlayer *player);
  gint    (* get_subtitle_track)  (ClutterGstPlayer *player);
  void    (* set_subtitle_track)  (ClutterGstPlayer *player,
                                   gint              index_);

  gboolean (*get_idle) (ClutterGstPlayer *player);

  gboolean (*get_in_seek) (ClutterGstPlayer *player);

  gint   (* get_buffer_size) (ClutterGstPlayer *player);
  void   (* set_buffer_size) (ClutterGstPlayer *player,
                              gint              size);

  gint64 (* get_buffer_duration) (ClutterGstPlayer *player);
  void   (* set_buffer_duration) (ClutterGstPlayer *player,
                                  gint64            duration);

  gboolean (* is_live_media) (ClutterGstPlayer *player);

  void (* _iface_reserved21) (void);
  void (* _iface_reserved22) (void);
  void (* _iface_reserved23) (void);
  void (* _iface_reserved24) (void);
  void (* _iface_reserved25) (void);
  void (* _iface_reserved26) (void);
  void (* _iface_reserved27) (void);
  void (* _iface_reserved28) (void);
  void (* _iface_reserved29) (void);
  void (* _iface_reserved30) (void);
  void (* _iface_reserved31) (void);
  void (* _iface_reserved32) (void);
  void (* _iface_reserved33) (void);
  void (* _iface_reserved34) (void);
  void (* _iface_reserved35) (void);

  /* signals */
  void (* download_buffering)  (ClutterGstPlayer *player,
                                gdouble           start,
                                gdouble           stop);
  void (* eos)                 (ClutterGstPlayer *player);
  void (* error)               (ClutterGstPlayer *player,
                                const GError *error);

  void (* _clutter_reserved4)  (void);
  void (* _clutter_reserved5)  (void);
  void (* _clutter_reserved6)  (void);
  void (* _clutter_reserved7)  (void);
  void (* _clutter_reserved8)  (void);
  void (* _clutter_reserved9)  (void);
  void (* _clutter_reserved10) (void);
};

GType clutter_gst_player_get_type (void) G_GNUC_CONST;

void                      clutter_gst_player_class_init          (GObjectClass *object_class);

gboolean                  clutter_gst_player_init                (ClutterGstPlayer        *player);
void                      clutter_gst_player_deinit              (ClutterGstPlayer        *player);

GstElement *              clutter_gst_player_get_pipeline        (ClutterGstPlayer        *player);

void                      clutter_gst_player_set_uri             (ClutterGstPlayer        *player,
                                                                  const gchar             *uri);
gchar *                   clutter_gst_player_get_uri             (ClutterGstPlayer        *player);
void                      clutter_gst_player_set_filename        (ClutterGstPlayer        *player,
                                                                  const gchar             *filename);

void                      clutter_gst_player_set_playing         (ClutterGstPlayer        *player,
                                                                  gboolean                 playing);
gboolean                  clutter_gst_player_is_playing          (ClutterGstPlayer        *player);


gchar *                   clutter_gst_player_get_user_agent      (ClutterGstPlayer        *player);
void                      clutter_gst_player_set_user_agent      (ClutterGstPlayer        *player,
                                                                  const gchar             *user_agent);

ClutterGstSeekFlags       clutter_gst_player_get_seek_flags      (ClutterGstPlayer        *player);
void                      clutter_gst_player_set_seek_flags      (ClutterGstPlayer        *player,
                                                                  ClutterGstSeekFlags      flags);

ClutterGstBufferingMode   clutter_gst_player_get_buffering_mode  (ClutterGstPlayer        *player);
void                      clutter_gst_player_set_buffering_mode  (ClutterGstPlayer        *player,
                                                                  ClutterGstBufferingMode  mode);
gdouble                   clutter_gst_player_get_buffer_fill     (ClutterGstPlayer        *player);
gint                      clutter_gst_player_get_buffer_size     (ClutterGstPlayer        *player);
void                      clutter_gst_player_set_buffer_size     (ClutterGstPlayer        *player,
                                                                  gint                     size);
gint64                    clutter_gst_player_get_buffer_duration (ClutterGstPlayer        *player);
void                      clutter_gst_player_set_buffer_duration (ClutterGstPlayer        *player,
                                                                  gint64                   duration);

void                      clutter_gst_player_set_audio_volume    (ClutterGstPlayer        *player,
                                                                  gdouble                  volume);
gdouble                   clutter_gst_player_get_audio_volume    (ClutterGstPlayer        *player);
GList *                   clutter_gst_player_get_audio_streams   (ClutterGstPlayer        *player);
gint                      clutter_gst_player_get_audio_stream    (ClutterGstPlayer        *player);
void                      clutter_gst_player_set_audio_stream    (ClutterGstPlayer        *player,
                                                                  gint                     index_);

void                      clutter_gst_player_set_subtitle_uri    (ClutterGstPlayer        *player,
                                                                  const gchar             *uri);
gchar *                   clutter_gst_player_get_subtitle_uri    (ClutterGstPlayer        *player);
void                      clutter_gst_player_set_subtitle_font_name
                                                                 (ClutterGstPlayer        *player,
                                                                  const char              *font_name);
gchar *                   clutter_gst_player_get_subtitle_font_name
                                                                 (ClutterGstPlayer        *player);
GList *                   clutter_gst_player_get_subtitle_tracks (ClutterGstPlayer       *player);
gint                      clutter_gst_player_get_subtitle_track  (ClutterGstPlayer       *player);
void                      clutter_gst_player_set_subtitle_track  (ClutterGstPlayer       *player,
                                                                  gint                    index_);

gboolean                  clutter_gst_player_get_idle            (ClutterGstPlayer        *player);

gboolean                  clutter_gst_player_get_can_seek        (ClutterGstPlayer        *player);
gboolean                  clutter_gst_player_get_in_seek         (ClutterGstPlayer        *player);

void                      clutter_gst_player_set_progress        (ClutterGstPlayer        *player,
                                                                  gdouble                  progress);
gdouble                   clutter_gst_player_get_progress        (ClutterGstPlayer        *player);
gdouble                   clutter_gst_player_get_duration        (ClutterGstPlayer        *player);

gboolean                  clutter_gst_player_is_live_media       (ClutterGstPlayer        *player);

G_END_DECLS

#endif /* __CLUTTER_GST_PLAYER_H__ */
