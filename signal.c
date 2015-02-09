/* signal.c: signal and miscellaneous routines for the ed line editor. */
/*  GNU ed - The GNU line editor.
    Copyright (C) 1993, 1994 Andrew Moore, Talke Studio
    Copyright (C) 2006 Antonio Diaz Diaz.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/

#include <ctype.h>
#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "ed.h"


jmp_buf jmp_state;
static int mutex = 0;			/* If set, signals stay pending */
static int _window_lines = 22;		/* scroll length: ws_row - 2 */
static int _window_columns = 72;
static char sighup_pending = 0;
static char sigint_pending = 0;


void sighup_handler( int signum )
  {
  signum = 0;			/* keep compiler happy */
  if( mutex ) sighup_pending = 1;
  else
    {
    char hb[] = "ed.hup";
    char *hup = 0;             /* hup filename */
    char *s;
    int n, m = 0;

    sighup_pending = 0;
    if( last_addr() && write_file( "ed.hup", "w", 1, last_addr(), 1 ) < 0 &&
        ( s = getenv( "HOME" ) ) != 0 &&
        ( !( n = strlen( s ) ) ||
          ( int )( n + ( m = *( s + n - 1 ) != '/' ) + sizeof( hb ) ) < path_max( 0 ) ) &&
        ( hup = ( char *) malloc( n + m + sizeof( hb ) ) ) != 0 )
      {
      memcpy( hup, s, n );
      memcpy( hup + n, m ? "/" : "", 1 );
      memcpy( hup + n + m, hb, sizeof( hb ) );
      write_file( hup, "w", 1, last_addr(), 1 );
      }
    exit( 2 );
    }
  }


void sigint_handler( int signum )
  {
  if( mutex ) sigint_pending = 1;
  else
    {
    sigset_t set;
    sigint_pending = 0;
    sigemptyset( &set );
    sigaddset( &set, signum );
    sigprocmask( SIG_UNBLOCK, &set, 0 );
    longjmp( jmp_state, -1 );
    }
  }


void sigwinch_handler( int signum )
  {
  signum = 0;			/* keep compiler happy */
#ifdef TIOCGWINSZ
  struct winsize ws;            /* window size structure */

  if( ioctl( 0, TIOCGWINSZ, (char *) &ws ) >= 0 )
    {
    /* Sanity check values of environment vars */
    if( ws.ws_row > 2 && ws.ws_row < 600 ) _window_lines = ws.ws_row - 2;
    if( ws.ws_col > 8 && ws.ws_col < 1800 ) _window_columns = ws.ws_col - 8;
    }
#endif
  }


int set_signal( int signum, void (*handler )( int ) )
  {
  struct sigaction new_action;

  new_action.sa_handler = handler;
  sigemptyset( &new_action.sa_mask );
  new_action.sa_flags = SA_RESTART;
  return sigaction( signum, &new_action, 0 );
  }


void enable_interrupts( void )
  {
  if( --mutex <= 0 )
    {
    mutex = 0;
    if( sighup_pending ) sighup_handler( SIGHUP );
    if( sigint_pending ) sigint_handler( SIGINT );
    }
  }


void disable_interrupts( void ) { ++mutex; }


void set_signals( void )
  {
#ifdef SIGWINCH
  sigwinch_handler( SIGWINCH );
  if( isatty( 0 ) ) set_signal( SIGWINCH, sigwinch_handler );
#endif
  set_signal( SIGHUP, sighup_handler );
  set_signal( SIGQUIT, SIG_IGN );
  set_signal( SIGINT, sigint_handler );
  }


void set_window_lines( const int lines ) { _window_lines = lines; }
int window_columns( void ) { return _window_columns; }
int window_lines( void ) { return _window_lines; }


/* convert a string to int with out_of_range detection */
char parse_int( int *i, const char *str, const char **tail )
  {
  char *tmp;
  errno = 0;
  *i = strtol( str, &tmp, 10 );
  if( tail ) *tail = tmp;
  if( tmp == str )
    {
    set_error_msg( "Bad numerical result" );
    *i = 0;
    return 0;
    }
  if( errno == ERANGE )
    {
    set_error_msg( "Numerical result out of range" );
    *i = 0;
    return 0;
    }
  return 1;
  }


/* assure at least a minimum size for buffer `buf' */
char resize_buffer( char **buf, int *size, int min_size )
  {
  if( *size < min_size )
    {
    const int new_size = ( min_size < 512 ? 512 : ( min_size / 512 ) * 1024 );
    char *new_buf = 0;
    disable_interrupts();
    if( *buf ) new_buf = realloc( *buf, new_size );
    else new_buf = malloc( new_size );
    if( !new_buf )
      {
      show_strerror( 0, errno );
      set_error_msg( "Memory exhausted" );
      enable_interrupts();
      return 0;
      }
    *size = new_size;
    *buf = new_buf;
    enable_interrupts();
    }
  return 1;
  }


/* scan command buffer for next non-space char */
const char *skip_blanks( const char *s )
  {
  while( isspace( (unsigned char)*s ) && *s != '\n' ) ++s;
  return s;
  }


/* return copy of escaped string of at most length PATH_MAX */
const char *strip_escapes( const char *s )
  {
  static char *file = 0;
  static int filesz = 0;

  int i = 0;

  if( !resize_buffer( &file, &filesz, path_max( 0 ) + 1 ) ) return 0;
  /* assert: no trailing escape */
  while( ( file[i++] = ( (*s == '\\' ) ? *++s : *s ) ) )
    s++;
  return file;
  }
