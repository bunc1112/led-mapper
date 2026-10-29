// Writes the plugin's GLSL (exactly as compiled in the plugin) to files for the preview harness.
#include <cstdio>
#include "../source/Chronos/Shaders.h"
static void w( const char* n, const char* s ) { FILE* f = std::fopen( n, "w" ); std::fputs( s, f ); std::fclose( f ); }
int main()
{
	w( "vertex.glsl", chronos::vertexShader );
	w( "copy.glsl", chronos::copyShader );
	w( "state.glsl", chronos::stateShader );
	w( "main.glsl", chronos::mainShader );
	return 0;
}
