#include <glib.h>
#include <glib/gi18n.h>
#include <locale.h>
#include "config.h"

/*
	definitions
*/
#define PROGRAM_SUMMARY "This program renames FILES with an external editor"


/*
	structures
*/
struct _InputData
{
	GList *input_file_list;
	gchar *editor_path;
};
typedef struct _InputData InputData;


/*
	private
*/
static gboolean
parse_input(
	gint argc,
	gchar **argv,
	InputData *input_data )
{
	guint i;
	GList *input_file_list;
	gchar *editor_path;
	GOptionContext *context;
	gboolean ret;
	const GOptionEntry entries[]=
	{
		{ "editor", 'e', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &editor_path, "Path to editor", "EDITOR_PATH" },
		{ NULL }
	};
	GError *error = NULL;

	g_return_val_if_fail( argc >= 0, FALSE );
	g_return_val_if_fail( argv != NULL, FALSE );

	if( input_data->input_file_list != NULL && input_data->editor_path != NULL )
		return FALSE;

	context = g_option_context_new( "FILES" );
	g_option_context_set_summary( context, PROGRAM_SUMMARY );
	g_option_context_add_main_entries( context, entries, NULL );
	ret = TRUE;
	editor_path = NULL;
	do
	{
		if( !g_option_context_parse( context, &argc, &argv, &error ) )
		{
			g_log_structured( G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
				"MESSAGE", error->message,
				NULL );
			g_error_free( error );
			ret = FALSE;
			break;
		}

		if( argc == 1 )
		{
			g_log_structured( G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
				"MESSAGE", "No input file names",
				NULL );
			ret = FALSE;
			break;
		}
	} while( 0 );
	g_option_context_free( context );

	if( !ret )
	{
		g_free( editor_path );
		return ret;
	}

	if( input_data->input_file_list == NULL )
	{
		input_file_list = NULL;
		for( i = 1; i < argc; ++i )
			input_file_list = g_list_append( input_file_list, g_strdup( argv[i] ) );
		input_data->input_file_list = input_file_list;
	}

	if( input_data->editor_path == NULL )
		input_data->editor_path = editor_path;
	else
		g_free( editor_path );

	return ret;
}

static gboolean
parse_config(
	InputData *input_data )
{
	const gchar separ[] = "/";
	const gchar conf_file[] = PROGRAM_NAME"/config";

	const gchar *conf_dir;
	gchar *conf_path, *editor_path;
	GKeyFile *key_file;
	gboolean ret;

	g_return_val_if_fail( input_data != NULL, FALSE );

	if( input_data->editor_path != NULL )
		return FALSE;

	conf_dir = g_get_user_config_dir();
	conf_path = g_strjoin( separ, conf_dir, conf_file, NULL );
	key_file = g_key_file_new();
	ret = TRUE;
	do
	{
		if( !g_key_file_load_from_file( key_file, conf_path, G_KEY_FILE_NONE, NULL ) )
		{
			ret = FALSE;
			break;
		}
		
		editor_path = g_key_file_get_string( key_file, "Main", "editor", NULL );
		if( editor_path == NULL )
			ret = FALSE;
	} while( 0 );
	g_free( conf_path );
	g_key_file_free( key_file );

	if( !ret )
	{
		g_free( editor_path );
		return ret;
	}

	if( input_data->editor_path == NULL )
		input_data->editor_path = editor_path;
	else
		g_free( editor_path );
	
	return ret;
}

static gboolean
parse_env(
	InputData *input_data )
{
	gchar *editor_path;
	gboolean ret;

	g_return_val_if_fail( input_data != NULL, FALSE );

	if( input_data->editor_path != NULL )
		return FALSE;

	/* try some environment variables */
	editor_path = g_strdup( g_getenv( "VISUAL" ) );
	if( editor_path == NULL )
		editor_path = g_strdup( g_getenv( "EDITOR" ) );

	ret = FALSE;
	if( editor_path != NULL )
	{
		input_data->editor_path = editor_path;
		ret = TRUE;
	}

	return ret;
}

static InputData*
get_input_data(
	gint argc,
	char **argv )
{
	InputData *input_data;
	gboolean stat;

	g_return_val_if_fail( argc >=0, NULL );
	g_return_val_if_fail( argv != NULL, NULL );

	input_data = g_new( InputData, 1 );
	input_data->input_file_list = NULL;
	input_data->editor_path = NULL;

	stat = TRUE;
	do
	{
		if( !parse_input( argc, argv, input_data ) )
		{
			stat = FALSE;
			break;
		}
		parse_config( input_data );
		parse_env( input_data );

		if( input_data->editor_path == NULL )
		{
			g_log_structured( G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
				"MESSAGE", "Can not get any editor",
				NULL );
			stat = FALSE;
			break;
		}
	} while( 0 );

	if( !stat )
	{
		g_list_free_full( input_data->input_file_list, (GDestroyNotify)g_free );
		g_free( input_data->editor_path );
		g_free( input_data );
		input_data = NULL;
	}

	return input_data;
}

int
main(
	int argc,
	char *argv[] )
{
	GList *l;
	InputData *input_data;
	GList *input_file_list;
	gchar *editor_path;

	/* reset program locale */
	setlocale( LC_ALL, "" );

	/* get input */
	input_data = get_input_data( argc, argv );
	if( input_data == NULL )
		return EXIT_FAILURE;
	input_file_list = input_data->input_file_list;
	editor_path = input_data->editor_path;
	g_free( input_data );

	for( l = input_file_list; l != NULL; l = l->next )
		g_print( "%s\n", (gchar*)l->data );
	g_print( "%s\n", editor_path );

	g_list_free_full( input_file_list, (GDestroyNotify)g_free );
	g_free( editor_path );

	return EXIT_SUCCESS;
}

