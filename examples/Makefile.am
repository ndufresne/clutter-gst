NULL = #

noinst_PROGRAMS = camera-player video-player video-sink video-sink-navigation video-content

AM_CPPFLAGS = -I$(top_srcdir) \
		$(MAINTAINER_CFLAGS) \
		$(NULL)

camera_player_SOURCES = camera-player.c
camera_player_CFLAGS = $(CLUTTER_GST_CFLAGS) $(GST_CFLAGS)
camera_player_LDADD = \
    $(CLUTTER_GST_LIBS) \
    $(GST_LIBS)   \
    $(top_builddir)/clutter-gst/libclutter-gst-@CLUTTER_GST_MAJORMINOR@.la

video_player_SOURCES = video-player.c
video_player_CFLAGS = $(CLUTTER_GST_CFLAGS) $(GST_CFLAGS)
video_player_LDADD = \
    $(CLUTTER_GST_LIBS) \
    $(GST_LIBS)   \
    $(top_builddir)/clutter-gst/libclutter-gst-@CLUTTER_GST_MAJORMINOR@.la

video_sink_SOURCES = video-sink.c
video_sink_CFLAGS = $(CLUTTER_GST_CFLAGS) $(GST_CFLAGS)
video_sink_LDADD = \
    $(CLUTTER_GST_LIBS) \
    $(GST_LIBS)   \
    $(top_builddir)/clutter-gst/libclutter-gst-@CLUTTER_GST_MAJORMINOR@.la

video_sink_navigation_SOURCES = video-sink-navigation.c
video_sink_navigation_CFLAGS = $(CLUTTER_GST_CFLAGS) $(GST_CFLAGS)
video_sink_navigation_LDADD = \
    $(CLUTTER_GST_LIBS) \
    $(GST_LIBS)   \
    $(top_builddir)/clutter-gst/libclutter-gst-@CLUTTER_GST_MAJORMINOR@.la

video_content_SOURCES = video-content.c
video_content_CFLAGS = $(CLUTTER_GST_CFLAGS) $(GST_CFLAGS)
video_content_LDADD = \
    $(CLUTTER_GST_LIBS) \
    $(GST_LIBS)   \
    $(top_builddir)/clutter-gst/libclutter-gst-@CLUTTER_GST_MAJORMINOR@.la

EXTRA_DIST = \
	media-actions-pause.png	\
	media-actions-start.png	\
	vid-panel.png          	\
	video-wall.js		\
	video-flip.js		\
	video-flip2.js		\
	pieces.js		\
	simple-player.js	\
	README			\
	$(NULL)
