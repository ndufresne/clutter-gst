#define COGL_ENABLE_EXPERIMENTAL_API 1
#define CLUTTER_ENABLE_EXPERIMENTAL_API 1

#include <clutter-gst/clutter-gst.h>

/**/

/**/

static gfloat tiles = 40;
static CoglTexture *base_texture;

/**/

#define MY_TYPE_CONTENT            (my_content_get_type())
#define MY_CONTENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MY_TYPE_CONTENT, MyContent))
#define MY_IS_CONTENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MY_TYPE_CONTENT))
#define MY_CONTENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), MY_TYPE_CONTENT, MyContentClass))
#define MY_IS_CONTENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), MY_TYPE_CONTENT))
#define MY_CONTENT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), MY_TYPE_CONTENT, MyContentClass))

typedef struct _MyContent            MyContent;
typedef struct _MyContentPrivate     MyContentPrivate;
typedef struct _MyContentClass       MyContentClass;

struct _MyContent
{
  GObject parent;

  CoglPipeline *pipeline;
  ClutterGstFrame *last_frame;
  ClutterGstFrame *current_frame;
};

struct _MyContentClass
{
  GObjectClass parent_class;
};

static void content_iface_init (ClutterContentIface *iface);

G_DEFINE_TYPE_WITH_CODE (MyContent,
                         my_content,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (CLUTTER_TYPE_CONTENT, content_iface_init))

static void
my_content_render_offscreen (MyContent *self)
{
  CoglOffscreen *offscreen;

  offscreen = cogl_offscreen_new_to_texture (base_texture);
  cogl_push_framebuffer (offscreen);
  cogl_ortho (0,
              cogl_texture_get_width (base_texture),
              cogl_texture_get_height (base_texture),
              0,
              0,
              1.0);

  cogl_set_source (self->current_frame->pipeline);
  cogl_rectangle (0,
                  0,
                  cogl_texture_get_width (base_texture),
                  cogl_texture_get_height (base_texture));

  cogl_pop_framebuffer ();
  cogl_handle_unref (offscreen);

  if (self->last_frame)
    g_boxed_free (CLUTTER_GST_TYPE_FRAME, self->last_frame);
  self->last_frame = g_boxed_copy (CLUTTER_GST_TYPE_FRAME,
                                   self->current_frame);
}

static void
my_content_paint_content (ClutterContent   *content,
                          ClutterActor     *actor,
                          ClutterPaintNode *root)
{
  MyContent *self = MY_CONTENT (content);
  ClutterActorBox box;
  ClutterPaintNode *node;
  guint8 paint_opacity;
  float t_w, t_h;

  if (self->current_frame != self->last_frame)
    my_content_render_offscreen (self);

  clutter_actor_get_content_box (actor, &box);
  paint_opacity = clutter_actor_get_paint_opacity (actor);

  cogl_pipeline_set_color4ub (self->pipeline,
                              paint_opacity, paint_opacity,
                              paint_opacity, paint_opacity);
  cogl_pipeline_set_uniform_1f (self->pipeline,
                                cogl_pipeline_get_uniform_location (self->pipeline, "n_tiles"),
                                tiles);
  cogl_pipeline_set_layer_wrap_mode (self->pipeline,
                                     0,
                                     COGL_PIPELINE_WRAP_MODE_REPEAT);

  node = clutter_pipeline_node_new (self->pipeline);
  clutter_paint_node_set_name (node, "Video");
  clutter_paint_node_add_rectangle (node, &box);
  clutter_paint_node_add_child (root, node);
  clutter_paint_node_unref (node);
}

static void
content_iface_init (ClutterContentIface *iface)
{
  iface->paint_content = my_content_paint_content;
}

static void
my_content_class_init (MyContentClass *klass)
{
}

static void
my_content_init (MyContent *self)
{
}


void
my_content_set_current_frame (MyContent *self, ClutterGstFrame *frame)
{

  if (self->current_frame)
    g_boxed_free (CLUTTER_GST_TYPE_FRAME, self->current_frame);
  self->current_frame = g_boxed_copy (CLUTTER_GST_TYPE_FRAME, frame);
  clutter_content_invalidate (CLUTTER_CONTENT (self));
}

MyContent *
my_content_new (CoglPipeline *pipeline)
{
  MyContent *content = g_object_new (MY_TYPE_CONTENT,
                                     NULL);

  content->pipeline = pipeline;

  return content;
}

/**/

static ClutterGstPlayer *camera;
static CoglContext *context;
static ClutterTexture *clutter_texture;
static MyContent *mycontent;

/* camera ---(scaled down to)---> base_texture ---(offscreen)---> base_pipeline */

static void
_allocation_changed (ClutterActor           *actor,
                     ClutterActorBox        *allocation,
                     ClutterAllocationFlags  flags,
                     gpointer                user_data)
{
  if (base_texture)
    cogl_object_unref (base_texture);
  base_texture =
    cogl_texture_2d_new_with_size (context, tiles, tiles,
                                   COGL_PIXEL_FORMAT_ARGB_8888);

  cogl_pipeline_set_layer_texture (mycontent->pipeline, 0, base_texture);
}

static void
update_gamma (gboolean increase)
{
  gdouble min, max, cur;

  if (!clutter_gst_camera_supports_gamma_correction (CLUTTER_GST_CAMERA (camera)))
    {
      g_warning ("Cannot update gamma, not supported\n");
      return;
    }

  if (!clutter_gst_camera_get_gamma_range (CLUTTER_GST_CAMERA (camera),
                                           &min, &max, NULL))
    {
      g_warning ("Cannot update gamma, unable to get allowed range\n");
      return;
    }

  if (!clutter_gst_camera_get_gamma (CLUTTER_GST_CAMERA (camera), &cur))
    {
      g_warning ("Cannot update gamma, unable to get current value\n");
      return;
    }

  g_print ("Updating gamma:\n");
  g_print ("\tmin value: %0.2f\n", min);
  g_print ("\tmax value: %0.2f\n", max);
  g_print ("\tcur value: %0.2f\n", cur);
  if (!increase)
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
  clutter_gst_camera_set_gamma (CLUTTER_GST_CAMERA (camera), cur);
}

static gboolean
_key_press (ClutterActor *actor,
            ClutterEvent *event,
            gpointer user_data)
{
  ClutterActorBox alloc;

  switch (clutter_event_get_key_symbol (event))
    {
    case CLUTTER_KEY_plus:
      tiles++;
      _allocation_changed (actor, NULL, 0, NULL);
      return TRUE;

    case CLUTTER_KEY_minus:
      tiles--;
      _allocation_changed (actor, NULL, 0, NULL);
      return TRUE;

    case CLUTTER_g:
      update_gamma (TRUE);
      break;

    case CLUTTER_h:
      update_gamma (FALSE);
      break;
    }

  return FALSE;
}

static void
_camera_new_frame (ClutterGstPlayer *self,
                   ClutterGstFrame  *frame,
                   gpointer          user_data)
{
  CoglOffscreen *offscreen;

  my_content_set_current_frame (mycontent, frame);
}

int
main (int argc, char *argv[])
{
  ClutterActor *stage, *actor;
  ClutterContent *content;
  CoglPipeline *base_pipeline;
  CoglSnippet *snippet;
  ClutterTimeline *timeline;

  clutter_gst_init (&argc, &argv);

  stage = g_object_new (CLUTTER_TYPE_STAGE,
                        "width", 900.0,
                        "height", 600.0,
                        "layout-manager", g_object_new (CLUTTER_TYPE_BIN_LAYOUT,
                                                        /* "orientation", CLUTTER_ORIENTATION_HORIZONTAL, */
                                                        "x-align", CLUTTER_BIN_ALIGNMENT_FILL,
                                                        "y-align", CLUTTER_BIN_ALIGNMENT_FILL,
                                                        NULL),
                        "background-color", clutter_color_new (0, 0, 0, 0xff),
                        "user-resizable", TRUE,
                        NULL);
  g_signal_connect (stage, "destroy",
                    G_CALLBACK (clutter_main_quit), NULL);

  context = clutter_backend_get_cogl_context (clutter_get_default_backend ());

  camera = CLUTTER_GST_PLAYER (clutter_gst_camera_new ());
  g_signal_connect (camera, "new-frame",
                    G_CALLBACK (_camera_new_frame),
                    NULL);
  clutter_gst_player_set_playing (camera, TRUE);

  base_pipeline = cogl_pipeline_new (context);
  snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                              "uniform float n_tiles;\n",
                              NULL);
  cogl_snippet_set_replace (snippet,
                            "vec4 mod_color = texture2D (cogl_sampler0, cogl_tex_coord_in[0].xy);\n"
                            "vec4 color = texture2D (cogl_sampler0, cogl_tex_coord_in[0].xy * n_tiles);\n"
                            "cogl_color_out.a = cogl_color_in.a;\n"
                            "cogl_color_out.r = mod_color.r * color.r;\n"
                            "cogl_color_out.g = mod_color.g * color.g;\n"
                            "cogl_color_out.b = mod_color.b * color.b;\n");
  cogl_pipeline_add_snippet (base_pipeline, snippet);

  mycontent = my_content_new (base_pipeline);
  actor = g_object_new (CLUTTER_TYPE_ACTOR,
                        "content", mycontent,
                        NULL);
  g_signal_connect (actor, "allocation-changed",
                    G_CALLBACK (_allocation_changed), NULL);
  g_signal_connect (stage, "key-press-event",
                    G_CALLBACK (_key_press), NULL);
  clutter_actor_add_child (stage, actor);


  /* content = clutter_gst_content_new (); */
  /* clutter_gst_content_set_player (CLUTTER_GST_CONTENT (content), camera); */
  /* actor = g_object_new (CLUTTER_TYPE_ACTOR, */
  /*                       "content", content, */
  /*                       "opacity", 0, */
  /*                       NULL); */
  /* clutter_actor_add_child (stage, actor); */

  /* clutter_actor_save_easing_state (actor); */
  /* clutter_actor_set_easing_duration (actor, 5000); */
  /* clutter_actor_set_easing_mode (actor, CLUTTER_LINEAR); */
  /* clutter_actor_set_opacity (actor, 255); */
  /* clutter_actor_restore_easing_state (actor); */

  /* timeline = CLUTTER_TIMELINE (clutter_actor_get_transition (actor, "opacity")); */
  /* clutter_timeline_set_repeat_count (timeline, -1); */

  clutter_actor_show (stage);
  clutter_main ();

  return 0;
}
