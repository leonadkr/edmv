#include "edmvapplication.h"

#include "config.h"

#include <gio/gio.h>
#include <glib.h>
#include <glib-object.h>

struct _EdmvApplication
{
	GApplication parent_instance;

	gchar *config_path;

	gchar *editor;
	GStrv inputs;
};
typedef struct _EdmvApplication EdmvApplication;

enum _EdmvApplicationPropertyID
{
	PROP_0, /* 0 is reserved for GObject */

	PROP_EDITOR,
	PROP_INPUTS,

	N_PROPS
};
typedef enum _EdmvApplicationPropertyID EdmvApplicationPropertyID;

static GParamSpec *object_props[N_PROPS] = { NULL, };

G_DEFINE_TYPE( EdmvApplication, edmv_application, G_TYPE_APPLICATION )

static void
edmv_application_init(
	EdmvApplication *self )
{
	const GOptionEntry option_entries[]=
	{
		{ "editor", 'e', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, NULL, "Editor to use", "EDITOR" },
		{ NULL }
	};

	gchar *summary;

	self->config_path = g_build_filename( g_get_user_config_dir(), PROGRAM_NAME, CONFIG_FILENAME, NULL );

	summary = g_strdup_printf(
		"This program renames FILES with an external editor." TEXT_FILE_LINE_BREAKER
		"Be aware of entering \'.\' and \'..\' in the input, it may cause an uncertain behavior." TEXT_FILE_LINE_BREAKER
		"Argument EDITOR, option in \'%s\', value of $VISUAL or $EDITOR in this order determine the editor.",
		self->config_path );

	g_application_set_option_context_parameter_string( G_APPLICATION( self ), "FILES" );
	g_application_set_option_context_description( G_APPLICATION( self ), PROGRAM_NAME" version "PROGRAM_VERSION );
	g_application_set_option_context_summary( G_APPLICATION( self ), summary );
	g_free( summary );
	g_application_add_main_option_entries( G_APPLICATION( self ), option_entries );

	self->editor = NULL;
	self->inputs = NULL;
}

static void
edmv_application_finalize(
	GObject *object )
{
	EdmvApplication *self = EDMV_APPLICATION( object );

	g_free( self->config_path );
	g_free( self->editor );
	g_strfreev( self->inputs );

	G_OBJECT_CLASS( edmv_application_parent_class )->finalize( object );
}

static void
edmv_application_get_property(
	GObject *object,
	guint prop_id,
	GValue *value,
	GParamSpec *pspec )
{
	EdmvApplication *self = EDMV_APPLICATION( object );

	switch( (EdmvApplicationPropertyID)prop_id )
	{
		case PROP_EDITOR:
			g_value_set_string( value, self->editor );
			break;
		case PROP_INPUTS:
			g_value_set_boxed( value, self->inputs );
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID( object, prop_id, pspec );
			break;
	}
}

static gint
edmv_application_command_line(
	GApplication *app,
	GApplicationCommandLine *command_line )
{
	EdmvApplication *self = EDMV_APPLICATION( app );
	GVariantDict *options;
	gchar *editor = NULL;
	GKeyFile *key_file;
	GStrv argv;
	gint argc, i;
	gchar *s_locale, *s_utf8;
	GError *error = NULL;

	options = g_application_command_line_get_options_dict( command_line );

	/* get input options */
	g_variant_dict_lookup( options, "editor", "s", &editor );

	/* get input, convert strings from current locale to UTF-8 */
	argv = g_application_command_line_get_arguments( command_line, &argc );
	if( argc < 2 )
	{
		g_free( editor );
		g_strfreev( argv );
		return EXIT_SUCCESS;
	}
	for( i = 1; i < argc; ++i )
	{
		s_locale = argv[i];
		if( !g_utf8_validate( s_locale, -1, NULL ) )
		{
			s_utf8 = g_locale_to_utf8( s_locale, -1, NULL, NULL, &error );
			if( error != NULL )
			{
				g_log_structured( PROGRAM_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
					"MESSAGE", error->message,
					NULL );
				g_clear_error( &error );
				g_free( editor );
				g_strfreev( argv );
				return EXIT_FAILURE;
			}

			g_free( s_locale );
			argv[i] = s_utf8;
		}
	}
	self->inputs = g_memdup2( argv + 1, argc * sizeof( gchar* ) );
	g_free( argv[0] );
	g_free( argv );

	/* try environment variables */
	if( editor == NULL )
	{
		editor = g_strdup( g_getenv( "VISUAL" ) );
		if( editor == NULL )
			editor = g_strdup( g_getenv( "EDITOR" ) );
	}

	/* parse configure file */
	key_file = g_key_file_new();
	if( !g_key_file_load_from_file( key_file, self->config_path, G_KEY_FILE_NONE, NULL ) )
	{
		if( editor == NULL )
			editor = g_key_file_get_string( key_file, "Main", "editor", NULL );
	}
	g_key_file_free( key_file );

	g_free( self->editor );
	self->editor = editor;

	g_application_activate( G_APPLICATION( self ) );

	return EXIT_SUCCESS;
}

static void
edmv_application_class_init(
	EdmvApplicationClass *klass )
{
	GObjectClass *object_class = G_OBJECT_CLASS( klass );
	GApplicationClass *app_class = G_APPLICATION_CLASS( klass );

	object_class->finalize = edmv_application_finalize;
	object_class->get_property = edmv_application_get_property;
	object_props[PROP_EDITOR] = g_param_spec_string(
		"editor",
		"Editor",
		"The editor to modify an input file list",
		NULL,
		G_PARAM_READABLE | G_PARAM_STATIC_STRINGS );
	object_props[PROP_INPUTS] = g_param_spec_boxed(
		"inputs",
		"Inputs",
		"GStrv containing input strings",
		G_TYPE_STRV,
		G_PARAM_READABLE | G_PARAM_STATIC_STRINGS );
	g_object_class_install_properties( object_class, N_PROPS, object_props );

	app_class->command_line = edmv_application_command_line;
}

EdmvApplication*
edmv_application_new(
	const gchar *application_id )
{
	g_return_val_if_fail( g_application_id_is_valid( application_id ), NULL );

	return EDMV_APPLICATION( g_object_new( EDMV_TYPE_APPLICATION, "application-id", application_id, "flags", G_APPLICATION_DEFAULT_FLAGS | G_APPLICATION_HANDLES_COMMAND_LINE | G_APPLICATION_NON_UNIQUE, NULL ) );
}

gchar*
edmv_application_get_editor(
	EdmvApplication *self )
{
	g_return_val_if_fail( EDMV_IS_APPLICATION( self ), NULL );

	return g_strdup( self->editor );
}

GStrv
edmv_application_get_inputs(
	EdmvApplication *self )
{
	g_return_val_if_fail( EDMV_IS_APPLICATION( self ), NULL );

	return g_strdupv( self->inputs );
}

