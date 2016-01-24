#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <gammaengine/Time.h>
#include <gammaengine/Debug.h>
#include "Controller.h"
#include "ui/Globals.h"

Controller::Controller( const std::string& addr, uint16_t port )
	: Thread()
	, mSocket( new Socket( addr, port, Socket::UDPLite ) )
	, mTicks( 0 )
{
	mJoysticks[0] = Joystick( 0, "/sys/bus/iio/devices/iio:device0/in_voltage0_raw", true );
	mJoysticks[1] = Joystick( 1, "/sys/bus/iio/devices/iio:device0/in_voltage1_raw" );
	mJoysticks[2] = Joystick( 2, "/sys/bus/iio/devices/iio:device0/in_voltage2_raw" );
	mJoysticks[3] = Joystick( 3, "/sys/bus/iio/devices/iio:device0/in_voltage3_raw" );
}


Controller::~Controller()
{
}

Controller::Joystick::Joystick( int id, const std::string& devfile, bool thrust_mode )
	: mId( id )
	, mDevFile( devfile )
	, mCalibrated( false )
	, mThrustMode( thrust_mode )
	, mMin( 0 )
	, mCenter( 32767 )
	, mMax( 65535 )
{
// 	mFd = open( devfile.c_str(), O_RDONLY );

	mMin = getGlobals()->setting( "Joystick:" + std::to_string( mId ) + ":min", 0 );
	mCenter = getGlobals()->setting( "Joystick:" + std::to_string( mId ) + ":cen", 0 );
	mMax = getGlobals()->setting( "Joystick:" + std::to_string( mId ) + ":max", 0 );
	if ( mMin != 0 and mCenter != 0 and mMax != 0 ) {
		mCalibrated = true;
	}
}


Controller::Joystick::~Joystick()
{
	if ( mFd >= 0 ) {
		close( mFd );
	}
}


void Controller::Joystick::SetCalibratedValues( uint16_t min, uint16_t center, uint16_t max )
{
	mMin = min;
	mCenter = center;
	mMax = max;
	mCalibrated = true;
	getGlobals()->setSetting( "Joystick:" + std::to_string( mId ) + ":min", mMin );
	getGlobals()->setSetting( "Joystick:" + std::to_string( mId ) + ":cen", mCenter );
	getGlobals()->setSetting( "Joystick:" + std::to_string( mId ) + ":max", mMax );
	getGlobals()->SaveSettings( "/root/ge/settings.txt" );
}


uint16_t Controller::Joystick::ReadRaw()
{
	char buf[32] = "";
	uint16_t ret = 0;

	mFd = open( mDevFile.c_str(), O_RDONLY );
	int rret = read( mFd, buf, sizeof(buf) );
	close( mFd );
	ret = (uint16_t)atoi( buf );

	return ret;
}


float Controller::Joystick::Read()
{
	uint16_t raw = ReadRaw();

	if ( mThrustMode ) {
		return (float)( raw - mMin ) / (float)( mMax - mMin );
	}

	if ( raw < 0 ) {
		return (float)( ReadRaw() - mCenter ) / (float)( mCenter - mMin );
	}
	return (float)( ReadRaw() - mCenter ) / (float)( mMax - mCenter );
}


bool Controller::run()
{
	float r_thrust = mJoysticks[0].Read();
	float r_yaw = mJoysticks[1].Read();
	float r_pitch = mJoysticks[2].Read();
	float r_roll = mJoysticks[3].Read();
	if ( r_thrust != mThrust ) {
		setThrust( r_thrust );
	}
	if ( r_roll != mRPY.x ) {
		setRoll( r_roll );
	}
	if ( r_pitch != mRPY.y ) {
		setPitch( r_pitch );
	}
	if ( r_yaw != mRPY.z ) {
		setYaw( r_yaw );
	}

	mTicks = Time::WaitTick( 1000 / 100, mTicks );
	return true;
}


void Controller::Arm()
{
	mXferMutex.lock();
	Send( ARM );
	mArmed = ReceiveU32();
	mXferMutex.unlock();
}


void Controller::Disarm()
{
	mXferMutex.lock();
	Send( DISARM );
	mArmed = ReceiveU32();
	mThrust = 0.0f;
	mXferMutex.unlock();
}


void Controller::ResetBattery()
{
	mXferMutex.lock();
	// TODO
// 	Send( RESET_BATTERY, (uint32_t)mSettings->meta( "battery_mah", 2500 ) );
// 	if ( ReceiveU32() != mSettings->meta( "battery_mah", 2500 ) ) {
// 		gDebug() << "Error while resetting battery state !\n";
// 	}
	mXferMutex.unlock();
}


void Controller::setPID( const Vector3f& v )
{
	mXferMutex.lock();
	Send( SET_PID_P, v.x );
	mPID.x = ReceiveFloat();
	Send( SET_PID_I, v.y );
	mPID.y = ReceiveFloat();
	Send( SET_PID_D, v.z );
	mPID.z = ReceiveFloat();
	mXferMutex.unlock();
}


void Controller::setOuterPID( const Vector3f& v )
{
	mXferMutex.lock();
	Send( SET_OUTER_PID_P, v.x );
	mOuterPID.x = ReceiveFloat();
	Send( SET_OUTER_PID_I, v.y );
	mOuterPID.y = ReceiveFloat();
	Send( SET_OUTER_PID_D, v.z );
	mOuterPID.z = ReceiveFloat();
	mXferMutex.unlock();
}


void Controller::setThrust( const float& v )
{
	mXferMutex.lock();
	Send( SET_THRUST, v );
	mThrust = ReceiveFloat();
	mXferMutex.unlock();
}


void Controller::setThrustRelative( const float& v )
{
	mXferMutex.lock();
	Send( SET_THRUST, mThrust + v );
	mThrust = ReceiveFloat();
	mXferMutex.unlock();
}


void Controller::setRoll( const float& v )
{
	mXferMutex.lock();
	Send( SET_ROLL, v );
	mControlRPY.x = ReceiveFloat();
	mXferMutex.unlock();
}


void Controller::setPitch( const float& v )
{
	mXferMutex.lock();
	Send( SET_PITCH, v );
	mControlRPY.y = ReceiveFloat();
	mXferMutex.unlock();
}



void Controller::setYaw( const float& v )
{
	mXferMutex.lock();
	Send( SET_YAW, v );
	mControlRPY.z = ReceiveFloat();
	mXferMutex.unlock();
}


const std::list< Vector3f >& Controller::rpyHistory() const
{
	return mRPYHistory;
}


const std::list< Vector3f >& Controller::outerPidHistory() const
{
	return mOuterPIDHistory;
}


void Controller::Send( const Controller::Cmd& cmd )
{
	uint32_t c = htonl( cmd );
	mSocket->Send( &c, sizeof( c ) );
}


void Controller::Send( const Controller::Cmd& cmd, uint32_t v )
{
	uint32_t data[2] = { htonl( cmd ), htonl( v ) };
	mSocket->Send( data, sizeof( data ) );
}


void Controller::Send( const Controller::Cmd& cmd, float v )
{
	union {
		float f;
		uint32_t u;
	} u;
	u.f = v;
	Send( cmd, u.u );
}


uint32_t Controller::ReceiveU32()
{
	uint32_t ret = 0;
	mSocket->Receive( &ret, sizeof( ret ), true );
	return ntohl( ret );
}


float Controller::ReceiveFloat()
{
	union {
		float f;
		uint32_t u;
	} u;

	u.u = ReceiveU32();
	return u.f;
}