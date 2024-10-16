#include <glib.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#include <locale.h>
#include "config.h"
#include "edmvapplication.h"

enum _EdmvError
{
	EDMV_ERROR_CANNOT_CREATE_TMP_FILE
};
typedef enum _EdmvError EdmvError;

G_DEFINE_QUARK( edmv-error-quark, edmv_error )
#define EDMV_ERROR ( edmv_error_quark() )

G_DEFINE_ENUM_TYPE( EdmvError, edmv_error,
	G_DEFINE_ENUM_VALUE( EDMV_ERROR_CANNOT_CREATE_TMP_FILE, "cannot-create-tmp-file" ) )

static GFile*
create_temp_file(
	const gchar *dirpath,
	GError **error )
{
	const gsize count_max = 9999;
	const gchar count_fmt[] = "%04" G_GSIZE_FORMAT;
	const gchar name_sep[] = "-";

	GFile *file;
	GFileOutputStream *file_stream;
	gsize count;
	gchar *date_time_str, *count_str, *filename;
	GDateTime *date_time;

	g_return_val_if_fail( dirpath != NULL, NULL );

	date_time = g_date_time_new_now_utc();
	date_time_str = g_date_time_format( date_time, "%H%M%S" );
	g_date_time_unref( date_time );

	for( count = 0; count < count_max; ++count )
	{
		count_str = g_strdup_printf( count_fmt, count );
		filename = g_strjoin( name_sep, PROGRAM_NAME, date_time_str, count_str, NULL );
		g_free( count_str );
		file = g_file_new_build_filename( dirpath, filename, NULL );
		g_free( filename );

		file_stream = g_file_create( file, G_FILE_CREATE_PRIVATE, NULL, NULL );
		if( file_stream != NULL )
		{
			g_object_unref( G_OBJECT( file_stream ) );
			break;
		}

		g_object_unref( G_OBJECT( file ) );
	}

	g_free( date_time_str );

	if( count == count_max )
	{
		g_set_error( error,
			EDMV_ERROR,
			EDMV_ERROR_CANNOT_CREATE_TMP_FILE,
			"Cannot create a temporary file in directory \'%s\'",
			dirpath );
		return NULL;
	}

	return file;
}

static void
write_filepaths_to_tmp_file(
	GStrv filepaths,
	GFile *tmp_file,
	GError **error )
{
	gchar *s_utf8, *s_locale;
	GError *loc_error = NULL;

	g_return_if_fail( filepaths != NULL );
	g_return_if_fail( G_IS_FILE( tmp_file ) );

	/* join strings and convert UTF-8 to locale */
	s_locale = g_strjoinv( TEXT_FILE_LINE_BREAKER, filepaths );
	s_utf8 = g_strconcat( s_locale, TEXT_FILE_LINE_BREAKER, NULL );
	g_free( s_locale );
	s_locale = g_locale_from_utf8( s_utf8, -1, NULL, NULL, &loc_error );
	g_free( s_utf8 );
	if( loc_error != NULL )
	{
		g_propagate_error( error, loc_error );
		g_free( s_locale );
		return;
	}

	/* place text to the file */
	g_file_replace_contents( tmp_file, s_locale, strlen( s_locale ), NULL, FALSE, G_FILE_CREATE_PRIVATE, NULL, NULL, &loc_error );
	g_free( s_locale );
	if( loc_error != NULL )
	{
		g_propagate_error( error, loc_error );
		return;
	}
}

static void
system_call(
	const gchar *editor,
	GFile *tmp_file,
	GError **error )
{
	GSubprocess *subproc;
	gchar *filepath;
	GError *loc_error = NULL;

	g_return_if_fail( editor != NULL );
	g_return_if_fail( G_IS_FILE( tmp_file ) );

	filepath = g_file_get_path( tmp_file );
	subproc = g_subprocess_new(
		G_SUBPROCESS_FLAGS_STDIN_INHERIT,
		&loc_error,
		editor,
		filepath,
		NULL );
	g_free( filepath );
	if( loc_error != NULL )
	{
		g_propagate_error( error, loc_error );
		return;
	}

	g_subprocess_wait( subproc, NULL, &loc_error );
	if( loc_error != NULL )
	{
		g_propagate_error( error, loc_error );
		g_object_unref( G_OBJECT( subproc ) );
		return;
	}

	g_object_unref( G_OBJECT( subproc ) );
}

static GStrv
get_output_filepaths(
	GFile *tmp_file,
	GError **error )
{
	gchar *s_utf8, *s_locale;
	guint last_idx;
	GStrv filepaths;
	GError *loc_error = NULL;

	g_return_val_if_fail( G_IS_FILE( tmp_file ), NULL );

	/* get file content */
	g_file_load_contents( tmp_file, NULL, &s_locale, NULL, NULL, &loc_error );
	if( loc_error != NULL )
	{
		g_propagate_error( error, loc_error );
		return NULL;
	}

	/* convert encondings */
	if( !g_utf8_validate( s_locale, -1, NULL ) )
	{
		s_utf8 = g_locale_to_utf8( s_locale, -1, NULL, NULL, &loc_error );
		g_free( s_locale );
		if( loc_error != NULL )
		{
			g_propagate_error( error, loc_error );
			return NULL;
		}
	}
	else
		s_utf8 = s_locale;

	/* setup string array */
	filepaths = g_strsplit( s_utf8, TEXT_FILE_LINE_BREAKER, -1 );
	g_free( s_utf8 );
	/* the last string may be zero-length, removing it */
	last_idx = g_strv_length( filepaths ) - 1;
	if( strlen( filepaths[last_idx] ) == 0 )
	{
		g_free( filepaths[last_idx] );
		filepaths[last_idx] = NULL;
	}

	return filepaths;
}

static void
move_files_by_filepaths(
	GStrv input_filepaths,
	GStrv output_filepaths,
	GError **error )
{
	gsize min_len, i;
	gsize files_num, tmped_files_num, moved_files_num;
	GFile *input_file, *output_file, *tmp_file, *dir;
	GFile **input_files, **output_files, **tmp_files;
	gchar *dirpath;
	GError *loc_error = NULL;

	g_return_if_fail( input_filepaths != NULL );
	g_return_if_fail( output_filepaths != NULL );

	/* prepare file arrays */
	min_len = MIN( g_strv_length( input_filepaths ), g_strv_length( output_filepaths ) );
	input_files = g_new( GFile*, min_len );
	output_files = g_new( GFile*, min_len );
	tmp_files = g_new( GFile*, min_len );
	files_num = 0;
	for( i = 0; i < min_len; ++i )
	{
		input_file = g_file_new_for_path( input_filepaths[i] );
		output_file = g_file_new_for_path( output_filepaths[i] );

		/* exclude the same files */
		if( g_file_equal( input_file, output_file ) )
		{
			g_object_unref( G_OBJECT( input_file ) );
			g_object_unref( G_OBJECT( output_file ) );
			continue;
		}

		/* create temporary file to prevent collisions */
		dir = g_file_get_parent( input_file );
		dirpath = g_file_get_path( dir );
		g_object_unref( G_OBJECT( dir ) );
		tmp_file = create_temp_file( dirpath, &loc_error );
		g_free( dirpath );
		if( loc_error != NULL )
		{
			g_propagate_error( error, loc_error );
			g_object_unref( G_OBJECT( input_file ) );
			g_object_unref( G_OBJECT( output_file ) );
			goto out1;
		}

		input_files[files_num] = input_file;
		output_files[files_num] = output_file;
		tmp_files[files_num] = tmp_file;
		files_num++;
	}

	/* move input files to temporary files */
	tmped_files_num = 0;
	for( i = 0; i < files_num; ++i )
	{
		g_file_move( input_files[i], tmp_files[i], G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &loc_error );
		if( loc_error != NULL )
		{
			g_propagate_error( error, loc_error );
			goto out2;
		}

		tmped_files_num++;
	}

	/* move temporary files to output files */
	moved_files_num = 0;
	for( i = 0; i < files_num; ++i )
	{
		dir = g_file_get_parent( output_files[i] );
		g_file_make_directory_with_parents( dir, NULL, &loc_error );
		g_object_unref( G_OBJECT( dir ) );
		/* DIRECTORY EXISTS is not treated as an error */
		if( loc_error != NULL && loc_error->code != G_IO_ERROR_EXISTS )
		{
			g_propagate_error( error, loc_error );
			goto out3;
		}
		g_clear_error( &loc_error );

		g_file_move( tmp_files[i], output_files[i], G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &loc_error );
		if( loc_error != NULL )
		{
			g_propagate_error( error, loc_error );
			goto out3;
		}

		moved_files_num++;
	}

	/* no errors: jump to proceed normaly */
	goto out1;

out3:
	/* on error: try to restore original file names */
	for( i = 0; i < moved_files_num; ++i )
		g_file_move( output_files[i], tmp_files[i], G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL );

out2:
	/* on error: try to restore original file names */
	for( i = 0; i < tmped_files_num; ++i )
		g_file_move( tmp_files[i], input_files[i], G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL );

out1:
	for( i = 0; i < files_num; ++i )
	{
		g_object_unref( G_OBJECT( input_files[i] ) );
		g_object_unref( G_OBJECT( output_files[i] ) );
		g_object_unref( G_OBJECT( tmp_files[i] ) );
	}
	g_free( input_files );
	g_free( output_files );
	g_free( tmp_files );
}

static void
on_app_activate(
	GApplication *app,
	gpointer user_data )
{
	EdmvApplication *self = EDMV_APPLICATION( app );
	gchar *editor;
	GStrv input_filepaths, output_filepaths;
	GFile *tmp_file;
	GError *error = NULL;

	/* cannot proceed with no editor */
	editor = edmv_application_get_editor( self );
	if( editor == NULL )
	{
		g_log_structured( G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
			"MESSAGE", "Editor is not set",
			NULL );
		return;
	}

	/* no inputs -- do nothing */
	input_filepaths = edmv_application_get_inputs( self );
	if( input_filepaths == NULL )
		goto out1;
	if( g_strv_length( input_filepaths ) == 0 )
		goto out2;

	/* create temporary file */
	tmp_file = create_temp_file( g_get_tmp_dir(), &error );
	if( error != NULL )
	{
		g_log_structured( G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
			"MESSAGE", error->message,
			NULL );
		g_clear_error( &error );
		goto out2;
	}

	/* write input filename array to temporary file */
	write_filepaths_to_tmp_file( input_filepaths, tmp_file, &error );
	if( error != NULL )
	{
		g_log_structured( G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
			"MESSAGE", error->message,
			NULL );
		g_clear_error( &error );
		goto out3;
	}

	/* do system call */
	system_call( editor, tmp_file, &error );
	if( error != NULL )
	{
		g_log_structured( G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
			"MESSAGE", error->message,
			NULL );
		g_clear_error( &error );
		goto out3;
	}

	/* get output file array */
	output_filepaths = get_output_filepaths( tmp_file, &error );
	if( error != NULL )
	{
		g_log_structured( G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
			"MESSAGE", error->message,
			NULL );
		g_clear_error( &error );
		goto out3;
	}
	/* no output filepaths -- do nothing */
	if( output_filepaths == NULL )
		goto out3;
	if( g_strv_length( output_filepaths ) == 0 )
		goto out4;

	/* move files */
	move_files_by_filepaths( input_filepaths, output_filepaths, &error );
	if( error != NULL )
	{
		g_log_structured( G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
			"MESSAGE", error->message,
			NULL );
		g_clear_error( &error );
	}

out4:
	g_strfreev( output_filepaths );

out3:
	g_file_delete( tmp_file, NULL, &error );
	if( error != NULL )
	{
		g_log_structured( G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
			"MESSAGE", error->message,
			NULL );
		g_clear_error( &error );
	}
	g_object_unref( G_OBJECT( tmp_file ) );

out2:
	g_strfreev( input_filepaths );

out1:
	g_free( editor );
}

int
main(
	int argc,
	char *argv[] )
{
	EdmvApplication *app;
	gint ret = EXIT_SUCCESS;

	setlocale( LC_ALL, "" );

	app = edmv_application_new( APP_ID );
	if( app == NULL )
		return EXIT_FAILURE;

	g_signal_connect( G_OBJECT( app ), "activate", G_CALLBACK( on_app_activate ), NULL );

	ret = g_application_run( G_APPLICATION( app ), argc, argv );
	g_object_unref( G_OBJECT( app ) );

	return ret;
}

