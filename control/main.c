/*
 * mnxcontrol -- Remote control utilities for UNIX, specifically Minix
 * Copyright (c) 2009 Marten Klencke
 * 
 * Distributed under an MIT license, See COPYING for details.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define PORT_DEFAULT 1408
#define BUFSIZE 4096

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

typedef enum _control_mode { MODE_NONE, MODE_SERVER, MODE_PUTFILE, MODE_LISTDIR, MODE_GETFILE, MODE_DELETEFILE, MODE_SHELLCOMMAND } control_mode;

#define NUM_COMMANDS 6
typedef enum _control_command { COMMAND_NONE, COMMAND_PUTFILE, COMMAND_LISTDIR, COMMAND_GETFILE, COMMAND_DELETEFILE, COMMAND_SHELLCOMMAND } control_command;

typedef enum _control_result { RESULT_OK, RESULT_COMMAND_TOO_LONG, RESULT_COMMAND_ERROR, RESULT_PATH_TOO_LONG, RESULT_COULD_NOT_CREATE_DIR, RESULT_COULD_NOT_CREATE_FILE, RESULT_ERROR } control_result;

#define MODE_TO_COMMAND( mode ) (control_command)( mode - 1 )

control_mode mode;
control_command command;
int port = PORT_DEFAULT;
char *host = NULL;
char *filename;

void usage( char *cmd )
{
	printf( "Usage: %s (server|shellcommand|putfile) (<host>) (<filename>) (<port>)\n", cmd );
	printf( "Examples:\n" );
	printf( "%s server\n", cmd );
	printf( "%s shellcommand minixhost\n", cmd );
	printf( "%s shellcommand minixhost 1408\n", cmd );
	printf( "%s putfile minixhost \"foo/bar.txt\"\n", cmd );
	printf( "%s putfile minixhost \"foo/bar.txt\" 1408\n", cmd );
}

int parse_cmdline( int argc, char *argv[] )
{
	char *port_str = NULL;

	if ( argc < 2 )
		return FALSE;
	
	if ( strcmp( argv[1], "server" ) == 0 )
		mode = MODE_SERVER;

	if ( strcmp( argv[1], "shellcommand" ) == 0 )
		mode = MODE_SHELLCOMMAND;

	if ( strcmp( argv[1], "putfile" ) == 0 )
		mode = MODE_PUTFILE;

	if ( mode == MODE_NONE )
		return FALSE;
	
	if ( mode == MODE_SERVER ) {
		if ( argc != 2 )
			return FALSE;
	}

	if ( mode == MODE_SHELLCOMMAND ) {
		if ( ( argc < 3 ) || ( argc > 4 ) )
			return FALSE;

		host = argv[2];

		if ( argc == 4 )
			port_str = argv[3];
	}

	if ( mode == MODE_PUTFILE ) {
		if ( ( argc < 4 ) || ( argc > 5 ) )
			return FALSE;

		host = argv[2];
		filename = argv[3];

		if ( argc == 5 )
			port_str = argv[4];
	}

	if ( port_str != NULL ) {
		port = atoi( port_str );
		if ( port <= 0 ) {
			printf( "Invalid port number!\n" );
			return FALSE;
		}
	}

	return TRUE;
}

void passthru_file( int source, int destination, int seek )
{
	char buf[BUFSIZE];
	ssize_t len;

	if ( seek )
		lseek( source, 0, SEEK_SET );

	while ( ( len = read( source, buf, BUFSIZE ) ) ) {
		write( destination, buf, len );
	}
}

int read_string( int sock, char *buf, ssize_t maxlen )
{
	ssize_t len;
	ssize_t left = maxlen;

	while ( ( len = read( sock, buf, 1 ) ) )
	{
		if ( *buf == '\0' )
			return TRUE;
		
		left--;
		buf++;
	}

	/* Have we not yet reached EOF or \0? */
	if ( read( sock, buf, 1 ) != 0 )
		return FALSE;
	
	*buf = '\0';

	return TRUE;
}

int process_shellcommand( int sock )
{
	char buf[BUFSIZE];
	char tempfile_name[64] = "/tmp/MNXCTRL_SHELL_XXXXXX";
	ssize_t len;
	control_result result;
	int exit_status = -1;
	int tmp_fd;

	if ( ! read_string( sock, buf, BUFSIZE ) ) {
		result = RESULT_COMMAND_TOO_LONG;
		write( sock, &result, sizeof( result ) );
		return FALSE;
	}

	if ( result == RESULT_OK ) {
		FILE *p;
		p = popen( buf, "r" );
		if ( ( p == NULL ) || ( p == (FILE *)-1 ) ) {
			result = RESULT_COMMAND_ERROR;
			write( sock, &result, sizeof( result ) );
			return FALSE;
		} else {
			result = RESULT_OK;
			write( sock, &result, sizeof( result ) );

			tmp_fd = mkstemp( tempfile_name );

			while ( ( len = fread( buf, 1, BUFSIZE, p ) ) ) {
				write( tmp_fd, buf, len );
			}

			exit_status = pclose( p );
			printf( "exit status: %d\n", exit_status );
			write( sock, &exit_status, sizeof( exit_status ) );

			passthru_file( tmp_fd, sock, TRUE );

			close( tmp_fd );
			unlink( tempfile_name );
		}
	}

	return TRUE;
}

int process_putfile( int sock )
{
	char buf[BUFSIZE];
	char *filesep;
	char mkdir_cmd[BUFSIZE + 32]; /* enough space to put 'mkdir -p "<path>"' */
	control_result result;
	int retval;
	int fd;

	if ( ! read_string( sock, buf, BUFSIZE ) ) {
		result = RESULT_PATH_TOO_LONG;
		write( sock, &result, sizeof( result ) );
		return FALSE;
	}

	/* split path and filename */
	filesep = strrchr( buf, '/' );
	if ( filesep != NULL ) {
		/* make sure the directory exists */

		*filesep = '\0';

		sprintf( mkdir_cmd, "mkdir -p \"%s\"", buf );
		retval = system( mkdir_cmd );
		if ( retval != 0 ) {
			result = RESULT_COULD_NOT_CREATE_DIR;
			write( sock, &result, sizeof( result ) );
			return FALSE;
		}

		/* put path/file separator back so we can open the file */
		*filesep = '/';
	}

	fd = creat( buf, S_IRWXU );
	if ( fd == -1 ) {
		result = RESULT_COULD_NOT_CREATE_FILE;
		write( sock, &result, sizeof( result ) );
		return TRUE;
	}

	result = RESULT_OK;
	write( sock, &result, sizeof( result ) );

	passthru_file( sock, fd, FALSE );

	return TRUE;
}

int process_command( int sock )
{
	control_command cmd;
	read( sock, &cmd, sizeof( cmd ) );

	if ( cmd == COMMAND_SHELLCOMMAND )
		return process_shellcommand( sock );

	if ( cmd == COMMAND_PUTFILE )
		return process_putfile( sock );
	
	return FALSE;
}

int server( void )
{
	int sock, recv_sock;
	int result;
	struct sockaddr_in addr;
#ifndef _MINIX
	int yes;
#endif

	sock = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );

#ifndef _MINIX
	yes = 1;
	result = setsockopt( sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof( int ) );
	if ( result != 0 ) {
		printf( "Could not set SO_REUSEADDR on socket.\n" );
		return FALSE;
	}
#endif

	if ( sock == -1 ) {
		printf( "Could not create socket\n" );
		return FALSE;
	}

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl( INADDR_ANY );
	addr.sin_port = htons( port );

	if ( bind( sock, (struct sockaddr *)&addr, sizeof( struct sockaddr_in ) ) == -1 ) {
		printf( "Unable to bind: %s\n", strerror( errno ) );
		return FALSE;
	}

	if ( listen( sock, 1 ) == -1 ) {
		printf( "Unable to listen: %s\n", strerror( errno ) );
		return FALSE;
	}

	while ( ( recv_sock = accept( sock, NULL, NULL ) ) != -1 ) {
		process_command( recv_sock );
		close( recv_sock );
	}

	close( sock );

	printf( "Error while accepting connection: %s\n", strerror( errno ) );
	return FALSE;
}

int shellcommand( int sock )
{
	char buf[BUFSIZE];
	ssize_t len;
	control_result result;
	int exit_status;

	while ( ( len = read( STDIN_FILENO, buf, BUFSIZE ) ) ) {
		write( sock, buf, len );
	}

	shutdown( sock, SHUT_WR );

	read( sock, &result, sizeof( result ) );
	if ( result != RESULT_OK ) {
		printf( "Error reading from server: %d\n", result );
		return EXIT_FAILURE;
	}

	read( sock, &exit_status, sizeof( exit_status ) );
	printf( "Exit status: %d\n", exit_status );

	while ( ( len = read( sock, buf, BUFSIZE ) ) ) {
		write( STDOUT_FILENO, buf, len );
	}

	return exit_status;
}

int putfile( int sock )
{
	control_result result;

	write( sock, filename, strlen( filename ) + 1 );

	read( sock, &result, sizeof( result ) );
	if ( result != RESULT_OK ) {
		printf( "Error creating file on server: %d\n", result );
		return EXIT_FAILURE;
	}

	printf( "yeah\n" );

	passthru_file( STDIN_FILENO, sock, FALSE );
	
	return EXIT_SUCCESS;
}

void send_command( int sock, control_mode mode )
{
	control_command cmd = MODE_TO_COMMAND( mode );

	write( sock, (void *)&cmd, sizeof( control_command ) );
}

int client( control_mode mode )
{
	int sock;
	struct hostent *host_info;
	struct sockaddr_in addr;
	int result;

	sock = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );

	if ( sock == -1 ) {
		printf( "Could not create socket\n" );
		return FALSE;
	}

	host_info = gethostbyname( host );
	if ( host_info == NULL ) {
		printf( "Could not find host %s\n", host );
		return FALSE;
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons( port );
	memcpy( &(addr.sin_addr.s_addr), host_info->h_addr, host_info->h_length );
	
	result = connect( sock, (struct sockaddr *)&addr, sizeof( struct sockaddr_in ) );
	if ( result == -1 ) {
		printf( "Unable to connect: %s\n", strerror( errno ) );
		return FALSE;
	}

	send_command( sock, mode );

	if ( mode == MODE_SHELLCOMMAND )
		return shellcommand( sock );

	if ( mode == MODE_PUTFILE )
		return putfile( sock );

	close( sock );

	return EXIT_SUCCESS;
}

int main( int argc, char *argv[] )
{
	int result = FALSE;

	if ( ! parse_cmdline( argc, argv ) ) {
		usage( argv[0] );
		exit( EXIT_FAILURE );
	}

	if ( mode == MODE_SERVER )
		result = server();

	if ( mode == MODE_SHELLCOMMAND )
		return client( MODE_SHELLCOMMAND );

	if ( mode == MODE_PUTFILE )
		return client( MODE_PUTFILE );
	
	if ( result ) 
		return EXIT_SUCCESS;
	else
		return EXIT_FAILURE;
}

