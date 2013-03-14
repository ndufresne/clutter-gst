/* clutter-gst-aspectratio.h */

#ifndef __CLUTTER_GST_ASPECTRATIO_H__
#define __CLUTTER_GST_ASPECTRATIO_H__

#include <glib-object.h>

#include <clutter-gst/clutter-gst-actor.h>

G_BEGIN_DECLS

#define CLUTTER_GST_TYPE_ASPECTRATIO clutter_gst_aspectratio_get_type()

#define CLUTTER_GST_ASPECTRATIO(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  CLUTTER_GST_TYPE_ASPECTRATIO, ClutterGstAspectratio))

#define CLUTTER_GST_ASPECTRATIO_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  CLUTTER_GST_TYPE_ASPECTRATIO, ClutterGstAspectratioClass))

#define CLUTTER_GST_IS_ASPECTRATIO(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  CLUTTER_GST_TYPE_ASPECTRATIO))

#define CLUTTER_GST_IS_ASPECTRATIO_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  CLUTTER_GST_TYPE_ASPECTRATIO))

#define CLUTTER_GST_ASPECTRATIO_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  CLUTTER_GST_TYPE_ASPECTRATIO, ClutterGstAspectratioClass))

typedef struct _ClutterGstAspectratio ClutterGstAspectratio;
typedef struct _ClutterGstAspectratioClass ClutterGstAspectratioClass;
typedef struct _ClutterGstAspectratioPrivate ClutterGstAspectratioPrivate;

struct _ClutterGstAspectratio
{
  ClutterGstActor parent;

  ClutterGstAspectratioPrivate *priv;
};

struct _ClutterGstAspectratioClass
{
  ClutterGstActorClass parent_class;
};

GType clutter_gst_aspectratio_get_type (void) G_GNUC_CONST;

ClutterGstAspectratio *clutter_gst_aspectratio_new (void);

G_END_DECLS

#endif /* __CLUTTER_GST_ASPECTRATIO_H__ */
