#include "Pump.h"
#include "Shaders.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace ffglex;
using namespace pump;

enum ParamID : FFUInt32
{
	PT_HIT,
	PT_SOURCE,
	PT_PATTERN,
	PT_STYLE,
	PT_DECAY,
	PT_CURVE,
	PT_ATTACK,
	PT_ACCENT,
	PT_SENS,
	// targets
	PT_BRIGHT,
	PT_GLOW,
	PT_ZOOM,
	PT_OPACITY,
	PT_SAT,
	PT_SPLIT,
	PT_SHAKE,
	PT_DEFOCUS,
	PT_HUE,
	PT_SHOCK,
	// keying
	PT_KEYMODE,
	PT_KEYLEVEL,
	PT_KEYSOFT,
	PT_KEYPUMP,
	PT_FFT,
	PT_COUNT
};
static const int kFFTSize = 256;

static CFFGLPluginInfo PluginInfo(
	PluginFactory< Pump >,
	"PUMP",
	"Pump",
	2, 1,
	1, 0,
	FF_EFFECT,
	"Beat-synced pump: punch, sidechain duck, swell or strobe on brightness, glow, zoom, opacity, RGB split, shake, defocus, shockwave. Keys out darks so the layer below is the background.",
	"PUMP FFGL" );

static const char* kKeyNames[ 3 ] = { "Off", "Luma Key", "Screen (Black = Clear)" };

Pump::Pump() : startTime( std::chrono::steady_clock::now() ), fft( kFFTSize, 0.0f )
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	auto option = [ & ]( FFUInt32 id, const char* name, int n, const char* const* names, int def ) {
		SetOptionParamInfo( id, name, n, (float)def );
		for( int i = 0; i < n; ++i ) SetParamElementInfo( id, i, names[ i ], (float)i );
		values[ id ] = (float)def;
	};
	auto slider = [ & ]( FFUInt32 id, const char* name, float def, const char* group ) {
		SetParamInfo( id, name, FF_TYPE_STANDARD, def );
		values[ id ] = def;
		if( group ) SetParamGroup( id, group );
	};

	SetParamInfof( PT_HIT, "Hit", FF_TYPE_EVENT );
	option( PT_SOURCE, "Trigger From", 4, kSourceNames, SRC_BEAT );
	option( PT_PATTERN, "Pattern", PAT_COUNT, kPatternNames, PAT_BEAT );
	option( PT_STYLE, "Style", 4, kStyleNames, STY_PUNCH );
	slider( PT_DECAY, "Decay", settings.decay, nullptr );
	slider( PT_CURVE, "Snap", settings.curve, nullptr );
	slider( PT_ATTACK, "Attack", settings.attack, nullptr );
	slider( PT_ACCENT, "Downbeat Accent", settings.accent, nullptr );
	slider( PT_SENS, "Audio Sensitivity", settings.sensitivity, nullptr );

	// defaults recreate the classic LED-wall pump: flash brighter + glow + small zoom punch
	slider( PT_BRIGHT, "Brightness", 0.6f, "Pump Targets" );
	slider( PT_GLOW, "Glow", 0.5f, "Pump Targets" );
	slider( PT_ZOOM, "Zoom Punch", 0.25f, "Pump Targets" );
	slider( PT_OPACITY, "Opacity", 0.55f, "Pump Targets" );
	slider( PT_SAT, "Saturation", 0.2f, "Pump Targets" );
	slider( PT_SPLIT, "RGB Split", 0.0f, "Pump Targets" );
	slider( PT_SHAKE, "Shake", 0.0f, "Pump Targets" );
	slider( PT_DEFOCUS, "Defocus", 0.0f, "Pump Targets" );
	slider( PT_HUE, "Hue Shift", 0.0f, "Pump Targets" );
	slider( PT_SHOCK, "Shockwave", 0.0f, "Pump Targets" );

	option( PT_KEYMODE, "Key", 3, kKeyNames, 1 );
	slider( PT_KEYLEVEL, "Key Level", 0.12f, "Keying" );
	slider( PT_KEYSOFT, "Key Softness", 0.25f, "Keying" );
	slider( PT_KEYPUMP, "Key Pump", 0.3f, "Keying" );
	SetParamGroup( PT_KEYMODE, "Keying" );

	// Resolume fills this with the live audio spectrum (used by "Audio Kick")
	SetBufferParamInfo( PT_FFT, "Audio", kFFTSize, FF_USAGE_FFT );
	FFGLLog::LogToHost( "Pump created" );
}

Pump::~Pump()
{
}

FFResult Pump::InitGL( const FFGLViewportStruct* vp )
{
	if( !shader.Compile( pump::vertexShader, pump::fragmentShader ) || !quad.Initialise() )
	{
		FFGLLog::LogToHost( "Pump: failed to compile shader" );
		DeInitGL();
		return FF_FAIL;
	}
	return CFFGLPlugin::InitGL( vp );
}

FFResult Pump::DeInitGL()
{
	shader.FreeGLResources();
	quad.Release();
	return FF_SUCCESS;
}

void Pump::SetSampleRate( unsigned int sampleRate )
{
	sampleRateHz = (float)sampleRate;
	CFFGLPlugin::SetSampleRate( sampleRate );
}

FFResult Pump::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( pGL->numInputTextures < 1 || pGL->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;
	const FFGLTextureStruct& input = *pGL->inputTextures[ 0 ];
	FFGLTexCoords maxCoords        = GetMaxGLTexCoords( input );

	settings.source      = (int)values[ PT_SOURCE ];
	settings.pattern     = (int)values[ PT_PATTERN ];
	settings.style       = (int)values[ PT_STYLE ];
	settings.decay       = values[ PT_DECAY ];
	settings.curve       = values[ PT_CURVE ];
	settings.attack      = values[ PT_ATTACK ];
	settings.accent      = values[ PT_ACCENT ];
	settings.sensitivity = values[ PT_SENS ];

	// latest audio spectrum from the host
	if( ParamInfo* info = FindParamInfo( PT_FFT ) )
		for( size_t i = 0; i < fft.size() && i < info->elements.size(); ++i )
			fft[ i ] = info->elements[ i ].value;

	if( barPhase != lastBarPhase && lastBarPhase >= 0.0f ) hostBeat = true;
	lastBarPhase = barPhase;
	double now   = std::chrono::duration< double >( std::chrono::steady_clock::now() - startTime ).count();
	const State& st = envelope.Update( now, bpm, barPhase, hostBeat, settings, fft.data(), (int)fft.size(), sampleRateHz );
	envDisplay      = st.env;

	ScopedShaderBinding shaderBinding( shader.GetGLID() );
	ScopedSamplerActivation s0( 0 );
	Scoped2DTextureBinding t0( input.Handle );
	shader.Set( "InputTexture", 0 );
	shader.Set( "MaxUV", maxCoords.s, maxCoords.t );
	shader.Set( "Resolution", (float)currentViewport.width, (float)currentViewport.height );
	shader.Set( "Env", st.env );
	shader.Set( "SinceHit", std::min( st.sinceHit, 10.0f ) );
	shader.Set( "Shake", st.shakeX, st.shakeY );
	shader.Set( "Brightness", values[ PT_BRIGHT ] );
	shader.Set( "Glow", values[ PT_GLOW ] );
	shader.Set( "Zoom", values[ PT_ZOOM ] );
	shader.Set( "Opacity", values[ PT_OPACITY ] );
	shader.Set( "Saturation", values[ PT_SAT ] );
	shader.Set( "Split", values[ PT_SPLIT ] );
	shader.Set( "ShakeAmt", values[ PT_SHAKE ] );
	shader.Set( "Defocus", values[ PT_DEFOCUS ] );
	shader.Set( "HueShift", values[ PT_HUE ] );
	shader.Set( "Shock", values[ PT_SHOCK ] );
	shader.Set( "KeyMode", (int)values[ PT_KEYMODE ] );
	shader.Set( "KeyLevel", values[ PT_KEYLEVEL ] );
	shader.Set( "KeySoft", values[ PT_KEYSOFT ] );
	shader.Set( "KeyPump", values[ PT_KEYPUMP ] );
	quad.Draw();
	return FF_SUCCESS;
}

FFResult Pump::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PT_FFT ) return FF_FAIL;
	if( index == PT_HIT )
	{
		if( value > 0.5f ) envelope.Trigger();
		return FF_SUCCESS;
	}
	if( index == PT_SOURCE || index == PT_PATTERN || index == PT_STYLE || index == PT_KEYMODE )
		value = (float)std::lround( value );
	values[ index ] = value;
	return FF_SUCCESS;
}

float Pump::GetFloatParameter( unsigned int index )
{
	if( index == PT_HIT || index >= PT_FFT ) return 0.0f;
	return values[ index ];
}

char* Pump::GetParameterDisplay( unsigned int index )
{
	if( index == PT_DECAY )
	{
		std::snprintf( displayBuf, sizeof( displayBuf ), "%.2f beat", 0.08f + values[ PT_DECAY ] * 1.4f );
		return displayBuf;
	}
	if( index == PT_ATTACK )
	{
		std::snprintf( displayBuf, sizeof( displayBuf ), "%d ms", (int)std::lround( values[ PT_ATTACK ] * 60.0f ) );
		return displayBuf;
	}
	return CFFGLPlugin::GetParameterDisplay( index );
}
