// Runs the real PUMP envelope offline. Writes per frame: env, sinceHit, shakeX, shakeY (float32).
// Usage: pump_sim out.bin frames fps bpm key=value...  (audio=1 synthesises a kick drum spectrum)
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "../source/Pump/Envelope.h"
using namespace pump;
int main( int argc, char** argv )
{
	FILE* f = std::fopen( argv[ 1 ], "wb" );
	int frames = std::atoi( argv[ 2 ] ); float fps = (float)std::atof( argv[ 3 ] ), bpm = (float)std::atof( argv[ 4 ] );
	Settings s; bool audio = false; std::string hits;
	for( int i = 5; i < argc; ++i )
	{
		std::string a = argv[ i ]; size_t e = a.find( '=' ); std::string k = a.substr( 0, e ); float v = (float)std::atof( a.c_str() + e + 1 );
		if( k == "source" ) s.source = (int)v; else if( k == "pattern" ) s.pattern = (int)v; else if( k == "style" ) s.style = (int)v;
		else if( k == "decay" ) s.decay = v; else if( k == "curve" ) s.curve = v; else if( k == "attack" ) s.attack = v;
		else if( k == "accent" ) s.accent = v; else if( k == "audio" ) audio = v > 0.5f; else if( k == "hits" ) hits = a.substr( e + 1 );
	}
	Envelope env; std::vector< float > fft( 256, 0.0f );
	for( int fr = 0; fr < frames; ++fr )
	{
		double t = fr / fps;
		if( audio ) // kick every beat (with a skipped one) + hats noise
		{
			double beat = t * bpm / 60.0, ph = beat - std::floor( beat );
			bool skip = ( (long)std::floor( beat ) % 8 ) == 7;
			float kick = skip ? 0.0f : (float)std::exp( -ph * 14.0 ) * 0.9f;
			for( int i = 0; i < 256; ++i ) fft[ i ] = ( i < 6 ? kick : 0.0f ) + 0.05f * (float)( ( ( fr * 131 + i * 17 ) % 100 ) / 100.0 );
		}
		if( !hits.empty() && ( "," + hits + "," ).find( "," + std::to_string( fr ) + "," ) != std::string::npos ) env.Trigger();
		const State& st = env.Update( t, bpm, 0.0f, false, s, audio ? fft.data() : nullptr, 256, 44100.0f );
		float o[ 4 ] = { st.env, st.sinceHit, st.shakeX, st.shakeY };
		std::fwrite( o, 4, 4, f );
	}
	std::fclose( f );
}
