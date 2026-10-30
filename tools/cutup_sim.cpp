// Runs the real CUTUP engine offline and writes the primitive list of every frame to a binary file:
// per frame: int32 count, then count * 16 floats. Usage: cutup_sim out.bin frames fps bpm aspect key=value...
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "../source/Cutup/Engine.h"
using namespace cutup;
int main( int argc, char** argv )
{
	if( argc < 6 ) { std::fprintf( stderr, "usage\n" ); return 1; }
	FILE* f = std::fopen( argv[ 1 ], "wb" );
	int frames = std::atoi( argv[ 2 ] ); float fps = (float)std::atof( argv[ 3 ] );
	float bpm = (float)std::atof( argv[ 4 ] ); float aspect = (float)std::atof( argv[ 5 ] );
	Settings s;
	const char* defs[] = { "2026", "RECAP", "MOMENTS", "NO\xC4\x86 ZA|PAM\xC4\x86" "ENJE", "LIVE" };
	for( int i = 0; i < 5; ++i ) s.texts[ i ] = defs[ i ];
	std::string triggers;
	for( int i = 6; i < argc; ++i )
	{
		std::string a = argv[ i ]; size_t e = a.find( '=' ); std::string k = a.substr( 0, e ), v = a.substr( e + 1 );
		float x = (float)std::atof( v.c_str() );
		if( k == "div" ) s.divisionBeats = x; else if( k == "order" ) s.sceneOrder = (int)x;
		else if( k == "textorder" ) s.textOrder = (int)x; else if( k == "reveal" ) s.reveal = (int)x;
		else if( k == "font" ) s.font = (int)x; else if( k == "size" ) s.textSize = x;
		else if( k == "pos" ) s.textPos = (int)x; else if( k == "tint" ) s.tint = x;
		else if( k == "overlay" ) s.overlay = (int)x; else if( k == "flash" ) s.flash = x;
		else if( k == "speed" ) s.speed = x; else if( k == "auto" ) s.autoBeat = x > 0.5f;
		else if( k == "scenes" ) { for( int j = 0; j < kSceneCount; ++j ) s.sceneOn[ j ] = v.find( char( '0' + j ) ) != std::string::npos; }
		else if( k.rfind( "text", 0 ) == 0 && k.size() > 4 ) s.texts[ std::atoi( k.c_str() + 4 ) - 1 ] = v;
		else if( k == "triggers" ) triggers = v; // comma separated frame numbers for manual "Next" presses
	}
	Engine e;
	for( int fr = 0; fr < frames; ++fr )
	{
		std::string tag = "," + std::to_string( fr ) + ",";
		if( ( "," + triggers + "," ).find( tag ) != std::string::npos ) e.Trigger();
		e.Update( fr / fps, bpm, -1.0f, aspect, s );
		int n = (int)e.Prims().size();
		std::fwrite( &n, 4, 1, f );
		std::fwrite( e.Prims().data(), sizeof( Prim ), n, f );
	}
	std::fclose( f );
	return 0;
}
