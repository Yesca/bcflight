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
#include <list>
#include <fstream>
#include <string.h>
#include <gammaengine/Debug.h>
#include "Config.h"

using namespace GE;

Config::Config( const std::string& filename )
	: mFilename( filename )
	, L( nullptr )
{
	L = luaL_newstate();
// 	luaL_openlibs( L );

	Reload();
}


Config::~Config()
{
	lua_close(L);
}


std::string Config::ReadFile()
{
	std::string ret = "";
	std::ifstream file( mFilename );

	if ( file.is_open() ) {
		file.seekg( 0, file.end );
		int length = file.tellg();
		char* buf = new char[ length + 1 ];
		file.seekg( 0, file.beg );
		file.read( buf, length );
		buf[length] = 0;
		ret = buf;
		delete buf;
		file.close();
	}

	return ret;
}


void Config::WriteFile( const std::string& content )
{
	std::ofstream file( mFilename );

	if ( file.is_open() ) {
		file.write( content.c_str(), content.length() );
		file.close();
	}
}


std::string Config::string( const std::string& name, const std::string& def )
{
	Debug() << "Config::string( " << name << " )";

	if ( LocateValue( name ) < 0 ) {
		Debug() << " => not found !\n";
		return def;
	}

	std::string ret = lua_tolstring( L, -1, nullptr );
	Debug() << " => " << ret << "\n";
	return ret;
}


int Config::integer( const std::string& name, int def )
{
	Debug() << "Config::integer( " << name << " )";

	if ( LocateValue( name ) < 0 ) {
		Debug() << " => not found !\n";
		return def;
	}

	int ret = lua_tointeger( L, -1 );
	Debug() << " => " << ret << "\n";
	return ret;
}


float Config::number( const std::string& name, float def )
{
	Debug() << "Config::number( " << name << " )";

	if ( LocateValue( name ) < 0 ) {
		Debug() << " => not found !\n";
		return def;
	}

	float ret = lua_tonumber( L, -1 );
	Debug() << " => " << ret << "\n";
	return ret;
}


bool Config::boolean( const std::string& name, bool def )
{
	Debug() << "Config::boolean( " << name << " )";

	if ( LocateValue( name ) < 0 ) {
		Debug() << " => not found !\n";
		return def;
	}

	bool ret = lua_toboolean( L, -1 );
	Debug() << " => " << ret << "\n";
	return ret;
}


std::vector<int> Config::integerArray( const std::string& name )
{
	Debug() << "Config::integerArray( " << name << " )";
	if ( LocateValue( name ) < 0 ) {
		Debug() << " => not found !\n";
		return std::vector<int>();
	}

	std::vector<int> ret;
	size_t len = lua_objlen( L, -1 );
	if ( len > 0 ) {
		for ( size_t i = 1; i <= len; i++ ) {
			lua_rawgeti( L, -1, i );
			int value = lua_tointeger( L, -1 );
			ret.emplace_back( value );
			lua_pop( L, 1 );
		}
	}
	lua_pop( L, 1 );
	Debug() << " => Ok\n";
	return ret;
}


int Config::LocateValue( const std::string& _name )
{
	const char* name = _name.c_str();

	if ( strchr( name, '.' ) == nullptr ) {
		lua_getfield( L, LUA_GLOBALSINDEX, name );
	} else {
		char tmp[128];
		int i, j, k;
		for ( i = 0, j = 0, k = 0; name[i]; i++ ) {
			if ( name[i] == '.' ) {
				tmp[j] = 0;
				if ( k == 0 ) {
					lua_getfield( L, LUA_GLOBALSINDEX, tmp );
				} else {
					lua_getfield( L, -1, tmp );
				}
				if ( lua_type( L, -1 ) == LUA_TNIL ) {
					return -1;
				}
				memset( tmp, 0, sizeof( tmp ) );
				j = 0;
				k++;
			} else {
				tmp[j] = name[i];
				j++;
			}
		}
		tmp[j] = 0;
		lua_getfield( L, -1, tmp );
		if ( lua_type( L, -1 ) == LUA_TNIL ) {
			return -1;
		}
	}

	return 0;
}


void Config::DumpVariable( const std::string& name, int index, int indent )
{
	for ( int i = 0; i < indent; i++ ) {
		Debug() << "    ";
	}
	if ( name != "" ) {
		Debug() << name << " = ";
	}

	if ( indent == 0 ) {
		LocateValue( name );
	}

	if ( lua_isnil( L, index ) ) {
		Debug() << "nil";
	} else if ( lua_isnumber( L, index ) ) {
		Debug() << lua_tonumber( L, index );
	} else if ( lua_isboolean( L, index ) ) {
		Debug() << ( lua_toboolean( L, index ) ? "true" : "false" );
	} else if ( lua_isstring( L, index ) ) {
		Debug() << "\"" << lua_tostring( L, index ) << "\"";
	} else if ( lua_iscfunction( L, index ) ) {
		Debug() << "C-function()";
	} else if ( lua_isuserdata( L, index ) ) {
		Debug() << "__userdata__";
	} else if ( lua_istable( L, index ) ) {
		Debug() << "{\n";
		size_t len = lua_objlen( L, index );
		if ( len > 0 ) {
			for ( size_t i = 1; i <= len; i++ ) {
				lua_rawgeti( L, index, i );
				DumpVariable( "", -1, indent + 1 );
				lua_pop( L, 1 );
				Debug() << ",\n";
			}
		} else {
			lua_pushnil( L );
			while( lua_next( L, -2 ) != 0 ) {
				std::string key = lua_tostring( L, index-1 );
				if ( lua_isnumber( L, index - 1 ) ) {
					key = "[" + key + "]";
				}
				DumpVariable( key, index, indent + 1 );
				lua_pop( L, 1 );
				Debug() << ",\n";
			}
		}
		for ( int i = 0; i < indent; i++ ) {
			Debug() << "    ";
		}
		Debug() << "}";
	} else {
		Debug() << "__unknown__";
	}

	if ( indent == 0 ) {
		Debug() << "\n";
	}
}


void Config::Reload()
{
	luaL_dostring( L, "function Vector( x, y, z, w ) return { x = x, y = y, z = z, w = w } end" );
	luaL_dostring( L, "function Socket( params ) params.link_type = \"Socket\" ; return params end" );
	luaL_dostring( L, "function RawWifi( params ) params.link_type = \"RawWifi\" ; if params.device == nil then params.device = \"wlan0\" end ; if params.blocking == nil then params.blocking = true end ; if params.retries == nil then params.retries = 2 end ; return params end" );
	luaL_dostring( L, "function Voltmeter( params ) params.sensor_type = \"Voltmeter\" ; return params end" );
	luaL_dostring( L, "function Buzzer( params ) params.type = \"Buzzer\" ; return params end" );
	luaL_dostring( L, "battery = {}" );
	luaL_dostring( L, "controller = {}" );
	luaL_dostring( L, "stream = {}" );
	luaL_dostring( L, "touchscreen = {}" );
	luaL_loadfile( L, mFilename.c_str() );
	int ret = lua_pcall( L, 0, LUA_MULTRET, 0 );

	if ( ret != 0 ) {
		gDebug() << "Lua : Error while executing file \"" << mFilename << "\" : \"" << lua_tostring( L, -1 ) << "\"\n";
		return;
	}
}


void Config::Save()
{
	// TODO
}
