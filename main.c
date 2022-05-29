#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <argp.h>
#include <assert.h>

#include "config.h"


/*
	definitions
*/


/*
	structures
*/
struct _argp_arguments
{
	char *editor;
	char **filenames;
	size_t filenames_num;
};
typedef struct _argp_arguments argp_arguments;

struct _program_configs
{
	char *editor;
};
typedef struct _program_configs program_configs;

struct _program_envs
{
	char *editor;
	char *tempdir;
};
typedef struct _program_envs program_envs;


/*
	private
*/
static void
print_errno(
	void )
{
	int errsv;
	char *strerr;

	errsv = errno;
	strerr = strerror( errsv );
	fprintf( stderr, "Error: %s\n", strerr );
}

static error_t
argp_parse_options(
	int key,
	char *arg,
	struct argp_state *state )
{
	argp_arguments *args = state->input;
	size_t arg_len;

	switch( key )
	{
		case (int)'e':
			arg_len = strlen( arg );
			args->editor = (char*)malloc( arg_len + 1 );
			memcpy( args->editor, arg, arg_len + 1 );
			break;

		case ARGP_KEY_END:
			if( state->arg_num < 1 )
				argp_usage( state );
			break;

		case ARGP_KEY_ARG:
			args->filenames[args->filenames_num] = arg;
			args->filenames_num++;
			break;

		default:
			return ARGP_ERR_UNKNOWN;
	}
	
	return 0;
}

static bool
parse_arguments(
	int argc,
	char *argv[],
	argp_arguments *args )
{
	const char *argp_option_doc = "INPUTFILES";
	const char *argp_program_doc = "ABOUT PROGRAM";
	const struct argp_option argp_options[] = {
		{ "editor", (int)'e', "EDITORPATH", 0, "Set path to editor", 0 },
		{ 0 }
	};
	const struct argp argp = (struct argp){
		argp_options,
		argp_parse_options,
		argp_option_doc,
		argp_program_doc
	};

	error_t argp_parse_error;

	assert( argc >= 0 );
	assert( argv != NULL );
	assert( args != NULL );

	args->editor = NULL;
	args->filenames = (char**)malloc( argc * sizeof( char* ) );
	args->filenames_num = 0;

	argp_parse_error = argp_parse( &argp, argc, argv, ARGP_NO_EXIT, 0, args );
	if( argp_parse_error != 0 || args->filenames_num == 0 )
	{
		free( args->filenames );
		args->filenames = NULL;
		args->filenames_num = 0;
		return false;
	}

	args->filenames = (char**)realloc( args->filenames, args->filenames_num * sizeof( char* ) );

	return true;
}

static char*
get_config_file(
	void )
{
	const char def_prefix[] = "/.config";
	const char postfix[] = "/"PROGRAM_NAME"/config";

	char *val;
	char *prefix, *conf;
	size_t val_len, prefix_len, postfix_len, conf_len;

	val = getenv( "XDG_CONFIG_HOME" );
	if( val != NULL )
	{
		prefix_len = strlen( val );
		prefix = (char*)malloc( prefix_len + 1 );
		memcpy( prefix, val, prefix_len + 1 );
	}
	else
	{
		val = getenv( "HOME" );
		val_len = strlen( val );
		prefix_len = val_len + sizeof( def_prefix ) - 1;
		prefix = (char*)malloc( prefix_len + 1 );
		memcpy( prefix, val, val_len );
		memcpy( prefix + val_len, def_prefix, sizeof( def_prefix ) );
	}

	postfix_len = strlen( postfix );
	conf_len = prefix_len + postfix_len;

	conf = (char*)malloc( conf_len + 1 );
	memcpy( conf, prefix, prefix_len );
	memcpy( conf + prefix_len, postfix, postfix_len + 1 );
	free( prefix );

	return conf;
}

static char*
get_param_value_from_line(
	const char *param,
	char *line )
{
	const char spaces[] = " \t";
	const int comment = (int)'#';
	const int equal = (int)'=';

	char *s, *ch, *value;
	size_t param_len, line_len, value_len;
	bool only_spaces;

	assert( param != NULL );
	assert( line != NULL );

	param_len = strlen( param );
	line_len = strlen( line );

	/* check whether line is a comment */
	ch = line;
	s = (char*)memchr( ch, comment, line_len );
	if( s != NULL )
	{
		only_spaces = true;
		while( ch < s )
		{
			if( memchr( spaces, (int)ch[0], sizeof( spaces ) ) == NULL )
				only_spaces = false;
			ch++;
		}
		if( only_spaces )
			return NULL;
	}

	/* scan for param */
	s = (char*)memmem( line, line_len, param, param_len );
	if( s == NULL )
		return NULL;
	/* get value, ignore spaces */
	ch = (char*)memchr( line, equal, line_len );
	while( ch[0] != (int)'\0' )
	{
		ch++;
		if( memchr( spaces, (int)ch[0], sizeof( spaces ) ) == NULL )
			break;
	}
	s = line + line_len;
	while( s > ch )
	{
		s--;
		if( memchr( spaces, (int)s[0], sizeof( spaces ) ) == NULL )
		{
			value_len = s - ch;
			value = (char*)malloc( value_len + 1 );
			memcpy( value, ch, value_len + 1 );
			return value;
		}
	}

	return NULL;
}

static bool
parse_config_file(
	program_configs *configs )
{
	char *line;
	char *conf_path;
	FILE *conf;
	ssize_t line_len;
	size_t line_size = 0;

	assert( configs != NULL );

	conf_path = get_config_file();
	conf = fopen( conf_path, "r" );
	free( conf_path );
	if( conf == NULL )
	{
		configs->editor = NULL;
		return false;
	}

	configs->editor = NULL;
	line = NULL;
	while( ( line_len = getline( &line, &line_size, conf ) ) != -1 )
	{
		line[line_len-1] = '\0';

		if( configs->editor == NULL )
			configs->editor = get_param_value_from_line( "editor", line );

		free( line );
		line = NULL;
	}
	
	fclose( conf );

	return true;
}

static bool
parse_enviroment(
	program_envs *envs )
{
	char *ed_envs[] =
	{
		"VISUAL",
		"EDITOR",
		NULL
	};

	char def_tmpdir[] = "/tmp";
	char *tmpdir_envs[] =
	{
		"TMPDIR",
		"TMP",
		"TEMP",
		"TEMPDIR",
		NULL
	};

	int i;
	char *env, *val;
	char *ed, *dir;
	size_t ed_len, dir_len;

	assert( envs != NULL );

	/* check environment variables for editor */
	ed = NULL;
	i = 0;
	while( ( env = ed_envs[i++] ) != NULL )
	{
		val = getenv( env );
		if( val != NULL )
		{
			ed_len = strlen( val );
			ed = (char*)malloc( ed_len + 1 );
			memcpy( ed, val, ed_len + 1 );
			break;
		}
	}
	envs->editor = ed;
	/* can not continue with no editor */
	if( ed == NULL )
		return false;

	/* check environment variables for temporary directory */
	dir = NULL;
	i = 0;
	while( ( env = tmpdir_envs[i++] ) != NULL )
	{
		val = getenv( env );
		if( val != NULL )
		{
			dir_len = strlen( val );
			dir = (char*)malloc( dir_len + 1 );
			memcpy( dir, val, dir_len + 1 );
			break;
		}
	}
	/* or use default temporary direcory */
	if( dir == NULL )
	{
		dir_len = strlen( def_tmpdir );
		dir = (char*)malloc( dir_len + 1 );
		memcpy( dir, def_tmpdir, dir_len + 1 );
	}
	envs->tempdir = dir;

	return true;
}
	

static char*
get_temp_file_path(
	const char *dir )
{
	const char file[] = PROGRAM_NAME"-XXXXXX";

	char *path;
	size_t file_len, dir_len, path_len;

	if( dir == NULL )
		return NULL;

	file_len = strlen( file );
	dir_len = strlen( dir );
	path_len = dir_len + file_len + 1; /* +1 for '/' */

	/* concatenate temporary directory and temporary file with '/' between */
	path = (char*)malloc( path_len + 1 );
	memcpy( path, dir, dir_len );
	path[dir_len] = '/';
	memcpy( path + dir_len + 1, file, file_len + 1 );

	return path;
}

static bool
system_call(
	char *syscall )
{
	if( syscall == NULL )
	{
		fprintf( stderr, "Error: command line is NULL\n" );
		return false;
	}

	if( system( NULL ) == 0 )
	{
		fprintf( stderr, "Error: shell is not available\n" );
		return false;
	}

	if( system( syscall ) == -1 )
	{
		print_errno();
		return false;
	}

	return true;
}

int
main(
	int argc,
	char *argv[] )
{
	int tmpf_id;
	FILE *tmpf;
	size_t i, line_size = 0;
	ssize_t line_len;
	char *tmpf_name, *editor, *syscall, *line;
	size_t 	syscall_len;
char **input_filenames, **output_filenames;
	size_t input_filenames_num, output_filenames_num;
	argp_arguments args;
	program_envs envs;
	program_configs configs;

	/* parsing */
	if( !parse_arguments( argc, argv, &args ) )
		return EXIT_FAILURE;
	parse_enviroment( &envs );
	parse_config_file( &configs );

	/* get input files */
	input_filenames_num = args.filenames_num;
	input_filenames = args.filenames;

	/* get editor */
	if( args.editor != NULL )
	{
		editor = args.editor;
		free( configs.editor );
		free( envs.editor );
	}
	else if( configs.editor != NULL )
	{
		editor = configs.editor;
		free( envs.editor );
	}
	else if( envs.editor != NULL )
		editor = envs.editor;
	else
	{
		fprintf( stderr, "Error: can not get any editor\n" );
		return EXIT_FAILURE;
	}

	/* get temporary file name and create it */
	tmpf_name = get_temp_file_path( envs.tempdir );
	free( envs.tempdir );
	tmpf_id = mkstemp( tmpf_name );
	if( tmpf_id == -1 )
	{
		print_errno();
		free( tmpf_name );
		free( editor );
		return EXIT_FAILURE;
	}
	tmpf = fdopen( tmpf_id, "w" );

	/* print input filenames to temporary file */
	for( i = 0; i < input_filenames_num; ++i )
		fprintf( tmpf, "%s\n", input_filenames[i] );
	fclose( tmpf );

	/* generate system call */
	syscall_len = snprintf( NULL, 0, "\"%s\" \"%s\"", editor, tmpf_name );
	syscall = malloc( syscall_len + 1 );
	snprintf( syscall, syscall_len + 1, "\"%s\" \"%s\"", editor, tmpf_name );
	free( editor );
	
	/* try to call */
	if( !system_call( syscall ) )
	{
		free( syscall );
		free( input_filenames );
		return EXIT_FAILURE;
	}
	free( syscall );

	/* read from temporary file to output filenames */
	tmpf = fopen( tmpf_name, "r" );
	output_filenames = (char**)malloc( input_filenames_num * sizeof( char* ) );
	output_filenames_num = 0;
	line = NULL;
	while( ( line_len = getline( &line, &line_size, tmpf ) ) != -1 && output_filenames_num < input_filenames_num )
	{
		line[line_len-1] = '\0';
		output_filenames[output_filenames_num++] = line;
		line = NULL;
	}
	if( output_filenames_num != input_filenames_num )
		output_filenames = (char**)realloc( output_filenames, output_filenames_num * sizeof(char*) );
	fclose( tmpf );
	if( remove( tmpf_name ) == -1 )
		print_errno();
	free( tmpf_name );

	/* compare input filenames and output filenames */
	for( i = 0; i < output_filenames_num; ++i )
	{
		line_size = strlen( input_filenames[i] );
		if( line_size == strlen( output_filenames[i] )
			&& memcmp( input_filenames[i], output_filenames[i], line_size ) == 0 )
		{
			free( output_filenames[i] );
			output_filenames[i] = NULL;
		}
	}

	/* rename files */
	for( i = 0; i < output_filenames_num; ++i )
		if( output_filenames[i] != NULL )
			if( rename( input_filenames[i], output_filenames[i] ) == -1 )
				print_errno();

	/* release memory */
	free( input_filenames );
	for( i = 0; i < output_filenames_num; ++i )
		free( output_filenames[i] );
	free( output_filenames );

	return EXIT_SUCCESS;
}

