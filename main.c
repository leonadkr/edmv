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
	gchar **filenames;
	gsize filenames_num;

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
		"Argument EDITOR, option in \'%s\', value of $VISUAL or $EDITOR in this order determine the editor.",
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
	gsize i;
	gchar **filenames;
	gsize filenames_num;
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
	if( input_data->filenames_num == 0 )
	{
		filenames_num = argc - 1;
		filenames = g_new( gchar*, filenames_num );
		for( i = 1; i < argc; ++i )
			filenames[i-1] = g_strdup( argv[i] );

		input_data->filenames = filenames;
		input_data->filenames_num = filenames_num;
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
	gsize i;
	InputData *input_data;

	g_return_val_if_fail( argc >=0, NULL );
	g_return_val_if_fail( argv != NULL, NULL );

	input_data = g_new( InputData, 1 );
	input_data->filenames = NULL;
	input_data->filenames_num = 0;
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
	for( i = 0; i < input_data->filenames_num; ++i )
		g_free( input_data->filenames[i] );
	g_free( input_data->filenames );
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
write_filenames_to_tmp_file(
	GFile *tmp_file,
	gchar **filenames,
	gsize filenames_num )
{
	GFileOutputStream *file_output_stream;
	GDataOutputStream *data_output_stream;
	gsize i;
	gchar *s;
	gboolean ret = TRUE;
	GError *error = NULL;

	g_return_val_if_fail( tmp_file != NULL, FALSE );
	g_return_val_if_fail( filenames != NULL, FALSE );
	g_return_val_if_fail( filenames_num >= 0, FALSE );

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
	for( i = 0; i < filenames_num; ++i )
	{
		s = g_strdup_printf( "%s\n", filenames[i] );
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

static gchar**
get_output_filenames(
	GFile *tmp_file,
	gsize input_filenames_num,
	gsize *output_filenames_num,
	GError **error )
{
	gsize i;
	GFileInputStream *file_stream;
	GDataInputStream *data_stream;
	gchar *filename;
	gchar **filenames;
	gsize filenames_num, size;
	GError *local_error = NULL;

	g_return_val_if_fail( tmp_file != NULL, FALSE );
	g_return_val_if_fail( input_filenames_num >= 0, FALSE );
	g_return_val_if_fail( output_filenames_num != NULL, FALSE );
	g_return_val_if_fail( error != NULL, FALSE );

	file_stream = g_file_read( tmp_file, NULL, &local_error );
	if( local_error != NULL )
	{
		g_propagate_error( error, local_error );
		return NULL;
	}

	data_stream = g_data_input_stream_new( G_INPUT_STREAM( file_stream ) );
	filenames = g_new( gchar*, input_filenames_num );
	filenames_num = 0;
	while( filenames_num <= input_filenames_num )
	{
		filename = g_data_input_stream_read_line( data_stream, &size, NULL, &local_error );
		if( local_error != NULL )
		{
			g_propagate_error( error, local_error );

			for( i = 0; i < filenames_num; ++i )
				g_free( filenames[i] );
			g_free( filenames );
			filenames = NULL;
			filenames_num = 0;
			break;
		}

		if( filename == NULL )
			break;

		filenames[filenames_num++] = filename;
	}

	g_object_unref( G_OBJECT( data_stream ) );
	g_object_unref( G_OBJECT( file_stream ) );

	*output_filenames_num = filenames_num;
	return filenames;
}

static gboolean
move_files_by_filenames(
	gchar **input_filenames,
	gsize input_filenames_num,
	gchar **output_filenames,
	gsize output_filenames_num )
{
	gsize i, filenames_num, files_num;
	GFile *input_file, *output_file, *tmp_file;
	GFile **input_files, **output_files, **tmp_files, *dir;
	gchar *dirname;
	gboolean ret = TRUE;
	GError *error = NULL;

	g_return_val_if_fail( input_filenames != NULL, FALSE );
	g_return_val_if_fail( input_filenames_num >= 0, FALSE );
	g_return_val_if_fail( output_filenames != NULL, FALSE );
	g_return_val_if_fail( output_filenames_num >= 0, FALSE );

	filenames_num = MIN( input_filenames_num, output_filenames_num );

	/* prepare file arrays */
	input_files = g_new( GFile*, filenames_num );
	output_files = g_new( GFile*, filenames_num );
	tmp_files = g_new( GFile*, filenames_num );
	files_num = 0;
	for( i = 0; i < filenames_num; ++i )
	{
		input_file = g_file_new_for_path( input_filenames[i] );
		output_file = g_file_new_for_path( output_filenames[i] );

		/* exclude the same files */
		if( g_file_equal( input_file, output_file ) )
		{
			g_object_unref( G_OBJECT( input_file ) );
			g_object_unref( G_OBJECT( output_file ) );
			continue;
		}

		input_files[files_num] = input_file;
		output_files[files_num] = output_file;

		/* create temporary file to prevent collisions */
		dir = g_file_get_parent( input_file );
		dirname = g_file_get_path( dir );
		g_object_unref( G_OBJECT( dir ) );
		tmp_file = create_temp_file( dirname );
		g_free( dirname );
		tmp_files[files_num] = tmp_file;

		files_num++;
	}

	/* move files */
	for( i = 0; i < files_num; ++i )
	{
		g_file_move( input_files[i], tmp_files[i], G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error );
		if( error != NULL )
		{
			g_log_structured( G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
				"MESSAGE", error->message,
				NULL );
			g_clear_error( &error );
			ret = FALSE;
			goto out;
		}
	}
	for( i = 0; i < files_num; ++i )
	{
		dir = g_file_get_parent( output_files[i] );
		g_file_make_directory_with_parents( dir, NULL, &error );
		g_object_unref( G_OBJECT( dir ) );
		if( error != NULL && error->code != G_IO_ERROR_EXISTS )
		{
			g_log_structured( G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
				"MESSAGE", error->message,
				NULL );
			g_clear_error( &error );
			ret = FALSE;
			break;
		}
		if( error != NULL && error->code == G_IO_ERROR_EXISTS )
			g_clear_error( &error );

		g_file_move( tmp_files[i], output_files[i], G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error );
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

out:
	/* on error: delete all temporary files */
	if( !ret )
			for( i = 0; i < files_num; ++i )
				g_file_delete( tmp_files[i], NULL, NULL );

	for( i = 0; i < files_num; ++i )
	{
		g_object_unref( G_OBJECT( input_files[i] ) );
		g_object_unref( G_OBJECT( output_files[i] ) );
		g_object_unref( G_OBJECT( tmp_files[i] ) );
	}
	g_free( input_files );
	g_free( output_files );
	g_free( tmp_files );

	return ret;
}

int
main(
	int argc,
	char *argv[] )
{
	gsize i;
	InputData *input_data;
	gchar **input_filenames, **output_filenames;
	gsize input_filenames_num, output_filenames_num;
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
	input_filenames = input_data->filenames;
	input_filenames_num = input_data->filenames_num;
	editor = input_data->editor;
	g_free( input_data );

	/* create temporary file */
	tmp_file = create_temp_file( g_get_tmp_dir() );
	if( tmp_file == NULL )
	{
		ret = EXIT_FAILURE;
		goto out1;
	}

	/* write input filename array to temporary file */
	if( !write_filenames_to_tmp_file( tmp_file, input_filenames, input_filenames_num ) )
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

	/* get output file array */
	output_filenames_num = 0;
	output_filenames = get_output_filenames( tmp_file, input_filenames_num, &output_filenames_num, &error );
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
	if( !move_files_by_filenames( input_filenames, input_filenames_num, output_filenames, output_filenames_num ) )
		ret = EXIT_FAILURE;

	for( i = 0; i < output_filenames_num; ++i )
		g_free( output_filenames[i] );
	g_free( output_filenames );

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
	for( i = 0; i < input_filenames_num; ++i )
		g_free( input_filenames[i] );
	g_free( input_filenames );
	g_free( editor );

	return ret;
}

