class Robobuilder

  HEADER = "\xFF\xFF\xAA\x55\xAA\x55\x37\xBA"
  MAX_READ_TRIES = 2
  MAX_CMD_TRIES = 5
  DELAY = 0.2
  MOTION_TIMEOUT = 10 * 10
  GETUP_A = 1
  GETUP_B = 2
  TURN_LEFT = 3
  MOTION_FORWARD = 4
  TURN_RIGHT = 5
  MOVE_LEFT = 6
  MOTION_BASIC_POSTURE = 7
  MOVE_RIGHT = 8
  ATTACK_LEFT = 9
  MOVE_BACKWARD = 10
  ATTACK_RIGHT = 11

  class << self

    alias_method :orig_new, :new

    def new( device_name = '/dev/ttyS0' )
      retval = orig_new device_name
      # Do some communication to synchronise.
      3.times { retval.serial_number }
      retval
    end

  end

  alias_method :orig_write, :write

  def write( data )
    n = orig_write( data )
    if n < data.size
      raise "Error writing to serial port (only #{n} of #{data.size} bytes " +
        "were written)" 
    end
    n
  end

  alias_method :orig_read, :read

  def read( n )
    t = 0
    retval = ''
    while retval.size < n and t < MAX_READ_TRIES
      retval += orig_read n - retval.size
      t += 1
    end
    if retval.size < n
      raise "Not receiving any response from serial device " +
        "(only received #{retval.size}/#{n} bytes)" 
    end
    retval
  end

  alias_method :orig_timeout, :timeout

  def timeout( value = nil )
    retval = orig_timeout
    if value
      begin
        self.timeout = value
        yield
      ensure
        self.timeout = retval
      end
    end
    retval
  end

  def checksum( *contents )
    contents.inject { |a,b| a ^ b }
  end

  def hexdump( *contents )
    contents.collect { |x| "%02X" % x }.join( ' ' )
  end

  def response( type )
    header = read HEADER.size
    if header != HEADER
      raise "Expecting #{hexdump( *HEADER.unpack( 'C' * HEADER.size ) )} " +
        "(but received #{hexdump( *header.unpack( 'C' * header.size ) )})"
    end
    result_type = read( 1 ).unpack( 'C' ).first
    if result_type != type
      raise "Result was of type #{ "%02X" % result_type } but command was " +
        "of type #{ "%02X" % type }"
    end
    read 1
    size = read( 4 ).unpack( 'N' ).first
    unless size.between? 1, 16
      raise "Size of result should be between 1 and 16 (but was #{size})"
    end
    contents = read size
    checksum_real = checksum( *contents.unpack( 'C' * contents.size ) )
    checksum_nominal = read( 1 ).unpack( 'C' ).first
    if type != 0x1A and checksum_real != checksum_nominal
      puts "Checksum was #{ "%02X" % checksum_real } but should be " +
        "#{ "%02X" % checksum_nominal }"
    end
    contents
  end

  def command( type, *contents )
    cmd = HEADER + [ type, 0x00, contents.size ].pack( 'CCN' ) +
      contents.pack( 'C' * contents.size ) +
      [ checksum( *contents ) ].pack( 'C' )
    result = nil
    t = 0
    e = nil
    while result == nil and t < MAX_CMD_TRIES
      begin
        e = nil
        write cmd
        result = response type
      rescue Exception => e
        puts "Recovering from error: #{e.to_s}"
        flush
        result = nil
      end
      t += 1
    end
    raise e unless result
    result
  end

  def serial_number
    command( 0x0C, 1 ).to_i
  end

  def firmware
    command( 0x12, 1 ).unpack 'CC'
  end

  def motion( n )
    sleep DELAY
    timeout( MOTION_TIMEOUT ) do
      command( 0x14, n ).unpack( 'C' ).first
    end
    self
  end

  def voice( n )
    command( 0x15, n ).unpack( 'C' ).first
  end

  def distance
    command( 0x16, 1 ).unpack( 'n' ).first / 256.0
  end

  def button # TODO: wait for button
    command( 0x18, 1 ).unpack( 'S' ).first
  end

  def remote # ???
    command( 0x19, 1 )
  end

  def accelerometer
    command( 0x1A, 1 ).unpack( 'sss' )
  end

  def direct( on )
    sleep DELAY
    if on
      # Alternatively press and hold button PF2 while turning on RBC power.
      command 0x10, 1
    else
      write "\xFF\xE0\xFB\x01\x00\x1A"
    end
  end

  def a
    motion GETUP_A
  end

  def b
    motion GETUP_B
  end

  def turn_left
    motion TURN_LEFT
  end

  def forward
    motion MOTION_FORWARD
  end

  def turn_right
    motion TURN_RIGHT
  end

  def left
    motion MOVE_LEFT
  end

  def basic
    motion MOTION_BASIC_POSTURE
  end

  def right
    motion MOVE_RIGHT
  end

  def attack_left
    motion ATTACK_LEFT
  end

  def backward
    motion MOVE_BACKWARD
  end

  def attack_right
    motion ATTACK_RIGHT
  end

  def run( n )
    case n
    when 1 .. 10
      motion n + 11
    when 11 .. 20
      motion n + 22
    else
      raise "Program number must be in 1 .. 20 (was #{n})"
    end
  end

end
