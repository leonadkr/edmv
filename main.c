#include <glib.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#include <locale.h>
#include "config.h"

/*
	definitions
*/
#define CONF_FILE "config"


/*
	structures
*/
struct _InputData
{
	/* input */
	GList *filename_list;

	/* options */
	gchar *editor;
};
typedef struct _InputData InputData;


/*
	private
*/
static gchar*
get_program_summary(
	void )
{
	const gchar *conf_dir;
	gchar *conf_path, *summary;

	conf_dir = g_get_user_config_dir();
	conf_path = g_build_filename( conf_dir, PROGRAM_NAME, CONF_FILE, NULL );

	summary = g_strdup_printf(
		"This program renames FILES with an external editor.\n"
		"Argument EDITOR, option in \'%s\', value of $VISUAL or $EDITOR in this order determines the editor.",
		conf_path );
	g_free( conf_path );

	return summary;
}

static gboolean
parse_input(
	gint argc,
	gchar **argv,
	InputData *input_data )
{
	guint i;
	GList *filename_list;
	gchar *editor, *summary;
	GOptionContext *context;
	gboolean ret = TRUE;
	const GOptionEntry entries[]=
	{
		{ "editor", 'e', G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &editor, "Editor to use", "EDITOR" },
		{ NULL }
	};
	GError *error = NULL;

	g_return_val_if_fail( argc >= 0, FALSE );
	g_return_val_if_fail( argv != NULL, FALSE );
	g_return_val_if_fail( input_data != NULL, FALSE );

	/* parse input */
	editor = NULL;
	summary = get_program_summary();
	context = g_option_context_new( "FILES" );
	g_option_context_set_summary( context, summary );
	g_option_context_add_main_entries( context, entries, NULL );

	if( !g_option_context_parse( context, &argc, &argv, &error ) )
	{
		g_log_structured( G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
			"MESSAGE", error->message,
			NULL );
		g_clear_error( &error );
		ret = FALSE;
		goto out;
	}

	if( argc == 1 )
	{
		g_log_structured( G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
			"MESSAGE", "No input file names",
			NULL );
		ret = FALSE;
		goto out;
	}

	/* get input */
	if( input_data->filename_list == NULL )
	{
		filename_list = NULL;
		for( i = 1; i < argc; ++i )
			filename_list = g_list_prepend( filename_list, g_strdup( argv[i] ) );
		input_data->filename_list = g_list_reverse( filename_list );
	}

	if( input_data->editor == NULL )
		input_data->editor = g_strdup( editor );
	
out:
	g_option_context_free( context );
	g_free( editor );
	g_free( summary );

	return ret;
}

static gboolean
parse_config(
	InputData *input_data )
{
	const gchar *conf_dir;
	gchar *conf_path;
	GKeyFile *key_file;
	gboolean ret = TRUE;

	g_return_val_if_fail( input_data != NULL, FALSE );

	/* load config */
	conf_dir = g_get_user_config_dir();
	conf_path = g_build_filename( conf_dir, PROGRAM_NAME, CONF_FILE, NULL );
	key_file = g_key_file_new();
	if( !g_key_file_load_from_file( key_file, conf_path, G_KEY_FILE_NONE, NULL ) )
	{
		ret = FALSE;
		goto out;
	}
		
	/* get options */
	if( input_data->editor == NULL )
		input_data->editor = g_key_file_get_string( key_file, "Main", "editor", NULL );

out:
	g_free( conf_path );
	g_key_file_free( key_file );
	
	return ret;
}

static gboolean
parse_env(
	InputData *input_data )
{
	gchar *editor;

	g_return_val_if_fail( input_data != NULL, FALSE );

	/* try some environment variables */
	if( input_data->editor == NULL )
	{
		editor = g_strdup( g_getenv( "VISUAL" ) );
		if( editor == NULL )
			editor = g_strdup( g_getenv( "EDITOR" ) );

		input_data->editor = editor;
	}

	return TRUE;
}

static InputData*
get_input_data(
	gint argc,
	char **argv )
{
	InputData *input_data;

	g_return_val_if_fail( argc >=0, NULL );
	g_return_val_if_fail( argv != NULL, NULL );

	input_data = g_new( InputData, 1 );
	input_data->filename_list = NULL;
	input_data->editor = NULL;

	if( !parse_input( argc, argv, input_data ) )
		goto failed;
	parse_config( input_data );
	parse_env( input_data );

	/* can not proceed without an editor */
	if( input_data->editor == NULL )
	{
		g_log_structured( G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
			"MESSAGE", "Editor is not specified",
			NULL );
		goto failed;
	}

	return input_data;

failed:
	g_list_free_full( input_data->filename_list, (GDestroyNotify)g_free );
	g_free( input_data->editor );
	g_free( input_data );

	return NULL;
}

static GFile*
create_temp_file(
	const gchar *dirname )
{
	const gsize count_max = 9999;
	const gchar count_fmt[] = "%04zu";
	const gchar name_sep[] = "-";

	GFile *file;
	gsize count;
	gchar *date_time_str, *count_str, *filename;
	GDateTime *date_time;

	g_return_val_if_fail( dirname != NULL, NULL );

	date_time = g_date_time_new_now_utc();
	date_time_str = g_date_time_format_iso8601( date_time );
	g_date_time_unref( date_time );

	for( count = 0; count < count_max; ++count )
	{
		count_str = g_strdup_printf( count_fmt, count );
		filename = g_strjoin( name_sep, PROGRAM_NAME, date_time_str, count_str, NULL );
		file = g_file_new_build_filename( dirname, filename, NULL );
		g_free( count_str );
		g_free( filename );

		if( g_file_create( file, G_FILE_CREATE_PRIVATE, NULL, NULL ) != NULL )
			break;

		g_clear_object( &file );
	}

	g_free( date_time_str );

	if( count == count_max )
	{
		g_log_structured( G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
			"MESSAGE", "Can not create a temporary file",
			NULL );
		return NULL;
	}

	return file;
}

static gboolean
write_filename_list_to_tmp_file(
	GFile *tmp_file,
	GList *filename_list )
{
	GFileOutputStream *file_output_stream;
	GDataOutputStream *data_output_stream;
	GList *l;
	gchar *s;
	gboolean ret = TRUE;
	GError *error = NULL;

	g_return_val_if_fail( tmp_file != NULL, FALSE );
	g_return_val_if_fail( filename_list != NULL, FALSE );

	file_output_stream = g_file_replace( tmp_file, NULL, FALSE, G_FILE_CREATE_PRIVATE, NULL, &error );
	if( error != NULL )
	{
		g_log_structured( G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
			"MESSAGE", error->message,
			NULL );
		g_clear_error( &error );
		return FALSE;
	}

	data_output_stream = g_data_output_stream_new( G_OUTPUT_STREAM( file_output_stream ) );
	for( l = filename_list; l != NULL; l = l->next )
	{
		s = g_strdup_printf( "%s\n", (gchar*)l->data );
		g_data_output_stream_put_string( data_output_stream, s, NULL, &error );
		g_free( s );
		if( error != NULL )
		{
			g_log_structured( G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
				"MESSAGE", error->message,
				NULL );
			g_clear_error( &error );
			ret = FALSE;
			break;
		}
	}

	g_object_unref( G_OBJECT( data_output_stream ) );
	g_object_unref( G_OBJECT( file_output_stream ) );

	return ret;
}

static gboolean
system_call(
	gchar *editor,
	GFile *tmp_file )
{
	GSubprocess *subproc;
	gchar *filename;
	gboolean ret = TRUE;
	GError *error = NULL;

	g_return_val_if_fail( editor != NULL, FALSE );
	g_return_val_if_fail( tmp_file != NULL, FALSE );

	filename = g_file_get_path( tmp_file );
	subproc = g_subprocess_new(
		G_SUBPROCESS_FLAGS_STDIN_INHERIT,
		&error,
		editor,
		filename,
		NULL );
	g_free( filename );
	if( error != NULL )
	{
		g_log_structured( G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
			"MESSAGE", error->message,
			NULL );
		g_clear_error( &error );
		return FALSE;
	}

	g_subprocess_wait( subproc, NULL, &error );
	if( error != NULL )
	{
		g_log_structured( G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
			"MESSAGE", error->message,
			NULL );
		g_clear_error( &error );
		ret = FALSE;
	}

	g_object_unref( G_OBJECT( subproc ) );

	return ret;
}

static GList*
get_output_filename_list(
	GFile *tmp_file,
	GError **error )
{
	GFileInputStream *file_stream;
	GDataInputStream *data_stream;
	gchar *filename;
	GList *filename_list;
	gsize size;
	GError *local_error = NULL;

	g_return_val_if_fail( tmp_file != NULL, FALSE );
	g_return_val_if_fail( error != NULL, FALSE );

	file_stream = g_file_read( tmp_file, NULL, &local_error );
	if( local_error != NULL )
	{
		g_propagate_error( error, local_error );
		return NULL;
	}

	data_stream = g_data_input_stream_new( G_INPUT_STREAM( file_stream ) );
	filename_list = NULL;
	while( TRUE )
	{
		filename = g_data_input_stream_read_line( data_stream, &size, NULL, &local_error );
		if( local_error != NULL )
		{
			g_propagate_error( error, local_error );
			g_list_free_full( filename_list, (GDestroyNotify)g_free );
			filename_list = NULL;
			break;
		}

		if( filename == NULL )
			break;

		filename_list = g_list_prepend( filename_list, filename );
	}

	g_object_unref( G_OBJECT( data_stream ) );
	g_object_unref( G_OBJECT( file_stream ) );

	return g_list_reverse( filename_list );
}

static gboolean
move_files_by_filename_lists(
	GList *input_filename_list,
	GList *output_filename_list )
{
	GList *l, *il, *ol;
	GList *input_file_list, *output_file_list, *tmp_file_list;
	GFile *input_file, *output_file, *tmp_file, *dir;
	gchar *dirname;
	gboolean ret = TRUE;
	GError *error = NULL;

	g_return_val_if_fail( input_filename_list != NULL, FALSE );
	g_return_val_if_fail( output_filename_list != NULL, FALSE );

	/* prepare file lists */
	input_file_list = NULL;
	output_file_list = NULL;
	tmp_file_list = NULL;
	for( il = input_filename_list, ol = output_filename_list; il != NULL && ol != NULL; il = il->next, ol = ol->next )
	{
		input_file = g_file_new_for_path( (gchar*)il->data );
		output_file = g_file_new_for_path( (gchar*)ol->data );

		/* exclude the same files */
		if( g_file_equal( input_file, output_file ) )
		{
			g_object_unref( G_OBJECT( input_file ) );
			g_object_unref( G_OBJECT( output_file ) );
			continue;
		}

		input_file_list = g_list_prepend( input_file_list, input_file );
		output_file_list = g_list_prepend( output_file_list, output_file );

		/* create temporary file to prevent collisions */
		dir = g_file_get_parent( input_file );
		dirname = g_file_get_path( dir );
		g_object_unref( G_OBJECT( dir ) );
		tmp_file = create_temp_file( dirname );
		g_free( dirname );
		g_object_ref( G_OBJECT( tmp_file ) );/* temporary file will be unrefed by input and ouput file lists */

		tmp_file_list = g_list_prepend( tmp_file_list, tmp_file );
	}
	tmp_file_list = g_list_reverse( tmp_file_list );
	input_file_list = g_list_concat( g_list_reverse( input_file_list ), g_list_copy( tmp_file_list ) );
	output_file_list = g_list_concat( g_list_copy( tmp_file_list ), g_list_reverse( output_file_list ) );

	/* move files */
	for( il = input_file_list, ol = output_file_list; il != NULL && ol != NULL; il = il->next, ol = ol->next )
	{
		input_file = G_FILE( il->data );
		output_file = G_FILE( ol->data );
		g_file_move( input_file, output_file, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error );
		if( error != NULL )
		{
			g_log_structured( G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
				"MESSAGE", error->message,
				NULL );
			g_clear_error( &error );

			/* on error, delete all temporary files */
			for( l = tmp_file_list; l != NULL; l = l->next )
				g_file_delete( G_FILE( l->data ), NULL, NULL );

			ret = FALSE;
			break;
		}
	}

	g_list_free_full( input_file_list, (GDestroyNotify)g_object_unref );
	g_list_free_full( output_file_list, (GDestroyNotify)g_object_unref );
	g_list_free( tmp_file_list );

	return ret;
}

int
main(
	int argc,
	char *argv[] )
{
	InputData *input_data;
	GList *input_filename_list, *output_filename_list;
	gchar *editor;
	GFile *tmp_file;
	guint ret = EXIT_SUCCESS;
	GError *error = NULL;

	/* reset program locale */
	setlocale( LC_ALL, "" );

	/* get input */
	input_data = get_input_data( argc, argv );
	if( input_data == NULL )
		return EXIT_FAILURE;
	input_filename_list = input_data->filename_list;
	editor = input_data->editor;
	g_free( input_data );

	/* create temporary file */
	tmp_file = create_temp_file( g_get_tmp_dir() );
	if( tmp_file == NULL )
	{
		ret = EXIT_FAILURE;
		goto out1;
	}

	/* write input filename list to temporary file */
	if( !write_filename_list_to_tmp_file( tmp_file, input_filename_list ) )
	{
		ret = EXIT_FAILURE;
		goto out2;
	}

	/* do system call */
	if( !system_call( editor, tmp_file ) )
	{
		ret = EXIT_FAILURE;
		goto out2;
	}

	/* get output file list */
	output_filename_list = get_output_filename_list( tmp_file, &error );
	if( error != NULL )
	{
		g_log_structured( G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
			"MESSAGE", error->message,
			NULL );
		g_clear_error( &error );
		ret = EXIT_FAILURE;
		goto out2;
	}

	/* move files */
	if( !move_files_by_filename_lists( input_filename_list, output_filename_list ) )
		ret = EXIT_FAILURE;

	g_list_free_full( output_filename_list, (GDestroyNotify)g_free );

out2:
	g_file_delete( tmp_file, NULL, &error );
	if( error != NULL )
	{
		g_log_structured( G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
			"MESSAGE", error->message,
			NULL );
		g_clear_error( &error );
		ret = EXIT_FAILURE;
	}
	g_object_unref( G_OBJECT( tmp_file ) );

out1:
	g_list_free_full( input_filename_list, (GDestroyNotify)g_free );
	g_free( editor );

	return ret;
}

