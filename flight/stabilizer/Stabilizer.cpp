/*
 * BCFlight
 * Copyright (C) 2016 Adrien Aubry (drich)
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
**/

#include <unistd.h>
#include <math.h>
#include <Debug.h>
#include <Main.h>
#include <Board.h>
#include <IMU.h>
#include <Controller.h>
#include "Stabilizer.h"

Stabilizer::Stabilizer( Main* main, Frame* frame )
	: mFrame( frame )
	, mMode( Stabilize )
	, mAltitudeHold( false )
	, mRateRollPID( PID<float>() )
	, mRatePitchPID( PID<float>() )
	, mRateYawPID( PID<float>() )
	, mHorizonPID( PID<Vector3f>() )
	, mAltitudePID( PID<float>() )
	, mAltitudeControl( 0.0f )
	, mLockState( 0 )
	, mHorizonMultiplier( Vector3f( 15.0f, 15.0f, 1.0f ) )
	, mHorizonOffset( Vector3f() )
{
	mRateRollPID.setP( std::atof( Board::LoadRegister( "PID:Roll:P" ).c_str() ) );
	mRateRollPID.setI( std::atof( Board::LoadRegister( "PID:Roll:I" ).c_str() ) );
	mRateRollPID.setD( std::atof( Board::LoadRegister( "PID:Roll:D" ).c_str() ) );
	mRatePitchPID.setP( std::atof( Board::LoadRegister( "PID:Pitch:P" ).c_str() ) );
	mRatePitchPID.setI( std::atof( Board::LoadRegister( "PID:Pitch:I" ).c_str() ) );
	mRatePitchPID.setD( std::atof( Board::LoadRegister( "PID:Pitch:D" ).c_str() ) );
	mRateYawPID.setP( std::atof( Board::LoadRegister( "PID:Yaw:P" ).c_str() ) );
	mRateYawPID.setI( std::atof( Board::LoadRegister( "PID:Yaw:I" ).c_str() ) );
	mRateYawPID.setD( std::atof( Board::LoadRegister( "PID:Yaw:D" ).c_str() ) );

	mHorizonPID.setP( std::atof( Board::LoadRegister( "PID:Outerloop:P" ).c_str() ) );
	mHorizonPID.setI( std::atof( Board::LoadRegister( "PID:Outerloop:I" ).c_str() ) );
	mHorizonPID.setD( std::atof( Board::LoadRegister( "PID:Outerloop:D" ).c_str() ) );

	mAltitudePID.setP( 0.001 );
	mAltitudePID.setI( 0.010 );
	mAltitudePID.setDeadBand( 0.05f );

	float v;
	if ( ( v = main->config()->number( "stabilizer.horizon_angles.x" ) ) > 0.0f ) {
		mHorizonMultiplier.x = v;
	}
	if ( ( v = main->config()->number( "stabilizer.horizon_angles.y" ) ) > 0.0f ) {
		mHorizonMultiplier.y = v;
	}

// 	mHorizonPID.setDeadBand( Vector3f( 0.5f, 0.5f, 0.0f ) );

	mRateFactor = main->config()->number( "stabilizer.rate_speed" );
	if ( mRateFactor <= 0.0f ) {
		mRateFactor = 200.0f;
	}
}


void Stabilizer::setRollP( float p )
{
	mRateRollPID.setP( p );
	Board::SaveRegister( "PID:Roll:P", std::to_string( p ) );
}


void Stabilizer::setRollI( float i )
{
	mRateRollPID.setI( i );
	Board::SaveRegister( "PID:Roll:I", std::to_string( i ) );
}


void Stabilizer::setRollD( float d )
{
	mRateRollPID.setD( d );
	Board::SaveRegister( "PID:Roll:D", std::to_string( d ) );
}


Vector3f Stabilizer::getRollPID() const
{
	return mRateRollPID.getPID();
}


void Stabilizer::setPitchP( float p )
{
	mRatePitchPID.setP( p );
	Board::SaveRegister( "PID:Pitch:P", std::to_string( p ) );
}


void Stabilizer::setPitchI( float i )
{
	mRatePitchPID.setI( i );
	Board::SaveRegister( "PID:Pitch:I", std::to_string( i ) );
}


void Stabilizer::setPitchD( float d )
{
	mRatePitchPID.setD( d );
	Board::SaveRegister( "PID:Pitch:D", std::to_string( d ) );
}


Vector3f Stabilizer::getPitchPID() const
{
	return mRatePitchPID.getPID();
}


void Stabilizer::setYawP( float p )
{
	mRateYawPID.setP( p );
	Board::SaveRegister( "PID:Yaw:P", std::to_string( p ) );
}


void Stabilizer::setYawI( float i )
{
	mRateYawPID.setI( i );
	Board::SaveRegister( "PID:Yaw:I", std::to_string( i ) );
}


void Stabilizer::setYawD( float d )
{
	mRateYawPID.setD( d );
	Board::SaveRegister( "PID:Yaw:D", std::to_string( d ) );
}


Vector3f Stabilizer::getYawPID() const
{
	return mRateYawPID.getPID();
}


Vector3f Stabilizer::lastPIDOutput() const
{
	return Vector3f( mRateRollPID.state(), mRatePitchPID.state(), mRateYawPID.state() );
}


void Stabilizer::setOuterP( float p )
{
	mHorizonPID.setP( p );
	Board::SaveRegister( "PID:Outerloop:P", std::to_string( p ) );
}


void Stabilizer::setOuterI( float i )
{
	mHorizonPID.setI( i );
	Board::SaveRegister( "PID:Outerloop:I", std::to_string( i ) );
}


void Stabilizer::setOuterD( float d )
{
	mHorizonPID.setD( d );
	Board::SaveRegister( "PID:Outerloop:D", std::to_string( d ) );
}


Vector3f Stabilizer::getOuterPID() const
{
	return mHorizonPID.getPID();
}


Vector3f Stabilizer::lastOuterPIDOutput() const
{
	return mHorizonPID.state();
}


void Stabilizer::setHorizonOffset( const Vector3f& v )
{
	mHorizonOffset = v;
}


Vector3f Stabilizer::horizonOffset() const
{
	return mHorizonOffset;
}


void Stabilizer::setMode( uint32_t mode )
{
	mMode = (Mode)mode;
}


uint32_t Stabilizer::mode() const
{
	return (uint32_t)mMode;
}


void Stabilizer::setAltitudeHold( bool enabled )
{
	mAltitudeHold = enabled;
}


bool Stabilizer::altitudeHold() const
{
	return mAltitudeHold;
}


void Stabilizer::Reset( const float& yaw )
{
	mRateRollPID.Reset();
	mRatePitchPID.Reset();
	mRateYawPID.Reset();
	mHorizonPID.Reset();
}


void Stabilizer::Update( IMU* imu, Controller* ctrl, float dt )
{
	Vector3f rate_control = Vector3f();

	if ( mLockState >= 1 ) {
		mLockState = 2;
		return;
	}

	if ( not ctrl->armed() ) {
		return;
	}

	switch ( mMode ) {
		case Rate : {
			rate_control = ctrl->RPY() * mRateFactor;
			break;
		}
		case ReturnToHome :
		case Follow :
		case Stabilize :
		default : {
			Vector3f control_angles = ctrl->RPY();
			control_angles.x = mHorizonMultiplier.x * std::min( std::max( control_angles.x, -1.0f ), 1.0f ) + mHorizonOffset.x;
			control_angles.y = mHorizonMultiplier.y * std::min( std::max( control_angles.y, -1.0f ), 1.0f ) + mHorizonOffset.y;
			mHorizonPID.Process( control_angles, imu->RPY(), dt );
			rate_control = mHorizonPID.state();
			rate_control.z = control_angles.z * mRateFactor; // TEST : Bypass heading for now
			break;
		}
	}

	mRateRollPID.Process( rate_control.x, imu->rate().x, dt );
	mRatePitchPID.Process( rate_control.y, imu->rate().y, dt );
	mRateYawPID.Process( rate_control.z, imu->rate().z, dt );

	float thrust = ctrl->thrust();
	if ( mAltitudeHold ) {
		thrust = thrust * 2.0f - 1.0f;
		if ( std::abs( thrust ) < 0.1f ) {
			thrust = 0.0f;
		} else if ( thrust > 0.0f ) {
			thrust = ( thrust - 0.1f ) * ( 1.0f / 0.9f );
		} else if ( thrust < 0.0f ) {
			thrust = ( thrust + 0.1f ) * ( 1.0f / 0.9f );
		}
		thrust *= 0.01f; // Reduce to 1m/s (assuming controller is sending at 100Hz update rate)
		mAltitudeControl += thrust;
		mAltitudePID.Process( mAltitudeControl, imu->altitude(), dt );
		thrust = mAltitudePID.state();
	}

	if ( mFrame->Stabilize( Vector3f( mRateRollPID.state(), mRatePitchPID.state(), mRateYawPID.state() ), thrust ) == false ) {
		Reset( mHorizonPID.state().z );
	}
}


void Stabilizer::CalibrateESCs()
{
	mLockState = 1;
	while ( mLockState != 2 ) {
		usleep( 1000 * 10 );
	}

	mFrame->CalibrateESCs();

	mLockState = 0;
}
