#ifndef EDMVAPPLICATION_H
#define EDMVAPPLICATION_H

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define EDMV_TYPE_APPLICATION ( edmv_application_get_type() )
G_DECLARE_FINAL_TYPE( EdmvApplication, edmv_application, EDMV, APPLICATION, GApplication )

EdmvApplication* edmv_application_new( const gchar *application_id );
gchar* edmv_application_get_editor( EdmvApplication *self );
GStrv edmv_application_get_inputs( EdmvApplication *self );

G_END_DECLS

#endif
