/* Robobuilder - Ruby-extension to control Robobuilder
   Copyright (C) 2009 Jan Wedekind

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */
#ifndef NDEBUG
#include <iostream>
#endif
#include <boost/shared_array.hpp>
#include <boost/smart_ptr.hpp>
#include <ruby.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <fcntl.h>
#include <string>
#include <sstream>

class Error: public std::exception
{
public:
  Error(void) {}
  Error( Error &e ): std::exception( e )
  { m_message << e.m_message.str(); }
  virtual ~Error(void) throw() {}
  template< typename T >
  std::ostream &operator<<( const T &t )
  { m_message << t; return m_message; }
  std::ostream &operator<<( std::ostream& (*__pf)( std::ostream&) )
  { (*__pf)( m_message ); return m_message; }
  virtual const char* what(void) const throw() {
    temp = m_message.str();
    return temp.c_str();
    return NULL;
  }
protected:
  std::ostringstream m_message;
  static std::string temp;
};

class Robobuilder
{
public:
  Robobuilder( const std::string &device = "/dev/ttyS0" ) throw (Error);
  virtual ~Robobuilder(void);
  std::string inspect(void) const;
  void close(void);
  int write( const char *data, int length ) throw (Error);
  std::string read( int num ) throw (Error);
  void flush(void) throw (Error);
  int timeout(void);
  void setTimeout( int value ) throw (Error);
  static VALUE cRubyClass;
  static VALUE registerRubyClass(void);
  static void deleteRubyObject( void *ptr );
  static VALUE wrapNew( VALUE rbClass, VALUE rbDevice );
  static VALUE wrapInspect( VALUE rbSelf );
  static VALUE wrapClose( VALUE rbSelf );
  static VALUE wrapWrite( VALUE rbSelf, VALUE rbData );
  static VALUE wrapRead( VALUE rbSelf, VALUE rbNum );
  static VALUE wrapFlush( VALUE rbSelf );
  static VALUE wrapTimeout( VALUE rbSelf );
  static VALUE wrapSetTimeout( VALUE rbSelf, VALUE rbValue );
protected:
  std::string m_device;
  int m_fd;
  struct termios m_tio;
};

typedef boost::shared_ptr< Robobuilder > RobobuilderPtr;

using namespace boost;
using namespace std;

string Error::temp;

#define ERRORMACRO( condition, class, params, message ) \
  if ( !( condition ) ) {                               \
    class _e params;                                    \
    _e << message;                                      \
    throw _e;                                           \
  };

VALUE Robobuilder::cRubyClass = Qnil;

#undef SW_FLOW_CONTROL
#undef HW_FLOW_CONTROL

Robobuilder::Robobuilder( const string &device ) throw (Error):
  m_device(device), m_fd(-1)
{
  try {
    m_fd = open( device.c_str(), O_RDWR | O_NOCTTY | O_NDELAY );
    ERRORMACRO( m_fd != -1, Error, ,
                "Error opening serial port: " << strerror( errno ) );
    int flags = fcntl( m_fd, F_GETFL, 0 );
    ERRORMACRO( fcntl( m_fd, F_SETFL, flags | O_NONBLOCK ) != -1, Error, ,
                "Error switching to non-blocking mode: "
                << strerror( errno ) );
    tcflush( m_fd, TCIOFLUSH );
    bzero( &m_tio, sizeof( m_tio ) );
#ifdef SW_FLOW_CONTROL
    m_tio.c_iflag = IXON |IXOFF;
#else
    m_tio.c_iflag = 0;
#endif
    m_tio.c_oflag = 0;
    m_tio.c_cflag = B115200 | CS8 | CLOCAL | CREAD;
#ifdef HW_FLOW_CONTROL
    m_tio.c_cflag |= CRTSCTS;
#endif
    m_tio.c_lflag = 0;
#ifdef SW_FLOW_CONTROL
    m_tio.c_cc[ VSTART ] = 0x11;
    m_tio.c_cc[ VSTOP ]  = 0x13;
#endif
#ifdef HW_FLOW_CONTROL
    m_tio.c_cc[ VSTART ] = _POSIX_VDISABLE;
    m_tio.c_cc[ VSTOP ]  = _POSIX_VDISABLE;
#endif
    m_tio.c_cc[ VTIME ]  = 1;// * 1/10 second
    m_tio.c_cc[ VMIN ]   = 0;// bytes
    ERRORMACRO( tcsetattr( m_fd, TCSANOW, &m_tio ) != -1, Error, ,
                "Error configuring serial port: " << strerror( errno ) );
    ERRORMACRO( fcntl( m_fd, F_SETFL, flags & ~O_NONBLOCK ) != -1, Error, ,
                "Error switching to blocking mode" );
  } catch ( Error &e ) {
    close();
    throw e;
  };
}

Robobuilder::~Robobuilder(void)
{
  close();
}

string Robobuilder::inspect(void) const
{
  ostringstream s;
  s << "Robobuilder( \"" << m_device << "\" )";
  return s.str();
}

void Robobuilder::close(void)
{
  if ( m_fd != -1 ) {
    ::close( m_fd );
    m_fd = -1;
  };
}

int Robobuilder::write( const char *data, int length ) throw (Error)
{
  ERRORMACRO( m_fd != -1, Error, , "Serial connection is closed" );
  int n = ::write( m_fd, data, length );
  ERRORMACRO( n != -1, Error, , "Error writing to serial device: "
              << strerror( errno ) );
  return n;
}

void Robobuilder::flush(void) throw (Error)
{
  ERRORMACRO( m_fd != -1, Error, , "Serial connection is closed" );
  ERRORMACRO( tcflush( m_fd, TCIOFLUSH ) != -1, Error, ,
              "Error flushing serial I/O: "
              << strerror( errno ) );
}

int Robobuilder::timeout(void)
{
  ERRORMACRO( m_fd != -1, Error, , "Serial connection is closed" );
  int retVal;
  if ( m_tio.c_cc[ VMIN ] == 0 )
    retVal = m_tio.c_cc[ VTIME ];
  else
    retVal = INT_MAX;
  return retVal;
}

void Robobuilder::setTimeout( int value ) throw (Error)
{
  ERRORMACRO( m_fd != -1, Error, , "Serial connection is closed" );
  if ( value == INT_MAX ) {
    m_tio.c_cc[ VMIN ] = 1;
    m_tio.c_cc[ VTIME ] = 0;
  } else {
    m_tio.c_cc[ VMIN ] = 0;
    m_tio.c_cc[ VTIME ] = value;
  };
  ERRORMACRO( tcsetattr( m_fd, TCSANOW, &m_tio ) != -1, Error, ,
              "Error configuring serial port: " << strerror( errno ) );
}

std::string Robobuilder::read( int num ) throw (Error)
{
  ERRORMACRO( m_fd != -1, Error, , "Serial connection is closed" );
  shared_array< char > buffer( new char[ num ] );
  int n = ::read( m_fd, buffer.get(), num );
  ERRORMACRO( n != -1, Error, , "Error reading from serial device: "
              << strerror( errno ) );
  return string( buffer.get(), n );
}

VALUE Robobuilder::registerRubyClass(void)
{
  cRubyClass = rb_define_class( "Robobuilder", rb_cObject );
  rb_define_const( cRubyClass, "INFINITE", INT2NUM(INT_MAX) );
  rb_define_singleton_method( cRubyClass, "new",
                              RUBY_METHOD_FUNC( wrapNew ), 1 );
  rb_define_method( cRubyClass, "inspect",
                    RUBY_METHOD_FUNC( wrapInspect ), 0 );
  rb_define_method( cRubyClass, "close",
                    RUBY_METHOD_FUNC( wrapClose ), 0 );
  rb_define_method( cRubyClass, "write",
                    RUBY_METHOD_FUNC( wrapWrite ), 1 );
  rb_define_method( cRubyClass, "read",
                    RUBY_METHOD_FUNC( wrapRead ), 1 );
  rb_define_method( cRubyClass, "flush",
                    RUBY_METHOD_FUNC( wrapFlush ), 0 );
  rb_define_method( cRubyClass, "timeout",
                    RUBY_METHOD_FUNC( wrapTimeout ), 0 );
  rb_define_method( cRubyClass, "timeout=",
                    RUBY_METHOD_FUNC( wrapSetTimeout ), 1 );
  return cRubyClass;
}

void Robobuilder::deleteRubyObject( void *ptr )
{
  delete (RobobuilderPtr *)ptr;
}


VALUE Robobuilder::wrapNew( VALUE rbClass, VALUE rbDevice )
{
  VALUE retVal = Qnil;
  try {
    rb_check_type( rbDevice, T_STRING );
    RobobuilderPtr ptr( new Robobuilder( StringValuePtr( rbDevice ) ) );
    retVal = Data_Wrap_Struct( rbClass, 0, deleteRubyObject,
                               new RobobuilderPtr( ptr ) );
  } catch ( std::exception &e ) {
    rb_raise( rb_eRuntimeError, e.what() );
  };
  return retVal;
}

VALUE Robobuilder::wrapInspect( VALUE rbSelf )
{
  RobobuilderPtr *self; Data_Get_Struct( rbSelf, RobobuilderPtr, self );
  string retVal( (*self)->inspect() );
  return rb_str_new( retVal.c_str(), retVal.length() );
}

VALUE Robobuilder::wrapClose( VALUE rbSelf )
{
  RobobuilderPtr *self; Data_Get_Struct( rbSelf, RobobuilderPtr, self );
  (*self)->close();
  return rbSelf;
}

VALUE Robobuilder::wrapWrite( VALUE rbSelf, VALUE rbData )
{
  VALUE rbRetVal = Qnil;
  try {
    RobobuilderPtr *self; Data_Get_Struct( rbSelf, RobobuilderPtr, self );
    rbRetVal = INT2NUM( (*self)->write( StringValuePtr( rbData ),
                                        RSTRING_LEN( rbData ) ) );
  } catch ( std::exception &e ) {
    rb_raise( rb_eRuntimeError, e.what() );
  };
  return rbRetVal;
}

VALUE Robobuilder::wrapRead( VALUE rbSelf, VALUE rbNum )
{
  VALUE rbRetVal = Qnil;
  try {
    RobobuilderPtr *self; Data_Get_Struct( rbSelf, RobobuilderPtr, self );
    string retVal( (*self)->read( NUM2INT( rbNum ) ) );
    rbRetVal = rb_str_new( retVal.c_str(), retVal.length() );
  } catch ( std::exception &e ) {
    rb_raise( rb_eRuntimeError, e.what() );
  };
  return rbRetVal;
}

VALUE Robobuilder::wrapFlush( VALUE rbSelf )
{
  try {
    RobobuilderPtr *self; Data_Get_Struct( rbSelf, RobobuilderPtr, self );
    (*self)->flush();
  } catch ( std::exception &e ) {
    rb_raise( rb_eRuntimeError, e.what() );
  };
  return Qnil;
}

VALUE Robobuilder::wrapTimeout( VALUE rbSelf )
{
  VALUE rbRetVal = Qnil;
  try {
    RobobuilderPtr *self; Data_Get_Struct( rbSelf, RobobuilderPtr, self );
    rbRetVal = INT2NUM( (*self)->timeout() );
  } catch ( std::exception &e ) {
    rb_raise( rb_eRuntimeError, e.what() );
  };
  return rbRetVal;
}

VALUE Robobuilder::wrapSetTimeout( VALUE rbSelf, VALUE rbValue )
{
  try {
    RobobuilderPtr *self; Data_Get_Struct( rbSelf, RobobuilderPtr, self );
    (*self)->setTimeout( NUM2INT( rbValue ) );
  } catch ( std::exception &e ) {
    rb_raise( rb_eRuntimeError, e.what() );
  };
  return rbValue;
}

extern "C" {

  void Init_robobuilder(void)
  {
    Robobuilder::registerRubyClass();
    rb_require( "robobuilder_ext.rb" );
  }

}
